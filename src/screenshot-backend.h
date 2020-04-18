/* screenshot-backend.h - Backend interface
 *
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

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SCREENSHOT_TYPE_BACKEND (screenshot_backend_get_type ())

G_DECLARE_INTERFACE (ScreenshotBackend, screenshot_backend, SCREENSHOT, BACKEND, GObject)

struct _ScreenshotBackendInterface
{
  GTypeInterface parent;

  GdkPixbuf * (*get_pixbuf) (ScreenshotBackend *self,
                             GdkRectangle      *rectangle);
};

GdkPixbuf *screenshot_backend_get_pixbuf (ScreenshotBackend *self,
                                          GdkRectangle      *rectangle);

G_END_DECLS
