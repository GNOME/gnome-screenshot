#include <config.h>
#include "screenshot-utils.h"
#include <gdk/gdkx.h>
#include <gnome.h>

#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
#include <X11/extensions/shape.h>
#endif

static GtkWidget *selection_window;

#define SELECTION_NAME "_GNOME_PANEL_SCREENSHOT"

/* To make sure there is only one screenshot taken at a time,
 * (Imagine key repeat for the print screen key) we hold a selection
 * until we are done taking the screenshot
 */
gboolean
screenshot_grab_lock (void)
{
  Atom selection_atom;
  GdkCursor *cursor;
  gboolean result = FALSE;

  selection_atom = gdk_x11_get_xatom_by_name (SELECTION_NAME);
  XGrabServer (GDK_DISPLAY ());
  if (XGetSelectionOwner (GDK_DISPLAY(), selection_atom) != None)
    goto out;

  selection_window = gtk_invisible_new ();
  gtk_widget_show (selection_window);

  if (!gtk_selection_owner_set (selection_window,
				gdk_atom_intern (SELECTION_NAME, FALSE),
				GDK_CURRENT_TIME))
    {
      gtk_widget_destroy (selection_window);
      selection_window = NULL;
      goto out;
    }

  cursor = gdk_cursor_new (GDK_WATCH);
  gdk_pointer_grab (selection_window->window, FALSE, 0, NULL,
		    cursor, GDK_CURRENT_TIME);
  gdk_cursor_unref (cursor);

  result = TRUE;

 out:
  XUngrabServer (GDK_DISPLAY ());
  gdk_flush ();

  return result;
}

void
screenshot_release_lock (void)
{
  if (selection_window)
    {
      gtk_widget_destroy (selection_window);
      selection_window = NULL;
    }
  gdk_flush ();
}


/* Functions to convert string  */
static char*
text_property_to_utf8 (const XTextProperty *prop)
{
  char **list;
  int count;
  char *retval;
  
  list = NULL;

  count = gdk_text_property_to_utf8_list (gdk_x11_xatom_to_atom (prop->encoding),
                                          prop->format,
                                          prop->value,
                                          prop->nitems,
                                          &list);

  if (count == 0)
    return NULL;

  retval = list[0];
  list[0] = g_strdup (""); /* something to free */
  
  g_strfreev (list);

  return retval;
}

static char*
get_text_property (Window  xwindow,
		   Atom    atom)
{
  XTextProperty text;
  char *retval;
  
  gdk_error_trap_push ();

  text.nitems = 0;
  if (XGetTextProperty (gdk_display,
                        xwindow,
                        &text,
                        atom))
    {
      retval = text_property_to_utf8 (&text);

      if (text.nitems > 0)
        XFree (text.value);
    }
  else
    {
      retval = NULL;
    }
  
  gdk_error_trap_pop ();

  return retval;
}

static char *
get_utf8_property (Window  xwindow,
		   Atom    atom)
{
  Atom type;
  int format;
  gulong nitems;
  gulong bytes_after;
  guchar *val;
  int err, result;
  char *retval;
  Atom utf8_string;

  utf8_string = gdk_x11_get_xatom_by_name ("UTF8_STRING");

  gdk_error_trap_push ();
  type = None;
  val = NULL;
  result = XGetWindowProperty (gdk_display,
			       xwindow,
			       atom,
			       0, G_MAXLONG,
			       False, utf8_string,
			       &type, &format, &nitems,
			       &bytes_after, (guchar **)&val);  
  err = gdk_error_trap_pop ();

  if (err != Success ||
      result != Success)
    return NULL;
  
  if (type != utf8_string ||
      format != 8 ||
      nitems == 0)
    {
      if (val)
        XFree (val);
      return NULL;
    }

  if (!g_utf8_validate (val, nitems, NULL))
    {
      g_warning ("Property %s contained invalid UTF-8\n",
		 gdk_x11_get_xatom_name (atom));
      XFree (val);
      return NULL;
    }
  
  retval = g_strndup (val, nitems);
  
  XFree (val);
  
  return retval;
}

gchar *
screenshot_get_window_title (Window w)
{
  gchar *name;

  name = get_utf8_property (w, gdk_x11_get_xatom_by_name ("_NET_WM_NAME"));

  if (name)
    return name;

  name = get_text_property (w, gdk_x11_get_xatom_by_name ("WM_NAME"));

  if (name)
    return name;
  
  name = get_text_property (w, gdk_x11_get_xatom_by_name ("WM_CLASS"));

  if (name)
    return name;

  return g_strdup (_("Untitled Window"));
}

Window
find_toplevel_window (Window xid)
{
  Window root, parent, *children;
  int nchildren;

  do
    {
      if (XQueryTree (GDK_DISPLAY (), xid, &root,
		      &parent, &children, &nchildren) == 0)
	{
	  g_warning ("Couldn't find window manager window");
	  return 0;
	}

      if (root == parent)
	return xid;

      xid = parent;
    }
  while (TRUE);
}

