/* screenshot-utils.h - common functions for GNOME Screenshot
 *
 * Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu>
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

#ifndef __SCREENSHOT_UTILS_H__
#define __SCREENSHOT_UTILS_H__

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

G_BEGIN_DECLS

gboolean   screenshot_grab_lock           (void);
void       screenshot_release_lock        (void);
gchar     *screenshot_get_window_title    (Window   w);
Window     screenshot_find_current_window (gboolean include_decoration);
GdkPixbuf *screenshot_get_pixbuf          (Window   w);

void       screenshot_show_error_dialog   (GtkWindow   *parent,
                                           const gchar *message,
                                           const gchar *detail);
void       screenshot_show_gerror_dialog  (GtkWindow   *parent,
                                           const gchar *message,
                                           GError      *error);

G_END_DECLS

#endif /* __SCREENSHOT_UTILS_H__ */
