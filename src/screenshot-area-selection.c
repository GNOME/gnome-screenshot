/* screenshot-area-selection.c - interactive screenshot area selection
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 */

#include "config.h"

#include <gtk/gtk.h>

#include "screenshot-area-selection.h"

typedef struct {
  GdkRectangle  rect;
  gboolean      button_pressed;
  GtkWidget    *window;

  gboolean      aborted;
} select_area_filter_data;

static gboolean
select_area_button_press (GtkWidget               *window,
                          GdkEventButton          *event,
                          select_area_filter_data *data)
{
  if (data->button_pressed)
    return TRUE;

  data->button_pressed = TRUE;
  data->rect.x = event->x_root;
  data->rect.y = event->y_root;

  return TRUE;
}

static gboolean
select_area_motion_notify (GtkWidget               *window,
                           GdkEventMotion          *event,
                           select_area_filter_data *data)
{
  GdkRectangle draw_rect;

  if (!data->button_pressed)
    return TRUE;

  draw_rect.width = ABS (data->rect.x - event->x_root);
  draw_rect.height = ABS (data->rect.y - event->y_root);
  draw_rect.x = MIN (data->rect.x, event->x_root);
  draw_rect.y = MIN (data->rect.y, event->y_root);

  if (draw_rect.width <= 0 || draw_rect.height <= 0)
    {
      gtk_window_move (GTK_WINDOW (window), -100, -100);
      gtk_window_resize (GTK_WINDOW (window), 10, 10);
      return TRUE;
    }

  gtk_window_move (GTK_WINDOW (window), draw_rect.x, draw_rect.y);
  gtk_window_resize (GTK_WINDOW (window), draw_rect.width, draw_rect.height);

  /* We (ab)use app-paintable to indicate if we have an RGBA window */
  if (!gtk_widget_get_app_paintable (window))
    {
      GdkWindow *gdkwindow = gtk_widget_get_window (window);

      /* Shape the window to make only the outline visible */
      if (draw_rect.width > 2 && draw_rect.height > 2)
        {
          cairo_region_t *region;
          cairo_rectangle_int_t region_rect = {
            0, 0,
            draw_rect.width, draw_rect.height
          };

          region = cairo_region_create_rectangle (&region_rect);
          region_rect.x++;
          region_rect.y++;
          region_rect.width -= 2;
          region_rect.height -= 2;
          cairo_region_subtract_rectangle (region, &region_rect);

          gdk_window_shape_combine_region (gdkwindow, region, 0, 0);

          cairo_region_destroy (region);
        }
      else
        gdk_window_shape_combine_region (gdkwindow, NULL, 0, 0);
    }

  return TRUE;
}

static gboolean
select_area_button_release (GtkWidget               *window,
                            GdkEventButton          *event,
                            select_area_filter_data *data)
{
  if (!data->button_pressed)
    return TRUE;

  data->rect.width  = ABS (data->rect.x - event->x_root);
  data->rect.height = ABS (data->rect.y - event->y_root);
  data->rect.x = MIN (data->rect.x, event->x_root);
  data->rect.y = MIN (data->rect.y, event->y_root);

  if (data->rect.width == 0 || data->rect.height == 0)
    data->aborted = TRUE;

  gtk_main_quit ();

  return TRUE;
}

static gboolean
select_area_key_press (GtkWidget               *window,
                       GdkEventKey             *event,
                       select_area_filter_data *data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      data->rect.x = 0;
      data->rect.y = 0;
      data->rect.width  = 0;
      data->rect.height = 0;
      data->aborted = TRUE;

      gtk_main_quit ();
    }

  return TRUE;
}

static gboolean
select_window_draw (GtkWidget *window, cairo_t *cr, gpointer unused)
{
  GtkStyleContext *style;

  style = gtk_widget_get_style_context (window);

  if (gtk_widget_get_app_paintable (window))
    {
      cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
      cairo_set_source_rgba (cr, 0, 0, 0, 0);
      cairo_paint (cr);

      gtk_style_context_save (style);
      gtk_style_context_add_class (style, GTK_STYLE_CLASS_RUBBERBAND);

      gtk_render_background (style, cr,
                             0, 0,
                             gtk_widget_get_allocated_width (window),
                             gtk_widget_get_allocated_height (window));
      gtk_render_frame (style, cr,
                        0, 0,
                        gtk_widget_get_allocated_width (window),
                        gtk_widget_get_allocated_height (window));

      gtk_style_context_restore (style);
    }

  return TRUE;
}