/* We don't actually honor include_decoration here.  We need to search
 * for WM_STATE;
 */
Window
screenshot_find_pointer_window (gboolean include_decoration)
{
  Display *display;
  Window root_window, root_return, child, toplevel;
  int unused;
  guint mask;

  display = GDK_DISPLAY ();
  root_window = GDK_ROOT_WINDOW ();
	
  XQueryPointer (display,
		 root_window,
		 &root_return,
		 &child,
		 &unused,
		 &unused,
		 &unused,
		 &unused,
		 &mask);

  if (child == None)
    {
      toplevel = root_return;
    }
  else
    {
      toplevel = find_toplevel_window (child);
    }

  return toplevel;
}

GdkPixbuf *
screenshot_get_pixbuf (Window w)
{
  GdkWindow *window;
  GdkPixbuf *screenshot;
  gint x_real_orig, y_real_orig;
  gint x_orig, y_orig;
  gint x = 0, y = 0;
  gint real_width, real_height;
  gint width, height;

#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
  XRectangle *rectangles;
  GdkPixbuf *tmp;
  int rectangle_count, rectangle_order, i;
#endif

  
  window = gdk_window_foreign_new (w);
  if (window == NULL)
    return NULL;

  gdk_drawable_get_size (window, &real_width, &real_height);
  gdk_window_get_origin (window, &x_real_orig, &y_real_orig);

  x_orig = x_real_orig;
  y_orig = y_real_orig;
  width = real_width;
  height = real_height;
	
  if (x_orig < 0)
    {
      x = - x_orig;
      width = width + x_orig;
      x_orig = 0;
    }
  if (y_orig < 0)
    {
      y = - y_orig;
      height = height + y_orig;
      y_orig = 0;
    }

  if (x_orig + width > gdk_screen_width ())
    width = gdk_screen_width () - x_orig;
  if (y_orig + height > gdk_screen_height ())
    height = gdk_screen_height () - y_orig;


#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
  tmp = gdk_pixbuf_get_from_drawable (NULL, window, NULL,
				      x, y, 0, 0,
				      width, height);

  rectangles = XShapeGetRectangles (GDK_DISPLAY (), GDK_WINDOW_XWINDOW (window),
				    ShapeBounding, &rectangle_count, &rectangle_order);
  if (rectangle_count > 0)
    {
      gboolean has_alpha = gdk_pixbuf_get_has_alpha (tmp);

      screenshot = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
				   width, height);
      gdk_pixbuf_fill (screenshot, 0);
	
      for (i = 0; i < rectangle_count; i++)
	{
	  gint rec_x, rec_y;
	  gint rec_width, rec_height;

	  rec_x = rectangles[i].x;
	  rec_y = rectangles[i].y;
	  rec_width = rectangles[i].width;
	  rec_height = rectangles[i].height;

	  if (x_real_orig < 0)
	    {
	      rec_x += x_real_orig;
	      rec_x = MAX(rec_x, 0);
	      rec_width += x_real_orig;
	    }
	  if (y_real_orig < 0)
	    {
	      rec_y += y_real_orig;
	      rec_y = MAX(rec_y, 0);
	      rec_height += y_real_orig;
	    }

	  if (x_orig + rec_x + rec_width > gdk_screen_width ())
	    rec_width = gdk_screen_width () - x_orig - rec_x;
	  if (y_orig + rec_y + rec_height > gdk_screen_height ())
	    rec_height = gdk_screen_height () - y_orig - rec_y;

	  for (y = rec_y; y < rec_y + rec_height; y++)
	    {
	      guchar *src_pixels, *dest_pixels;
	      
	      src_pixels = gdk_pixbuf_get_pixels (tmp) +
		y * gdk_pixbuf_get_rowstride(tmp) +
		rec_x * (has_alpha ? 4 : 3);
	      dest_pixels = gdk_pixbuf_get_pixels (screenshot) +
		y * gdk_pixbuf_get_rowstride (screenshot) +
		rec_x * 4;
				
	      for (x = 0; x < rec_width; x++)
		{
		  *dest_pixels++ = *src_pixels ++;
		  *dest_pixels++ = *src_pixels ++;
		  *dest_pixels++ = *src_pixels ++;
		  *dest_pixels++ = 255;
		  if (has_alpha)
		    src_pixels++;
		}
	    }
	}
      g_object_unref (tmp);
    }
  else
    {
      screenshot = tmp;
    }
#else /* HAVE_X11_EXTENSIONS_SHAPE_H */
  screenshot = gdk_pixbuf_get_from_drawable (NULL, window, NULL,
					     x, y, 0, 0,
					     width, height);
#endif /* HAVE_X11_EXTENSIONS_SHAPE_H */

	/* Add a drop_shadow, if needed */
#if 0
  g_print ("add shadow\n");
  if (drop_shadow) {
    GdkPixbuf *old = screenshot;
    
    screenshot = create_shadowed_pixbuf (screenshot);
    g_object_unref (old);
  }
#endif
  return screenshot;
}

