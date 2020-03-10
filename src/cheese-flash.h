/*
 * Copyright © 2008 Alexander “weej” Jones <alex@weej.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * CheeseFlash:
 *
 * Use the accessor functions below.
 */
struct _CheeseFlash
{
  /*< private >*/
  GtkWindow parent_instance;
  void *unused;
};

#define CHEESE_TYPE_FLASH (cheese_flash_get_type ())
G_DECLARE_FINAL_TYPE (CheeseFlash, cheese_flash, CHEESE, FLASH, GtkWindow)

CheeseFlash *cheese_flash_new (void);
void cheese_flash_fire (CheeseFlash *flash,
                        GdkRectangle *rect);

G_END_DECLS
