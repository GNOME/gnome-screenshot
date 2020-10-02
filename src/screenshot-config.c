/* screenshot-config.h - Holds current configuration for gnome-screenshot
 *
 * Copyright (C) 2001 Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2006 Emmanuele Bassi <ebassi@gnome.org>
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

#include "config.h"

#include <glib/gi18n.h>

#include "screenshot-config.h"

#define DELAY_KEY               "delay"
#define INCLUDE_POINTER_KEY     "include-pointer"
#define INCLUDE_ICC_PROFILE     "include-icc-profile"
#define AUTO_SAVE_DIRECTORY_KEY "auto-save-directory"
#define LAST_SAVE_DIRECTORY_KEY "last-save-directory"
#define DEFAULT_FILE_TYPE_KEY   "default-file-type"

ScreenshotConfig *screenshot_config;

void
screenshot_load_config (void)
{
  ScreenshotConfig *config;

  config = g_slice_new0 (ScreenshotConfig);

  config->settings = g_settings_new ("org.gnome.gnome-screenshot");
  config->save_dir =
    g_settings_get_string (config->settings,
                           LAST_SAVE_DIRECTORY_KEY);
  config->delay =
    g_settings_get_int (config->settings,
                        DELAY_KEY);
  config->include_pointer =
    g_settings_get_boolean (config->settings,
                            INCLUDE_POINTER_KEY);
  config->file_type =
    g_settings_get_string (config->settings,
                           DEFAULT_FILE_TYPE_KEY);
  config->include_icc_profile =
    g_settings_get_boolean (config->settings,
                            INCLUDE_ICC_PROFILE);

  config->take_window_shot = FALSE;
  config->take_area_shot = FALSE;

  screenshot_config = config;
}

void
screenshot_save_config (void)
{
  ScreenshotConfig *c = screenshot_config;

  g_assert (c != NULL);

  /* if we were not started up in interactive mode, avoid
   * overwriting these settings.
   */
  if (!c->interactive)
    return;

  g_settings_set_boolean (c->settings,
                          INCLUDE_POINTER_KEY, c->include_pointer);

  g_settings_set_int (c->settings, DELAY_KEY, c->delay);
}

gboolean
screenshot_config_parse_command_line (gboolean clipboard_arg,
                                      gboolean window_arg,
                                      gboolean area_arg,
                                      gboolean include_border_arg,
                                      gboolean disable_border_arg,
                                      gboolean include_pointer_arg,
                                      const gchar *border_effect_arg,
                                      guint delay_arg,
                                      gboolean interactive_arg,
                                      const gchar *file_arg)
{
  if (window_arg && area_arg)
    {
      g_printerr (_("Conflicting options: --window and --area should not be "
                    "used at the same time.\n"));
      return FALSE;
    }

  screenshot_config->interactive = interactive_arg;

  if (include_border_arg)
    g_warning ("Option --include-border is deprecated and will be removed in "
               "gnome-screenshot 3.38.0. Window border is always included.");
  if (disable_border_arg)
    g_warning ("Option --remove-border is deprecated and will be removed in "
               "gnome-screenshot 3.38.0. Window border is always included.");
  if (border_effect_arg != NULL)
    g_warning ("Option --border-effect is deprecated and will be removed in "
               "gnome-screenshot 3.38.0. No effect will be used.");

  if (screenshot_config->interactive)
    {
      if (clipboard_arg)
        g_warning ("Option --clipboard is ignored in interactive mode.");
      if (include_pointer_arg)
        g_warning ("Option --include-pointer is ignored in interactive mode.");
      if (file_arg)
        g_warning ("Option --file is ignored in interactive mode.");

      if (delay_arg > 0)
        screenshot_config->delay = delay_arg;
    }
  else
    {
      g_free (screenshot_config->save_dir);
      screenshot_config->save_dir =
        g_settings_get_string (screenshot_config->settings,
                               AUTO_SAVE_DIRECTORY_KEY);

      screenshot_config->delay = delay_arg;
      screenshot_config->include_pointer = include_pointer_arg;
      screenshot_config->copy_to_clipboard = clipboard_arg;
      if (file_arg != NULL)
        screenshot_config->file = g_file_new_for_commandline_arg (file_arg);
    }

  screenshot_config->take_window_shot = window_arg;
  screenshot_config->take_area_shot = area_arg;

  return TRUE;
}
