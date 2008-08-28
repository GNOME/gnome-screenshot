/* screenshot-utils.c - common functions for GNOME Screenshot
 *
 * Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2008 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 */

#include "config.h"
#include "screenshot-utils.h"

#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>

#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
#include <X11/extensions/shape.h>
#endif

static GtkWidget *selection_window;

#define SELECTION_NAME "_GNOME_PANEL_SCREENSHOT"

static char *
get_utf8_property (GdkWindow *window,
		   GdkAtom    atom)
{
  gboolean res;
  GdkAtom utf8_string;
  GdkAtom type;
  int actual_format, actual_length;
  guchar *data;
  char *retval;
  
  utf8_string = gdk_x11_xatom_to_atom (gdk_x11_get_xatom_by_name ("UTF8_STRING"));
  res = gdk_property_get (window, atom, utf8_string,
                          0, G_MAXLONG, FALSE,
                          &type,
                          &actual_format, &actual_length,
                          &data);
  if (!res)
    return NULL;

  if (type != utf8_string || actual_format != 8 || actual_length == 0)
    {
      g_free (data);
      return NULL;
    }

  if (!g_utf8_validate ((gchar *) data, actual_length, NULL))
    {
      char *atom_name = gdk_atom_name (atom);

      g_warning ("Property `%s' (format: %d, length: %d) contained "
                 "invalid UTF-8",
                 atom_name,
                 actual_format,
                 actual_length);

      g_free (atom_name);
      g_free (data);

      return NULL;
    }
  
  retval = g_strndup ((gchar *) data, actual_length);

  g_free (data);
  
  return retval;
}

/* To make sure there is only one screenshot taken at a time,
 * (Imagine key repeat for the print screen key) we hold a selection
 * until we are done taking the screenshot
 */
