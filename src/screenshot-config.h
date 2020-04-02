/* screenshot-config.h - Holds current configuration for gnome-screenshot
 *
 * Copyright (C) 2008, 2011 Cosimo Cecchi <cosimoc@gnome.org>
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

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct {
  GSettings *settings;

  gchar *save_dir;
  gchar *file_type;
  GFile *file;

  gboolean copy_to_clipboard;

  gboolean take_window_shot;
  gboolean take_area_shot;

  gboolean include_pointer;
  gboolean include_icc_profile;

  guint delay;

  gboolean interactive;
} ScreenshotConfig;

extern ScreenshotConfig *screenshot_config;

void        screenshot_load_config                (void);
void        screenshot_save_config                (void);
gboolean    screenshot_config_parse_command_line  (gboolean clipboard_arg,
                                                   gboolean window_arg,
                                                   gboolean area_arg,
                                                   gboolean include_border_arg,
                                                   gboolean disable_border_arg,
                                                   gboolean include_pointer_arg,
                                                   const gchar *border_effect_arg,
                                                   guint delay_arg,
                                                   gboolean interactive_arg,
                                                   const gchar *file_arg);

G_END_DECLS
