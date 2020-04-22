/* screenshot-backend.c - Backend interface
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

#include "config.h"

#include "screenshot-backend.h"

G_DEFINE_INTERFACE (ScreenshotBackend, screenshot_backend, G_TYPE_OBJECT)

static void
screenshot_backend_default_init (ScreenshotBackendInterface *iface)
{
}

GdkPixbuf *
screenshot_backend_get_pixbuf (ScreenshotBackend *self,
                               GdkRectangle      *rectangle)
{
  ScreenshotBackendInterface *iface;

  g_return_val_if_fail (SCREENSHOT_IS_BACKEND (self), NULL);

  iface = SCREENSHOT_BACKEND_GET_IFACE (self);

  g_return_val_if_fail (iface->get_pixbuf != NULL, NULL);

  return iface->get_pixbuf (self, rectangle);
}
