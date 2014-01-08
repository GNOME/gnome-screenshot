/* gnome-screenshot.c - Take screenshots
 *
 * Copyright (C) 2001 Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2006 Emmanuele Bassi <ebassi@gnome.org>
 * Copyright (C) 2008-2011 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef __SCREENSHOT_APPLICATION_H__
#define __SCREENSHOT_APPLICATION_H__

#include <gtk/gtk.h>

#define SCREENSHOT_TYPE_APPLICATION screenshot_application_get_type()
#define SCREENSHOT_APPLICATION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SCREENSHOT_TYPE_APPLICATION, ScreenshotApplication))
#define SCREENSHOT_APPLICATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), SCREENSHOT_TYPE_APPLICATION, ScreenshotApplicationClass))
#define SCREENSHOT_IS_APPLICATION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SCREENSHOT_TYPE_APPLICATION))
#define SCREENSHOT_IS_APPLICATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), SCREENSHOT_TYPE_APPLICATION))
#define SCREENSHOT_APPLICATION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SCREENSHOT_TYPE_APPLICATION, ScreenshotApplicationClass))

typedef struct _ScreenshotApplicationPriv ScreenshotApplicationPriv;

typedef struct {
	GtkApplication parent;
	ScreenshotApplicationPriv *priv;
} ScreenshotApplication;

typedef struct {
	GtkApplicationClass parent_class;
} ScreenshotApplicationClass;

GType screenshot_application_get_type (void);
ScreenshotApplication * screenshot_application_new (void);

#endif /* __SCREENSHOT_APPLICATION_H__ */
