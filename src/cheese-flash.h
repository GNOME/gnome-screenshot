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

#ifndef _CHEESE_FLASH_H_
#define _CHEESE_FLASH_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define CHEESE_TYPE_FLASH (cheese_flash_get_type ())
#define CHEESE_FLASH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CHEESE_TYPE_FLASH, CheeseFlash))
#define CHEESE_FLASH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CHEESE_TYPE_FLASH, CheeseFlashClass))
#define CHEESE_IS_FLASH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CHEESE_TYPE_FLASH))
#define CHEESE_IS_FLASH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CHEESE_TYPE_FLASH))
#define CHEESE_FLASH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), CHEESE_TYPE_FLASH, CheeseFlashClass))

typedef struct
{
  GObjectClass parent_class;
} CheeseFlashClass;

typedef struct
{
  GObject parent_instance;
} CheeseFlash;

GType        cheese_flash_get_type (void) G_GNUC_CONST;
CheeseFlash *cheese_flash_new (void);

void cheese_flash_fire (CheeseFlash *flash,
                        GdkRectangle *rect);

G_END_DECLS

#endif /* _CHEESE_FLASH_H_ */