static GtkWidget *
create_select_window (void)
{
  GtkWidget *window;
  GdkScreen *screen;
  GdkVisual *visual;

  screen = gdk_screen_get_default ();
  visual = gdk_screen_get_rgba_visual (screen);

  window = gtk_window_new (GTK_WINDOW_POPUP);
  if (gdk_screen_is_composited (screen) && visual)
    {
      gtk_widget_set_visual (window, visual);
      gtk_widget_set_app_paintable (window, TRUE);
    }

  g_signal_connect (window, "draw", G_CALLBACK (select_window_draw), NULL);

  gtk_window_move (GTK_WINDOW (window), -100, -100);
  gtk_window_resize (GTK_WINDOW (window), 10, 10);
  gtk_widget_show (window);

  return window;
}

typedef struct {
  GdkRectangle rectangle;
  SelectAreaCallback callback;
  gpointer callback_data;
  gboolean aborted;
} CallbackData;

static gboolean
emit_select_callback_in_idle (gpointer user_data)
{
  CallbackData *data = user_data;

  if (!data->aborted)
    data->callback (&data->rectangle, data->callback_data);
  else
    data->callback (NULL, data->callback_data);

  g_slice_free (CallbackData, data);

  return FALSE;
}

static void
screenshot_select_area_x11_async (CallbackData *cb_data)
{
  g_autoptr(GdkCursor) cursor = NULL;
  GdkDisplay *display;
  select_area_filter_data  data;
  GdkSeat *seat;

  data.rect.x = 0;
  data.rect.y = 0;
  data.rect.width  = 0;
  data.rect.height = 0;
  data.button_pressed = FALSE;
  data.aborted = FALSE;
  data.window = create_select_window();

  g_signal_connect (data.window, "key-press-event", G_CALLBACK (select_area_key_press), &data);
  g_signal_connect (data.window, "button-press-event", G_CALLBACK (select_area_button_press), &data);
  g_signal_connect (data.window, "button-release-event", G_CALLBACK (select_area_button_release), &data);
  g_signal_connect (data.window, "motion-notify-event", G_CALLBACK (select_area_motion_notify), &data);

  display = gtk_widget_get_display (data.window);
  cursor = gdk_cursor_new_for_display (display, GDK_CROSSHAIR);
  seat = gdk_display_get_default_seat (display);

  gdk_seat_grab (seat,
                 gtk_widget_get_window (data.window),
                 GDK_SEAT_CAPABILITY_ALL,
                 FALSE,
                 cursor,
                 NULL,
                 NULL,
                 NULL);

  gtk_main ();

  gdk_seat_ungrab (seat);

  gtk_widget_destroy (data.window);

  cb_data->aborted = data.aborted;
  cb_data->rectangle = data.rect;

  /* FIXME: we should actually be emitting the callback When
   * the compositor has finished re-drawing, but there seems to be no easy
   * way to know that.
   */
  g_timeout_add (200, emit_select_callback_in_idle, cb_data);
}

static void
select_area_done (GObject *source_object,
                  GAsyncResult *res,
                  gpointer user_data)
{
  CallbackData *cb_data = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) ret = NULL;

  ret = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), res, &error);
  if (error != NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          cb_data->aborted = TRUE;
          g_idle_add (emit_select_callback_in_idle, cb_data);
          return;
        }

      g_message ("Unable to select area using GNOME Shell's builtin screenshot "
                 "interface, resorting to fallback X11.");

      screenshot_select_area_x11_async (cb_data);
      return;
    }

  g_variant_get (ret, "(iiii)",
                 &cb_data->rectangle.x,
                 &cb_data->rectangle.y,
                 &cb_data->rectangle.width,
                 &cb_data->rectangle.height);

  g_idle_add (emit_select_callback_in_idle, cb_data);
}

void
screenshot_select_area_async (SelectAreaCallback callback,
                              gpointer callback_data)
{
  CallbackData *cb_data;
  GDBusConnection *connection;

  cb_data = g_slice_new0 (CallbackData);
  cb_data->callback = callback;
  cb_data->callback_data = callback_data;

  connection = g_application_get_dbus_connection (g_application_get_default ());
  g_dbus_connection_call (connection,
                          "org.gnome.Shell.Screenshot",
                          "/org/gnome/Shell/Screenshot",
                          "org.gnome.Shell.Screenshot",
                          "SelectArea",
                          NULL,
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          G_MAXINT,
                          NULL,
                          select_area_done,
                          cb_data);
}