gboolean
screenshot_grab_lock (void)
{
  GdkAtom selection_atom;
  GdkCursor *cursor;
  gboolean result = FALSE;

  selection_atom = gdk_atom_intern (SELECTION_NAME, FALSE);
  gdk_x11_grab_server ();

  if (gdk_selection_owner_get (selection_atom) != NULL)
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

  result = TRUE;

 out:
  gdk_x11_ungrab_server ();
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

static GdkWindow *
screenshot_find_active_window (void)
{
  GdkWindow *window;
  GdkScreen *default_screen;

  default_screen = gdk_screen_get_default ();
  window = gdk_screen_get_active_window (default_screen);

  return window;
}

static gboolean
screenshot_window_is_desktop (GdkWindow *window)
{
  GdkWindow *root_window = gdk_get_default_root_window ();
  GdkWindowTypeHint window_type_hint;

  if (window == root_window)
    return TRUE;

  window_type_hint = gdk_window_get_type_hint (window);
  if (window_type_hint == GDK_WINDOW_TYPE_HINT_DESKTOP)
    return TRUE;

  return FALSE;
      
}

#define MAXIMUM_WM_REPARENTING_DEPTH 4

static GdkWindow *
look_for_hint_helper (GdkWindow *window,
		      GdkAtom    property,
		      int       depth)
{
  gboolean res;
  GdkAtom actual_type;
  int actual_format, actual_length;
  guchar *data;
  
  res = gdk_property_get (window, property, GDK_NONE,
                          0, 1, FALSE,
                          &actual_type,
                          &actual_format, &actual_length,
                          &data);

  if (res == TRUE &&
      data != NULL &&
      actual_format == 32 &&
      data[0] == 1)
    {
      g_free (data);

      return window;
    }

  if (depth < MAXIMUM_WM_REPARENTING_DEPTH)
    {
      GList *children, *l;

      children = gdk_window_get_children (window);
      if (children != NULL)
        {
          for (l = children; l; l = l->next)
            {
              window = look_for_hint_helper (l->data, property, depth + 1);
              if (window)
                break;
            }

          g_list_free (children);

          if (window)
            return window;
        }
    }

  return NULL;
}

static GdkWindow *
look_for_hint (GdkWindow *window,
	       GdkAtom property)
{
  GdkWindow *retval;

  retval = look_for_hint_helper (window, property, 0);

  return retval;
}

GdkWindow *
screenshot_find_current_window ()
{
  GdkWindow *current_window;

  current_window = screenshot_find_active_window ();
  
  /* If there's no active window, we fall back to returning the
   * window that the cursor is in.
   */
  if (!current_window)
    current_window = gdk_window_at_pointer (NULL, NULL);

  if (current_window)
    {
      if (screenshot_window_is_desktop (current_window))
	/* if the current window is the desktop (e.g. nautilus), we
	 * return NULL, as getting the whole screen makes more sense.
         */
        return NULL;

      /* Once we have a window, we take the toplevel ancestor. */
      current_window = gdk_window_get_toplevel (current_window);
    }

  return current_window;
}

static Window
find_wm_window (Window xid)
{
  Window root, parent, *children;
  unsigned int nchildren;

  do
    {
      if (XQueryTree (GDK_DISPLAY (), xid, &root,
		      &parent, &children, &nchildren) == 0)
	{
	  g_warning ("Couldn't find window manager window");
	  return None;
	}

      if (root == parent)
	return xid;

      xid = parent;
    }
  while (TRUE);
}


GdkPixbuf *
screenshot_get_pixbuf (GdkWindow *window,
                       gboolean   include_pointer,
                       gboolean   include_border)
{
  GdkWindow *root;
  GdkPixbuf *screenshot;
  gint x_real_orig, y_real_orig, x_orig, y_orig;
  gint width, real_width, height, real_height;

  /* If the screenshot should include the border, we look for the WM window. */

  if (include_border)
    {
      Window xid, wm;

      xid = GDK_WINDOW_XWINDOW (window);
      wm = find_wm_window (xid);

      if (wm != None)
        window = gdk_window_foreign_new (wm);

      /* fallback to no border if we can't find the WM window. */
    }

  root = gdk_get_default_root_window ();

  gdk_drawable_get_size (window, &real_width, &real_height);      
  gdk_window_get_origin (window, &x_real_orig, &y_real_orig);

  x_orig = x_real_orig;
  y_orig = y_real_orig;
  width  = real_width;
  height = real_height;

  if (x_orig < 0)
    {
      width = width + x_orig;
      x_orig = 0;
    }

  if (y_orig < 0)
    {
      height = height + y_orig;
      y_orig = 0;
    }

  if (x_orig + width > gdk_screen_width ())
    width = gdk_screen_width () - x_orig;

  if (y_orig + height > gdk_screen_height ())
    height = gdk_screen_height () - y_orig;
  
  screenshot = gdk_pixbuf_get_from_drawable (NULL, root, NULL,
                                             x_orig, y_orig, 0, 0,
                                             width, height);

#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
  if (include_border)
    {
      XRectangle *rectangles;
      GdkPixbuf *tmp;
      int rectangle_count, rectangle_order, i;

      /* we must use XShape to avoid showing what's under the rounder corners
       * of the WM decoration.
       */

      rectangles = XShapeGetRectangles (GDK_DISPLAY (),
                                        GDK_WINDOW_XWINDOW (window),
                                        ShapeBounding,
                                        &rectangle_count,
                                        &rectangle_order);
      if (rectangles && rectangle_count > 0)
        {
          gboolean has_alpha = gdk_pixbuf_get_has_alpha (screenshot);
          
          tmp = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, width, height);
          gdk_pixbuf_fill (tmp, 0);
          
          for (i = 0; i < rectangle_count; i++)
            {
              gint rec_x, rec_y;
              gint rec_width, rec_height;
              gint y;

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
                  gint x;

                  src_pixels = gdk_pixbuf_get_pixels (screenshot)
                             + y * gdk_pixbuf_get_rowstride(screenshot)
                             + rec_x * (has_alpha ? 4 : 3);
                  dest_pixels = gdk_pixbuf_get_pixels (tmp)
                              + y * gdk_pixbuf_get_rowstride (tmp)
                              + rec_x * 4;

                  for (x = 0; x < rec_width; x++)
                    {
                      *dest_pixels++ = *src_pixels++;
                      *dest_pixels++ = *src_pixels++;
                      *dest_pixels++ = *src_pixels++;

                      if (has_alpha)
                        *dest_pixels++ = *src_pixels++;
                      else
                        *dest_pixels++ = 255;
                    }
                }
            }

          g_object_unref (screenshot);
          screenshot = tmp;
        }
    }
