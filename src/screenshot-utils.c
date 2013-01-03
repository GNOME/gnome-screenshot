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

#include <config.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <canberra-gtk.h>
#include <stdlib.h>

#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
#include <X11/extensions/shape.h>
#endif

#include "cheese-flash.h"
#include "screenshot-application.h"
#include "screenshot-config.h"
#include "screenshot-utils.h"

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

static Window
find_wm_window (GdkWindow *window)
{
  Window xid, root, parent, *children;
  unsigned int nchildren;

  if (window == gdk_get_default_root_window ())
    return None;

  xid = GDK_WINDOW_XID (window);

  do
    {
      if (XQueryTree (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                      xid, &root, &parent, &children, &nchildren) == 0)
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

static cairo_region_t *
make_region_with_monitors (GdkScreen *screen)
{
  cairo_region_t *region;
  int num_monitors;
  int i;

  num_monitors = gdk_screen_get_n_monitors (screen);

  region = cairo_region_create ();

  for (i = 0; i < num_monitors; i++)
    {
      GdkRectangle rect;

      gdk_screen_get_monitor_geometry (screen, i, &rect);
      cairo_region_union_rectangle (region, &rect);
    }

  return region;
}

static void
blank_rectangle_in_pixbuf (GdkPixbuf *pixbuf, GdkRectangle *rect)
{
  int x, y;
  int x2, y2;
  guchar *pixels;
  int rowstride;
  int n_channels;
  guchar *row;
  gboolean has_alpha;

  g_assert (gdk_pixbuf_get_colorspace (pixbuf) == GDK_COLORSPACE_RGB);
  
  x2 = rect->x + rect->width;
  y2 = rect->y + rect->height;

  pixels = gdk_pixbuf_get_pixels (pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);
  n_channels = gdk_pixbuf_get_n_channels (pixbuf);

  for (y = rect->y; y < y2; y++)
    {
      guchar *p;

      row = pixels + y * rowstride;
      p = row + rect->x * n_channels;

      for (x = rect->x; x < x2; x++)
	{
	  *p++ = 0;
	  *p++ = 0;
	  *p++ = 0;

	  if (has_alpha)
	    *p++ = 255; /* opaque black */
	}
    }
}

static void
blank_region_in_pixbuf (GdkPixbuf *pixbuf, cairo_region_t *region)
{
  int n_rects;
  int i;
  int width, height;
  cairo_rectangle_int_t pixbuf_rect;

  n_rects = cairo_region_num_rectangles (region);

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);

  pixbuf_rect.x	     = 0;
  pixbuf_rect.y	     = 0;
  pixbuf_rect.width  = width;
  pixbuf_rect.height = height;

  for (i = 0; i < n_rects; i++)
    {
      cairo_rectangle_int_t rect, dest;

      cairo_region_get_rectangle (region, i, &rect);
      if (gdk_rectangle_intersect (&rect, &pixbuf_rect, &dest))
	blank_rectangle_in_pixbuf (pixbuf, &dest);
    }
}

/* When there are multiple monitors with different resolutions, the visible area
 * within the root window may not be rectangular (it may have an L-shape, for
 * example).  In that case, mask out the areas of the root window which would
 * not be visible in the monitors, so that screenshot do not end up with content
 * that the user won't ever see.
 */
static void
mask_monitors (GdkPixbuf *pixbuf, GdkWindow *root_window)
{
  GdkScreen *screen;
  cairo_region_t *region_with_monitors;
  cairo_region_t *invisible_region;
  cairo_rectangle_int_t rect;

  screen = gdk_window_get_screen (root_window);

  region_with_monitors = make_region_with_monitors (screen);

  rect.x = 0;
  rect.y = 0;
  rect.width = gdk_screen_get_width (screen);
  rect.height = gdk_screen_get_height (screen);

  invisible_region = cairo_region_create_rectangle (&rect);
  cairo_region_subtract (invisible_region, region_with_monitors);

  blank_region_in_pixbuf (pixbuf, invisible_region);

  cairo_region_destroy (region_with_monitors);
  cairo_region_destroy (invisible_region);
}

static void
screenshot_fallback_get_window_rect_coords (GdkWindow *window,
                                            gboolean include_border,
                                            GdkRectangle *real_coordinates_out,
                                            GdkRectangle *screenshot_coordinates_out)
{
  gint x_orig, y_orig;
  gint width, height;
  GdkRectangle real_coordinates;

  if (include_border)
    {
      gdk_window_get_frame_extents (window, &real_coordinates);
    }
  else
    {
      real_coordinates.width = gdk_window_get_width (window);
      real_coordinates.height = gdk_window_get_height (window);
      
      gdk_window_get_origin (window, &real_coordinates.x, &real_coordinates.y);
    }

  x_orig = real_coordinates.x;
  y_orig = real_coordinates.y;
  width  = real_coordinates.width;
  height = real_coordinates.height;

  if (real_coordinates_out != NULL)
    *real_coordinates_out = real_coordinates;

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

  if (screenshot_coordinates_out != NULL)
    {
      screenshot_coordinates_out->x = x_orig;
      screenshot_coordinates_out->y = y_orig;
      screenshot_coordinates_out->width = width;
      screenshot_coordinates_out->height = height;
    }
}

void
screenshot_play_sound_effect (const gchar *event_id,
                              const gchar *event_desc)
{
  ca_context *c;
  ca_proplist *p = NULL;
  int res;

  c = ca_gtk_context_get ();

  res = ca_proplist_create (&p);
  if (res < 0)
    goto done;

  res = ca_proplist_sets (p, CA_PROP_EVENT_ID, event_id);
  if (res < 0)
    goto done;

  res = ca_proplist_sets (p, CA_PROP_EVENT_DESCRIPTION, event_desc);
  if (res < 0)
    goto done;

  res = ca_proplist_sets (p, CA_PROP_CANBERRA_CACHE_CONTROL, "permanent");
  if (res < 0)
    goto done;

  ca_context_play_full (c, 0, p, NULL, NULL);

 done:
  if (p != NULL)
    ca_proplist_destroy (p);

}

static void
screenshot_fallback_fire_flash (GdkWindow *window,
                                GdkRectangle *rectangle)
{
  GdkRectangle rect;
  CheeseFlash *flash = NULL;

  if (rectangle != NULL)
    rect = *rectangle;
  else
    screenshot_fallback_get_window_rect_coords (window,
                                                screenshot_config->include_border,
                                                NULL,
                                                &rect);

  flash = cheese_flash_new ();
  cheese_flash_fire (flash, &rect);

  g_object_unref (flash);
}

GdkWindow *
do_find_current_window (void)
{
  GdkWindow *current_window;
  GdkDeviceManager *manager;
  GdkDevice *device;

  current_window = screenshot_find_active_window ();
  manager = gdk_display_get_device_manager (gdk_display_get_default ());
  device = gdk_device_manager_get_client_pointer (manager);
  
  /* If there's no active window, we fall back to returning the
   * window that the cursor is in.
   */
  if (!current_window)
    current_window = gdk_device_get_window_at_position (device, NULL, NULL);

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

static GdkWindow *
screenshot_fallback_find_current_window (void)
{
  GdkWindow *window = NULL;

  if (screenshot_config->take_window_shot)
    {
      window = do_find_current_window ();

      if (window == NULL)
        screenshot_config->take_window_shot = FALSE;
    }

  if (window == NULL)
    window = gdk_get_default_root_window ();

  return window;
}

static GdkPixbuf *
screenshot_fallback_get_pixbuf (GdkRectangle *rectangle)
{
  GdkWindow *root, *wm_window = NULL;
  GdkPixbuf *screenshot;
  GdkRectangle real_coords, screenshot_coords;
  Window wm;
  GtkBorder frame_offset = { 0, 0, 0, 0 };
  GdkWindow *window;

  window = screenshot_fallback_find_current_window ();

  screenshot_fallback_get_window_rect_coords (window, 
                                              screenshot_config->include_border,
                                              &real_coords,
                                              &screenshot_coords);

  wm = find_wm_window (window);
  if (wm != None)
    {
      GdkRectangle wm_real_coords;

      wm_window = gdk_x11_window_foreign_new_for_display 
        (gdk_window_get_display (window), wm);

      screenshot_fallback_get_window_rect_coords (wm_window,
                                                  FALSE,
                                                  &wm_real_coords,
                                                  NULL);

      frame_offset.left = (gdouble) (real_coords.x - wm_real_coords.x);
      frame_offset.top = (gdouble) (real_coords.y - wm_real_coords.y);
      frame_offset.right = (gdouble) (wm_real_coords.width - real_coords.width - frame_offset.left);
      frame_offset.bottom = (gdouble) (wm_real_coords.height - real_coords.height - frame_offset.top);
    }

  if (rectangle)
    {
      screenshot_coords.x = rectangle->x - screenshot_coords.x;
      screenshot_coords.y = rectangle->y - screenshot_coords.y;
      screenshot_coords.width  = rectangle->width;
      screenshot_coords.height = rectangle->height;
    }

  root = gdk_get_default_root_window ();
  screenshot = gdk_pixbuf_get_from_window (root,
                                           screenshot_coords.x, screenshot_coords.y,
                                           screenshot_coords.width, screenshot_coords.height);

  if (!screenshot_config->take_window_shot &&
      !screenshot_config->take_area_shot)
    mask_monitors (screenshot, root);

#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
  if (screenshot_config->include_border && (wm != None))
    {
      XRectangle *rectangles;
      GdkPixbuf *tmp;
      int rectangle_count, rectangle_order, i;

      /* we must use XShape to avoid showing what's under the rounder corners
       * of the WM decoration.
       */
      rectangles = XShapeGetRectangles (GDK_DISPLAY_XDISPLAY (gdk_display_get_default()),
                                        wm,
                                        ShapeBounding,
                                        &rectangle_count,
                                        &rectangle_order);
      if (rectangles && rectangle_count > 0)
        {
          gboolean has_alpha = gdk_pixbuf_get_has_alpha (screenshot);
          
          tmp = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
                                screenshot_coords.width, screenshot_coords.height);
          gdk_pixbuf_fill (tmp, 0);
          
          for (i = 0; i < rectangle_count; i++)
            {
              gint rec_x, rec_y;
              gint rec_width, rec_height;
              gint y;

              /* If we're using invisible borders, the ShapeBounding might not
               * have the same size as the frame extents, as it would include the
               * areas for the invisible borders themselves.
               * In that case, trim every rectangle we get by the offset between the
               * WM window size and the frame extents.
               */
              rec_x = rectangles[i].x;
              rec_y = rectangles[i].y;
              rec_width = rectangles[i].width - (frame_offset.left + frame_offset.right);
              rec_height = rectangles[i].height - (frame_offset.top + frame_offset.bottom);

              if (real_coords.x < 0)
                {
                  rec_x += real_coords.x;
                  rec_x = MAX(rec_x, 0);
                  rec_width += real_coords.x;
                }

              if (real_coords.y < 0)
                {
                  rec_y += real_coords.y;
                  rec_y = MAX(rec_y, 0);
                  rec_height += real_coords.y;
                }

              if (screenshot_coords.x + rec_x + rec_width > gdk_screen_width ())
                rec_width = gdk_screen_width () - screenshot_coords.x - rec_x;

              if (screenshot_coords.y + rec_y + rec_height > gdk_screen_height ())
                rec_height = gdk_screen_height () - screenshot_coords.y - rec_y;

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

          XFree (rectangles);
        }
    }
#endif /* HAVE_X11_EXTENSIONS_SHAPE_H */

  /* if we have a selected area, there were by definition no cursor in the
   * screenshot */
  if (screenshot_config->include_pointer && !rectangle) 
    {
      GdkCursor *cursor;
      GdkPixbuf *cursor_pixbuf;

      cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_LEFT_PTR);
      cursor_pixbuf = gdk_cursor_get_image (cursor);

      if (cursor_pixbuf != NULL) 
        {
          GdkDeviceManager *manager;
          GdkDevice *device;
          GdkRectangle rect;
          gint cx, cy, xhot, yhot;

          manager = gdk_display_get_device_manager (gdk_display_get_default ());
          device = gdk_device_manager_get_client_pointer (manager);

          if (wm_window != NULL)
            gdk_window_get_device_position (wm_window, device,
                                            &cx, &cy, NULL);
          else
            gdk_window_get_device_position (window, device,
                                            &cx, &cy, NULL);

          sscanf (gdk_pixbuf_get_option (cursor_pixbuf, "x_hot"), "%d", &xhot);
          sscanf (gdk_pixbuf_get_option (cursor_pixbuf, "y_hot"), "%d", &yhot);

          /* in rect we have the cursor window coordinates */
          rect.x = cx + real_coords.x;
          rect.y = cy + real_coords.y;
          rect.width = gdk_pixbuf_get_width (cursor_pixbuf);
          rect.height = gdk_pixbuf_get_height (cursor_pixbuf);

          /* see if the pointer is inside the window */
          if (gdk_rectangle_intersect (&real_coords, &rect, &rect)) 
            {
              gint cursor_x, cursor_y;

              cursor_x = cx - xhot - frame_offset.left;
              cursor_y = cy - yhot - frame_offset.top;
              gdk_pixbuf_composite (cursor_pixbuf, screenshot,
                                    cursor_x, cursor_y,
                                    rect.width, rect.height,
                                    cursor_x, cursor_y,
                                    1.0, 1.0, 
                                    GDK_INTERP_BILINEAR,
                                    255);
            }

          g_object_unref (cursor_pixbuf);
          g_object_unref (cursor);
        }
    }

  screenshot_fallback_fire_flash (window, rectangle);

  return screenshot;
}

GdkPixbuf *
screenshot_get_pixbuf (GdkRectangle *rectangle)
{
  GdkPixbuf *screenshot = NULL;
  gchar *path, *filename, *tmpname;
  const gchar *method_name;
  GVariant *method_params;
  GError *error = NULL;
  GDBusConnection *connection;

  path = g_build_filename (g_get_user_cache_dir (), "gnome-screenshot", NULL);
  g_mkdir_with_parents (path, 0700);

  tmpname = g_strdup_printf ("scr-%d.png", g_random_int ());
  filename = g_build_filename (path, tmpname, NULL);

  if (screenshot_config->take_window_shot)
    {
      method_name = "ScreenshotWindow";
      method_params = g_variant_new ("(bbbs)",
                                     screenshot_config->include_border,
                                     screenshot_config->include_pointer,
                                     TRUE, /* flash */
                                     filename);
    }
  else if (rectangle != NULL)
    {
      method_name = "ScreenshotArea";
      method_params = g_variant_new ("(iiiibs)",
                                     rectangle->x, rectangle->y,
                                     rectangle->width, rectangle->height,
                                     TRUE, /* flash */
                                     filename);
    }
  else
    {
      method_name = "Screenshot";
      method_params = g_variant_new ("(bbs)",
                                     screenshot_config->include_pointer,
                                     TRUE, /* flash */
                                     filename);
    }

  connection = g_application_get_dbus_connection (g_application_get_default ());
  g_dbus_connection_call_sync (connection,
                               "org.gnome.Shell.Screenshot",
                               "/org/gnome/Shell/Screenshot",
                               "org.gnome.Shell.Screenshot",
                               method_name,
                               method_params,
                               NULL,
                               G_DBUS_CALL_FLAGS_NONE,
                               -1,
                               NULL,
                               &error);

  if (error == NULL)
    {
      screenshot = gdk_pixbuf_new_from_file (filename, &error);

      /* remove the temporary file created by the shell */
      g_unlink (filename);
    }

  if (error != NULL)
    {
      g_message ("Unable to use GNOME Shell's builtin screenshot interface, "
                 "resorting to fallback X11.");
      g_error_free (error);

      screenshot = screenshot_fallback_get_pixbuf (rectangle);
    }

  g_free (path);
  g_free (tmpname);
  g_free (filename);

  return screenshot;
}

gint
screenshot_show_dialog (GtkWindow   *parent,
                        GtkMessageType message_type,
                        GtkButtonsType buttons_type,
                        const gchar *message,
                        const gchar *detail)
{
  GtkWidget *dialog;
  GtkWindowGroup *group;
  gint response;

  g_return_val_if_fail ((parent == NULL) || (GTK_IS_WINDOW (parent)),
                        GTK_RESPONSE_NONE);
  g_return_val_if_fail (message != NULL, GTK_RESPONSE_NONE);
  
  dialog = gtk_message_dialog_new (parent,
  				   GTK_DIALOG_DESTROY_WITH_PARENT,
  				   message_type,
  				   buttons_type,
  				   "%s", message);
  gtk_window_set_title (GTK_WINDOW (dialog), "");
  
  if (detail)
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
  					      "%s", detail);

  if (parent)
    {
      group = gtk_window_get_group (parent);
      if (group != NULL)
        gtk_window_group_add_window (group, GTK_WINDOW (dialog));
    }

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  
  gtk_widget_destroy (dialog);

  return response;
}

void
screenshot_display_help (GtkWindow *parent)
{
  GError *error = NULL;

  gtk_show_uri (gtk_window_get_screen (parent),
		"help:gnome-help/screen-shot-record",
		gtk_get_current_event_time (), &error);

  if (error)
    {
      screenshot_show_dialog (parent, 
                              GTK_MESSAGE_ERROR,
                              GTK_BUTTONS_OK,
                              _("Error loading the help page"), 
                              error->message);
      g_error_free (error);
    }
}