#if 0
gboolean
take_window_shot (void)
{
	GdkWindow *window, *toplevel_window;
	Display *disp;
	Window w, root, child, toplevel;
	int unused;
	guint mask;
	gint x_real_orig, y_real_orig;
	gint x_orig, y_orig;
	gint x = 0, y = 0;
	gint real_width, real_height;
	gint width, height;

#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
	XRectangle *rectangles;
	GdkPixbuf *tmp;
	int rectangle_count, rectangle_order, i;
#endif

	
	disp = GDK_DISPLAY ();
	w = GDK_ROOT_WINDOW ();
	
	XQueryPointer (disp, w, &root, &child,
		       &unused,
		       &unused,
		       &unused,
		       &unused,
		       &mask);

	if (child == None) {
                window = gdk_get_default_root_window ();
	} else {

                window = gdk_window_foreign_new (child);
		if (window == NULL)
			return FALSE;

		toplevel = find_toplevel_window (child);

		//window_title = screenshot_get_window_title (toplevel);
		
		/* Force window to be shown */
		toplevel_window	 = gdk_window_foreign_new (toplevel);
		gdk_window_show (toplevel_window);
	}

	gdk_drawable_get_size (window, &real_width, &real_height);
	gdk_window_get_origin (window, &x_real_orig, &y_real_orig);

	x_orig = x_real_orig;
	y_orig = y_real_orig;
	width = real_width;
	height = real_height;
	
	if (x_orig < 0) {
		x = - x_orig;
		width = width + x_orig;
		x_orig = 0;
	}
	if (y_orig < 0) {
		y = - y_orig;
		height = height + y_orig;
		y_orig = 0;
	}

	if (x_orig + width > gdk_screen_width ())
		width = gdk_screen_width () - x_orig;
	if (y_orig + height > gdk_screen_height ())
		height = gdk_screen_height () - y_orig;


#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
	tmp = gdk_pixbuf_get_from_drawable (NULL, window, NULL,
					    x, y, 0, 0,
					    width, height);

	rectangles = XShapeGetRectangles (GDK_DISPLAY (), GDK_WINDOW_XWINDOW (window),
					  ShapeBounding, &rectangle_count, &rectangle_order);
	if (rectangle_count > 0) {
		gboolean has_alpha = gdk_pixbuf_get_has_alpha (tmp);

		screenshot = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
					     width, height);
		gdk_pixbuf_fill (screenshot, 0);
	
		for (i = 0; i < rectangle_count; i++) {
			gint rec_x, rec_y;
			gint rec_width, rec_height;

			rec_x = rectangles[i].x;
			rec_y = rectangles[i].y;
			rec_width = rectangles[i].width;
			rec_height = rectangles[i].height;

			if (x_real_orig < 0) {
				rec_x += x_real_orig;
				rec_x = MAX(rec_x, 0);
				rec_width += x_real_orig;
			}
			if (y_real_orig < 0) {
				rec_y += y_real_orig;
				rec_y = MAX(rec_y, 0);
				rec_height += y_real_orig;
			}

			if (x_orig + rec_x + rec_width > gdk_screen_width ())
				rec_width = gdk_screen_width () - x_orig - rec_x;
			if (y_orig + rec_y + rec_height > gdk_screen_height ())
				rec_height = gdk_screen_height () - y_orig - rec_y;

			for (y = rec_y; y < rec_y + rec_height; y++) {
				guchar *src_pixels, *dest_pixels;
				
				src_pixels = gdk_pixbuf_get_pixels (tmp) +
					y * gdk_pixbuf_get_rowstride(tmp) +
					rec_x * (has_alpha ? 4 : 3);
				dest_pixels = gdk_pixbuf_get_pixels (screenshot) +
					y * gdk_pixbuf_get_rowstride (screenshot) +
					rec_x * 4;
				
				for (x = 0; x < rec_width; x++) {
					*dest_pixels++ = *src_pixels ++;
					*dest_pixels++ = *src_pixels ++;
					*dest_pixels++ = *src_pixels ++;
					*dest_pixels++ = 255;
					if (has_alpha)
						src_pixels++;
				}
			}
		}
		g_object_unref (tmp);
	}
	else {
		screenshot = tmp;
	}
#else /* HAVE_X11_EXTENSIONS_SHAPE_H */
	screenshot = gdk_pixbuf_get_from_drawable (NULL, window, NULL,
						   x, y, 0, 0,
						   width, height);
#endif /* HAVE_X11_EXTENSIONS_SHAPE_H */

	/* Add a drop_shadow, if needed */
	g_print ("add shadow\n");
	if (drop_shadow) {
		GdkPixbuf *old = screenshot;

		screenshot = create_shadowed_pixbuf (screenshot);
		g_object_unref (old);
	}
	return TRUE;
}
#endif
void
take_screen_shot (void)
{
	gint width, height;
	GdkPixbuf *screenshot;

	width = gdk_screen_width ();
	height = gdk_screen_height ();

	screenshot = gdk_pixbuf_get_from_drawable (NULL, gdk_get_default_root_window (),
						   NULL, 0, 0, 0, 0,
						   width, height);
}
