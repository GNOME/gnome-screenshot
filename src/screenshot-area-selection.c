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

      g_message ("Unable to select area.");

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
