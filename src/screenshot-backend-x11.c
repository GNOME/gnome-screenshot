/* screenshot-backend-x11.c - Fallback X11 backend
 *
 * Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2008 Cosimo Cecchi <cosimoc@gnome.org>
 * Copyright (C) 2020 Alexander Mikhaylenko <alexm@gnome.org>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 */

#include "config.h"

#ifdef HAVE_X11

#include "screenshot-backend-x11.h"

#include "screenshot-config.h"

#include "cheese-flash.h"

#include <gdk/gdkx.h>

#ifdef HAVE_X11_EXTENSIONS_SHAPE_H
#include <X11/extensions/shape.h>
#endif

struct _ScreenshotBackendX11
{
  GObject parent_instance;
};

static void screenshot_backend_x11_backend_init (ScreenshotBackendInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ScreenshotBackendX11, screenshot_backend_x11, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SCREENSHOT_TYPE_BACKEND, screenshot_backend_x11_backend_init))

static GdkWindow *
screenshot_find_active_window (void)
{
  GdkWindow *window;
  GdkScreen *default_screen;

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  default_screen = gdk_screen_get_default ();
  window = gdk_screen_get_active_window (default_screen);
G_GNUC_END_IGNORE_DEPRECATIONS

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
make_region_with_monitors (GdkDisplay *display)
{
  cairo_region_t *region;
  int num_monitors;
  int i;

  num_monitors = gdk_display_get_n_monitors (display);

  region = cairo_region_create ();

  for (i = 0; i < num_monitors; i++)
    {
      GdkMonitor *monitor;
      GdkRectangle rect;

      monitor = gdk_display_get_monitor (display, i);
      gdk_monitor_get_geometry (monitor, &rect);
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

  pixbuf_rect.x      = 0;
  pixbuf_rect.y      = 0;
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
mask_monitors (GdkPixbuf *pixbuf,
               GdkWindow *root_window)
{
  GdkDisplay *display;
  cairo_region_t *region_with_monitors;
  cairo_region_t *invisible_region;
  cairo_rectangle_int_t rect;

  display = gdk_window_get_display (root_window);

  region_with_monitors = make_region_with_monitors (display);

  rect.x = 0;
  rect.y = 0;
  rect.width = gdk_pixbuf_get_width (pixbuf);
  rect.height = gdk_pixbuf_get_height (pixbuf);

  invisible_region = cairo_region_create_rectangle (&rect);
  cairo_region_subtract (invisible_region, region_with_monitors);

  blank_region_in_pixbuf (pixbuf, invisible_region);

  cairo_region_destroy (region_with_monitors);
  cairo_region_destroy (invisible_region);
}

static void
screenshot_fallback_get_window_rect_coords (GdkWindow    *window,
                                            GdkRectangle *real_coordinates_out,
                                            GdkRectangle *screenshot_coordinates_out)
{
  gint x_orig, y_orig;
  gint width, height;
  GdkRectangle real_coordinates;

  gdk_window_get_frame_extents (window, &real_coordinates);

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

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  if (x_orig + width > gdk_screen_width ())
    width = gdk_screen_width () - x_orig;

  if (y_orig + height > gdk_screen_height ())
    height = gdk_screen_height () - y_orig;
G_GNUC_END_IGNORE_DEPRECATIONS

  if (screenshot_coordinates_out != NULL)
    {
      screenshot_coordinates_out->x = x_orig;
      screenshot_coordinates_out->y = y_orig;
      screenshot_coordinates_out->width = width;
      screenshot_coordinates_out->height = height;
    }
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
    screenshot_fallback_get_window_rect_coords (window, NULL, &rect);

  flash = cheese_flash_new ();
  cheese_flash_fire (flash, &rect);
}

GdkWindow *
do_find_current_window (void)
{
  GdkWindow *current_window;
  GdkDevice *device;
  GdkSeat *seat;

  current_window = screenshot_find_active_window ();
  seat = gdk_display_get_default_seat (gdk_display_get_default ());
  device = gdk_seat_get_pointer (seat);

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
screenshot_backend_x11_get_pixbuf (ScreenshotBackend *backend,
                                   GdkRectangle      *rectangle)
{
  GdkWindow *root, *wm_window = NULL;
  GdkPixbuf *screenshot = NULL;
  GdkRectangle real_coords, screenshot_coords;
  Window wm;
  GtkBorder frame_offset = { 0, 0, 0, 0 };
  GdkWindow *window;

  window = screenshot_fallback_find_current_window ();

  screenshot_fallback_get_window_rect_coords (window,
                                              &real_coords,
                                              &screenshot_coords);

  wm = find_wm_window (window);
  if (wm != None)
    {
      GdkRectangle wm_real_coords;

      wm_window = gdk_x11_window_foreign_new_for_display
        (gdk_window_get_display (window), wm);

      screenshot_fallback_get_window_rect_coords (wm_window,
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
  if (wm != None)
    {
      XRectangle *rectangles;
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
          int scale_factor = gdk_window_get_scale_factor (wm_window);
          gboolean has_alpha = gdk_pixbuf_get_has_alpha (screenshot);
          GdkPixbuf *tmp = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
                                           gdk_pixbuf_get_width (screenshot),
                                           gdk_pixbuf_get_height (screenshot));
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
               *
               * Note that the XShape values are in actual pixels, whereas the GDK
               * ones are in display pixels (i.e. scaled), so we need to apply the
               * scale factor to the former to use display pixels for all our math.
               */
              rec_x = rectangles[i].x / scale_factor;
              rec_y = rectangles[i].y / scale_factor;
              rec_width = rectangles[i].width / scale_factor - (frame_offset.left + frame_offset.right);
              rec_height = rectangles[i].height / scale_factor - (frame_offset.top + frame_offset.bottom);

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

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
              if (screenshot_coords.x + rec_x + rec_width > gdk_screen_width ())
                rec_width = gdk_screen_width () - screenshot_coords.x - rec_x;

              if (screenshot_coords.y + rec_y + rec_height > gdk_screen_height ())
                rec_height = gdk_screen_height () - screenshot_coords.y - rec_y;
G_GNUC_END_IGNORE_DEPRECATIONS

              /* Undo the scale factor in order to copy the pixbuf data pixel-wise */
              for (y = rec_y * scale_factor; y < (rec_y + rec_height) * scale_factor; y++)
                {
                  guchar *src_pixels, *dest_pixels;
                  gint x;

                  src_pixels = gdk_pixbuf_get_pixels (screenshot)
                             + y * gdk_pixbuf_get_rowstride(screenshot)
                             + rec_x * scale_factor * (has_alpha ? 4 : 3);
                  dest_pixels = gdk_pixbuf_get_pixels (tmp)
                              + y * gdk_pixbuf_get_rowstride (tmp)
                              + rec_x * scale_factor * 4;

                  for (x = 0; x < rec_width * scale_factor; x++)
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

          g_set_object (&screenshot, tmp);

          XFree (rectangles);
        }
    }
#endif /* HAVE_X11_EXTENSIONS_SHAPE_H */

  /* if we have a selected area, there were by definition no cursor in the
   * screenshot */
  if (screenshot_config->include_pointer && !rectangle)
    {
      g_autoptr(GdkCursor) cursor = NULL;
      g_autoptr(GdkPixbuf) cursor_pixbuf = NULL;

      cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_LEFT_PTR);
      cursor_pixbuf = gdk_cursor_get_image (cursor);

      if (cursor_pixbuf != NULL)
        {
          GdkSeat *seat;
          GdkDevice *device;
          GdkRectangle rect;
          gint cx, cy, xhot, yhot;

          seat = gdk_display_get_default_seat (gdk_display_get_default ());
          device = gdk_seat_get_pointer (seat);

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
        }
    }

  screenshot_fallback_fire_flash (window, rectangle);

  return screenshot;
}

static void
screenshot_backend_x11_class_init (ScreenshotBackendX11Class *klass)
{
}

static void
screenshot_backend_x11_init (ScreenshotBackendX11 *self)
{
}

static void
screenshot_backend_x11_backend_init (ScreenshotBackendInterface *iface)
{
  iface->get_pixbuf = screenshot_backend_x11_get_pixbuf;
}

ScreenshotBackend *
screenshot_backend_x11_new (void)
{
  return g_object_new (SCREENSHOT_TYPE_BACKEND_X11, NULL);
}

#endif /* HAVE_X11 */