#endif /* HAVE_X11_EXTENSIONS_SHAPE_H */

  if (include_pointer) 
    {
      GdkCursor *cursor;
      GdkPixbuf *cursor_pixbuf;

      cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_LEFT_PTR);
      cursor_pixbuf = gdk_cursor_get_image (cursor);

      if (cursor_pixbuf != NULL) 
        {
          GdkRectangle r1, r2;
          gint cx, cy;

          gdk_window_get_pointer (window, &cx, &cy, NULL);

          r1.x = x_real_orig;
          r1.y = y_real_orig;
          r1.width = real_width;
          r1.height = real_height;

          r2.x = cx;
          r2.y = cy;
          r2.width = gdk_pixbuf_get_width (cursor_pixbuf);
          r2.height = gdk_pixbuf_get_height (cursor_pixbuf);

          if (gdk_rectangle_intersect (&r1, &r2, &r2)) 
            { 
              gdk_pixbuf_composite (cursor_pixbuf, screenshot,
                                    r2.x - x_real_orig, r2.y - y_real_orig, 
                                    r2.width, r2.height,
                                    cx - x_real_orig, cy - y_real_orig, 
                                    1.0, 1.0, 
                                    GDK_INTERP_BILINEAR,
                                    255);
            }

          g_object_unref (cursor_pixbuf);
          gdk_cursor_unref (cursor);
        }
    }

  return screenshot;
}

gchar *
screenshot_get_window_title (GdkWindow *win)
{
  gchar *name;

  win = gdk_window_get_toplevel (win);
  win = look_for_hint (win, gdk_x11_xatom_to_atom (gdk_x11_get_xatom_by_name ("WM_STATE")));

  name = get_utf8_property (win, gdk_x11_xatom_to_atom (gdk_x11_get_xatom_by_name ("_NET_WM_NAME")));
  if (name)
    return name;

  /* TODO: maybe we should also look at WM_NAME and WM_CLASS? */

  return g_strdup (_("Untitled Window"));
}

void
screenshot_show_error_dialog (GtkWindow   *parent,
                              const gchar *message,
                              const gchar *detail)
{
  GtkWidget *dialog;
  
  g_return_if_fail ((parent == NULL) || (GTK_IS_WINDOW (parent)));
  g_return_if_fail (message != NULL);
  
  dialog = gtk_message_dialog_new (parent,
  				   GTK_DIALOG_DESTROY_WITH_PARENT,
  				   GTK_MESSAGE_ERROR,
  				   GTK_BUTTONS_OK,
  				   "%s", message);
  gtk_window_set_title (GTK_WINDOW (dialog), "");
  
  if (detail)
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
  					      "%s", detail);
  
  if (parent && parent->group)
    gtk_window_group_add_window (parent->group, GTK_WINDOW (dialog));
  
  gtk_dialog_run (GTK_DIALOG (dialog));
  
  gtk_widget_destroy (dialog);
}

void
screenshot_show_gerror_dialog (GtkWindow   *parent,
                               const gchar *message,
                               GError      *error)
{
  g_return_if_fail (parent == NULL || GTK_IS_WINDOW (parent));
  g_return_if_fail (message != NULL);
  g_return_if_fail (error != NULL);

  screenshot_show_error_dialog (parent, message, error->message);
  g_clear_error (&error);
}
