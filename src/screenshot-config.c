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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <glib/gi18n.h>

#include "screenshot-config.h"

#define BORDER_EFFECT_KEY       "border-effect"
#define DELAY_KEY               "delay"
#define INCLUDE_BORDER_KEY      "include-border"
#define INCLUDE_POINTER_KEY     "include-pointer"
#define INCLUDE_ICC_PROFILE     "include-icc-profile"
#define AUTO_SAVE_DIRECTORY_KEY "auto-save-directory"
#define LAST_SAVE_DIRECTORY_KEY "last-save-directory"

gboolean
screenshot_load_config (gboolean clipboard_arg,
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
  static gboolean initialized = FALSE;
  ScreenshotConfig *config;

  if (initialized)
    return FALSE;

  if (window_arg && area_arg)
    {
      g_printerr (_("Conflicting options: --window and --area should not be "
                    "used at the same time.\n"));
      return FALSE;
    }

  if (delay_arg && area_arg)
    {
      g_printerr (_("Conflicting options: --area and --delay should not be "
                    "used at the same time.\n"));
      return FALSE;
    }

  config = g_slice_new0 (ScreenshotConfig);
  initialized = TRUE;

  config->interactive = interactive_arg;

  config->settings = g_settings_new ("org.gnome.gnome-screenshot");
  if (config->interactive)
    {
      if (clipboard_arg)
        g_warning ("Option --clipboard is ignored in interactive mode.");
      if (include_pointer_arg)
        g_warning ("Option --include-pointer is ignored in interactive mode.");
      if (file_arg)
        g_warning ("Option --file is ignored in interactive mode.");

      config->save_dir =
        g_settings_get_string (config->settings,
                               LAST_SAVE_DIRECTORY_KEY);
      config->delay =
        g_settings_get_int (config->settings, DELAY_KEY);
      if (delay_arg > 0)
        config->delay = delay_arg;

      config->include_border =
        g_settings_get_boolean (config->settings,
                                INCLUDE_BORDER_KEY);
      if (include_border_arg)
        config->include_border = TRUE;
      if (disable_border_arg)
        config->include_border = FALSE;

      config->include_pointer =
        g_settings_get_boolean (config->settings,
                                INCLUDE_POINTER_KEY);

      if (border_effect_arg != NULL)
        config->border_effect = g_strdup (border_effect_arg);
      else
        config->border_effect =
          g_settings_get_string (config->settings,
                                 BORDER_EFFECT_KEY);
    }
  else
    {
      config->save_dir =
        g_settings_get_string (config->settings,
                               AUTO_SAVE_DIRECTORY_KEY);
      config->delay = delay_arg;
      config->include_border = include_border_arg;
      config->include_border = !disable_border_arg;
      config->include_pointer = include_pointer_arg;
      if (border_effect_arg != NULL)
        config->border_effect = g_strdup (border_effect_arg);

      config->copy_to_clipboard = clipboard_arg;
      if (file_arg != NULL)
        config->file = g_file_new_for_commandline_arg (file_arg);
    }

  config->include_icc_profile =
    g_settings_get_boolean (config->settings,
                            INCLUDE_ICC_PROFILE);

  if (config->border_effect == NULL)
    config->border_effect = g_strdup ("none");

  config->take_window_shot = window_arg;
  config->take_area_shot = area_arg;

  screenshot_config = config;

  return TRUE;
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
                          INCLUDE_BORDER_KEY, c->include_border);
  g_settings_set_boolean (c->settings,
                          INCLUDE_POINTER_KEY, c->include_pointer);
  g_settings_set_string (c->settings,
                         BORDER_EFFECT_KEY, c->border_effect);

  if (!c->take_area_shot)
    g_settings_set_int (c->settings, DELAY_KEY, c->delay);
}
