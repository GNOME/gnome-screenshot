/* screenshot-backend-shell.c - GNOME Shell backend
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

#include "screenshot-backend-shell.h"

#include "screenshot-config.h"

#include <glib/gstdio.h>

struct _ScreenshotBackendShell
{
  GObject parent_instance;
};

static void screenshot_backend_shell_backend_init (ScreenshotBackendInterface *iface);

G_DEFINE_TYPE_WITH_CODE (ScreenshotBackendShell, screenshot_backend_shell, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SCREENSHOT_TYPE_BACKEND, screenshot_backend_shell_backend_init))

static GdkPixbuf *
screenshot_backend_shell_get_pixbuf (ScreenshotBackend *backend,
                                     GdkRectangle      *rectangle)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *path = NULL, *filename = NULL, *tmpname = NULL;
  GdkPixbuf *screenshot = NULL;
  const gchar *method_name;
  GVariant *method_params;
  GDBusConnection *connection;

  path = g_build_filename (g_get_user_cache_dir (), "gnome-screenshot", NULL);
  g_mkdir_with_parents (path, 0700);

  tmpname = g_strdup_printf ("scr-%d.png", g_random_int ());
  filename = g_build_filename (path, tmpname, NULL);

  if (screenshot_config->take_window_shot)
    {
      method_name = "ScreenshotWindow";
      method_params = g_variant_new ("(bbbs)",
                                     TRUE,
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

  return screenshot;
}

static void
screenshot_backend_shell_class_init (ScreenshotBackendShellClass *klass)
{
}

static void
screenshot_backend_shell_init (ScreenshotBackendShell *self)
{
}

static void
screenshot_backend_shell_backend_init (ScreenshotBackendInterface *iface)
{
  iface->get_pixbuf = screenshot_backend_shell_get_pixbuf;
}

ScreenshotBackend *
screenshot_backend_shell_new (void)
{
  return g_object_new (SCREENSHOT_TYPE_BACKEND_SHELL, NULL);
}
