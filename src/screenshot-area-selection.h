/* screenshot-area-selection.h - interactive screenshot area selection
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

#pragma once

#include <gdk/gdk.h>

G_BEGIN_DECLS

typedef void (* SelectAreaCallback) (GdkRectangle *rectangle,
                                     gpointer user_data);

void       screenshot_select_area_async   (SelectAreaCallback callback,
                                           gpointer callback_data);

G_END_DECLS
