/* gnome-screenshot.c - Take a screenshot of the desktop
 *
 * Copyright (C) 2001 Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2006 Emmanuele Bassi <ebassi@gnome.org>
 * Copyright (C) 2008-2012 Cosimo Cecchi <cosimoc@gnome.org>
 * Copyright (C) 2020 Philipp Wolfer <ph.wolfer@gmail.com>
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

#include <gdk/gdkkeysyms.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <handy.h>

#include "screenshot-application.h"
#include "screenshot-area-selection.h"
#include "screenshot-config.h"
#include "screenshot-filename-builder.h"
#include "screenshot-interactive-dialog.h"
#include "screenshot-utils.h"
#include "screenshot-dialog.h"

#define LAST_SAVE_DIRECTORY_KEY "last-save-directory"

static void screenshot_save_to_file (ScreenshotApplication *self);
static void screenshot_show_interactive_dialog (ScreenshotApplication *self);

struct _ScreenshotApplication
{
  GtkApplication parent_instance;

  gchar *icc_profile_base64;
  GdkPixbuf *screenshot;

  gchar *save_uri;
  gboolean should_overwrite;

  GdkRectangle *rectangle;

  ScreenshotDialog *dialog;
};

G_DEFINE_TYPE (ScreenshotApplication, screenshot_application, GTK_TYPE_APPLICATION)

static void
save_folder_to_settings (ScreenshotApplication *self)
{
  g_autofree gchar *folder = screenshot_dialog_get_folder (self->dialog);
  g_settings_set_string (screenshot_config->settings,
                         LAST_SAVE_DIRECTORY_KEY, folder);
}

static void
set_recent_entry (ScreenshotApplication *self)
{
  g_autofree gchar *app_exec = NULL;
  g_autoptr(GAppInfo) app = NULL;
  GtkRecentManager *recent;
  GtkRecentData recent_data;
  const char *exec_name = NULL;
  static char * groups[2] = { "Graphics", NULL };

  app = g_app_info_get_default_for_type ("image/png", TRUE);

  if (!app) {
    /* return early, as this would be an useless recent entry anyway. */
    return;
  }

  recent = gtk_recent_manager_get_default ();

  exec_name = g_app_info_get_executable (app);
  app_exec = g_strjoin (" ", exec_name, "%u", NULL);

  recent_data.display_name = NULL;
  recent_data.description = NULL;
  recent_data.mime_type = "image/png";
  recent_data.app_name = "GNOME Screenshot";
  recent_data.app_exec = app_exec;
  recent_data.groups = groups;
  recent_data.is_private = FALSE;

  gtk_recent_manager_add_full (recent, self->save_uri, &recent_data);
}

static void
screenshot_close_interactive_dialog (ScreenshotApplication *self)
{
  ScreenshotDialog *dialog = self->dialog;
  save_folder_to_settings (self);
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
save_pixbuf_handle_success (ScreenshotApplication *self)
{
  set_recent_entry (self);

  if (screenshot_config->interactive)
    {
      screenshot_close_interactive_dialog (self);
    }
  else
    {
      g_application_release (G_APPLICATION (self));
    }
}

static void
screenshot_dialog_focus_cb (gint                   response,
                            ScreenshotApplication *self)
{
  gtk_widget_grab_focus (screenshot_dialog_get_filename_entry (self->dialog));
}

static void
screenshot_dialog_override_cb (gint                   response,
                               ScreenshotApplication *self)
{
  if (response == GTK_RESPONSE_YES)
    {
      self->should_overwrite = TRUE;
      screenshot_save_to_file (self);

      return;
    }

  screenshot_dialog_focus_cb (response, self);
}

static void
save_pixbuf_handle_error (ScreenshotApplication *self,
                          GError *error)
{
  if (screenshot_config->interactive)
    {
      ScreenshotDialog *dialog = self->dialog;

      screenshot_dialog_set_busy (dialog, FALSE);

      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS) &&
          !self->should_overwrite)
        {
          g_autofree gchar *folder = screenshot_dialog_get_folder (dialog);
          g_autofree gchar *folder_uri = g_path_get_basename (folder);
          g_autofree gchar *folder_name = g_uri_unescape_string (folder_uri, NULL);
          g_autofree gchar *file_name = screenshot_dialog_get_filename (dialog);
          g_autofree gchar *detail = g_strdup_printf (_("A file named “%s” already exists in “%s”"),
                                                      file_name, folder_name);

          screenshot_show_dialog (GTK_WINDOW (dialog),
                                  GTK_MESSAGE_WARNING,
                                  GTK_BUTTONS_YES_NO,
                                  _("Overwrite existing file?"),
                                  detail,
                                  (ScreenshotResponseFunc) screenshot_dialog_override_cb,
                                  self);
        }
      else
        {
          screenshot_show_dialog (GTK_WINDOW (dialog),
                                  GTK_MESSAGE_ERROR,
                                  GTK_BUTTONS_OK,
                                  _("Unable to capture a screenshot"),
                                  _("Error creating file. Please choose another location and retry."),
                                  (ScreenshotResponseFunc) screenshot_dialog_focus_cb,
                                  dialog);
        }
    }
  else
    {
      g_critical ("Unable to save the screenshot: %s", error->message);
      g_application_release (G_APPLICATION (self));
      if (screenshot_config->file != NULL)
        exit (EXIT_FAILURE);
    }
}

static void
save_pixbuf_ready_cb (GObject *source,
                      GAsyncResult *res,
                      gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  ScreenshotApplication *self = user_data;

  gdk_pixbuf_save_to_stream_finish (res, &error);

  if (error != NULL)
    {
      save_pixbuf_handle_error (self, error);
      return;
    }

  save_pixbuf_handle_success (self);
}

static void
find_out_writable_format_by_extension (gpointer data,
                                       gpointer user_data)
{
  GdkPixbufFormat *format     = (GdkPixbufFormat*) data;
  gchar          **name       = (gchar **) user_data;
  g_auto(GStrv)    extensions = gdk_pixbuf_format_get_extensions (format);
  gchar          **ptr        = extensions;

  while (*ptr != NULL)
    {
      if (g_strcmp0 (*ptr, *name) == 0 &&
          gdk_pixbuf_format_is_writable (format) == TRUE)
        {
          g_free (*name);
          *name = gdk_pixbuf_format_get_name (format);
          break;
        }
      ptr++;
    }
}

static gboolean
is_png (gchar *format)
{
  if (g_strcmp0 (format, "png") == 0)
    return TRUE;
  else
    return FALSE;
}

static gboolean
has_profile (ScreenshotApplication *self)
{
  return (self->icc_profile_base64 != NULL);
}

static void
save_with_description_and_profile (ScreenshotApplication *self,
                                   GFileOutputStream     *os,
                                   gchar                 *format)
{
  gdk_pixbuf_save_to_stream_async (self->screenshot,
                                   G_OUTPUT_STREAM (os),
                                   format, NULL,
                                   save_pixbuf_ready_cb, self,
                                   "icc-profile", self->icc_profile_base64,
                                   "tEXt::Software", "gnome-screenshot",
                                   NULL);
}
static void
save_with_description (ScreenshotApplication *self,
                       GFileOutputStream     *os,
                       gchar                 *format)
{
  gdk_pixbuf_save_to_stream_async (self->screenshot,
                                   G_OUTPUT_STREAM (os),
                                   format, NULL,
                                   save_pixbuf_ready_cb, self,
                                   "tEXt::Software", "gnome-screenshot",
                                   NULL);
}

static void
save_with_no_profile_or_description (ScreenshotApplication *self,
                                     GFileOutputStream     *os,
                                     gchar                 *format)
{
  gdk_pixbuf_save_to_stream_async (self->screenshot,
                                   G_OUTPUT_STREAM (os),
                                   format, NULL,
                                   save_pixbuf_ready_cb, self,
                                   NULL);
}

static void
save_file_create_ready_cb (GObject *source,
                           GAsyncResult *res,
                           gpointer user_data)
{
  ScreenshotApplication *self = user_data;
  g_autoptr(GFileOutputStream) os = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *basename = g_file_get_basename (G_FILE (source));
  g_autofree gchar *format = NULL;
  gchar *extension = g_strrstr (basename, ".");
  GSList *formats = NULL;

  if (extension == NULL)
    extension = "png";
  else
    extension++;

  format = g_strdup (extension);

  formats = gdk_pixbuf_get_formats();
  g_slist_foreach (formats,
                   find_out_writable_format_by_extension,
                   (gpointer) &format);
  g_slist_free (formats);

  if (self->should_overwrite)
    os = g_file_replace_finish (G_FILE (source), res, &error);
  else
    os = g_file_create_finish (G_FILE (source), res, &error);

  if (error != NULL)
    {
      save_pixbuf_handle_error (self, error);
      return;
    }

  if (is_png (format))
    {
      if (has_profile (self))
        save_with_description_and_profile (self, os, format);
      else
        save_with_description (self, os, format);
    }
  else
    {
      save_with_no_profile_or_description (self, os, format);
    }
}

static void
screenshot_save_to_file (ScreenshotApplication *self)
{
  g_autoptr(GFile) target_file = NULL;

  if (self->dialog != NULL)
    screenshot_dialog_set_busy (self->dialog, TRUE);

  target_file = g_file_new_for_uri (self->save_uri);

  if (self->should_overwrite)
    {
      g_file_replace_async (target_file,
                            NULL, FALSE,
                            G_FILE_CREATE_NONE,
                            G_PRIORITY_DEFAULT,
                            NULL,
                            save_file_create_ready_cb, self);
    }
  else
    {
      g_file_create_async (target_file,
                           G_FILE_CREATE_NONE,
                           G_PRIORITY_DEFAULT,
                           NULL,
                           save_file_create_ready_cb, self);
    }
}

static void
screenshot_back (ScreenshotApplication *self)
{
  screenshot_close_interactive_dialog (self);
  screenshot_show_interactive_dialog (self);
}

static void
screenshot_save_to_clipboard (ScreenshotApplication *self)
{
  GtkClipboard *clipboard;

  clipboard = gtk_clipboard_get_for_display (gdk_display_get_default (),
                                             GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_image (clipboard, self->screenshot);
}

static void
save_clicked_cb (ScreenshotDialog      *dialog,
                 ScreenshotApplication *self)
{
  /* update to the new URI */
  g_free (self->save_uri);
  self->save_uri = screenshot_dialog_get_uri (self->dialog);
  screenshot_save_to_file (self);
}

static void
copy_clicked_cb (ScreenshotDialog      *dialog,
                 ScreenshotApplication *self)
{
  screenshot_save_to_clipboard (self);
}

static void
back_clicked_cb (ScreenshotDialog      *dialog,
                 ScreenshotApplication *self)
{
  screenshot_back (self);
}

static void
build_filename_ready_cb (GObject *source,
                         GAsyncResult *res,
                         gpointer user_data)
{
  ScreenshotApplication *self = user_data;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *save_path = screenshot_build_filename_finish (res, &error);

  if (save_path != NULL)
    {
      g_autoptr(GFile) file = g_file_new_for_path (save_path);
      self->save_uri = g_file_get_uri (file);
    }
  else
    self->save_uri = NULL;

  /* now release the application */
  g_application_release (G_APPLICATION (self));

  if (error != NULL)
    {
      g_critical ("Impossible to find a valid location to save the screenshot: %s",
                  error->message);

      if (screenshot_config->interactive)
        screenshot_show_dialog (NULL,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_OK,
                                _("Unable to capture a screenshot"),
                                _("Error creating file"),
                                NULL,
                                NULL);
      else
        {
          if (screenshot_config->file != NULL)
            exit (EXIT_FAILURE);
        }

      return;
    }

  if (screenshot_config->interactive)
    {
      self->dialog = screenshot_dialog_new (GTK_APPLICATION (self),
                                            self->screenshot,
                                            self->save_uri);
      g_signal_connect_object (self->dialog, "save", G_CALLBACK (save_clicked_cb), self, 0);
      g_signal_connect_object (self->dialog, "copy", G_CALLBACK (copy_clicked_cb), self, 0);
      g_signal_connect_object (self->dialog, "back", G_CALLBACK (back_clicked_cb), self, 0);
    }
  else
    {
      g_application_hold (G_APPLICATION (self));
      screenshot_save_to_file (self);
    }
}

static void
screenshot_release_cb (gint                   response,
                       ScreenshotApplication *self)
{
  g_application_release (G_APPLICATION (self));
  if (screenshot_config->file != NULL)
    exit (EXIT_FAILURE);
}

static void
finish_take_screenshot (ScreenshotApplication *self)
{
  GdkPixbuf *screenshot;

  screenshot = screenshot_get_pixbuf (self->rectangle);
  g_clear_pointer (&self->rectangle, g_free);

  if (screenshot == NULL)
    {
      g_critical ("Unable to capture a screenshot of any window");

      if (screenshot_config->interactive)
        screenshot_show_dialog (NULL,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_OK,
                                _("Unable to capture a screenshot"),
                                _("All possible methods failed"),
                                (ScreenshotResponseFunc) screenshot_release_cb,
                                self);

      return;
    }

  self->screenshot = screenshot;

  if (screenshot_config->copy_to_clipboard)
    {
      screenshot_save_to_clipboard (self);
      if (screenshot_config->file == NULL)
        {
          g_application_release (G_APPLICATION (self));
          
          return;
        }
    }

  /* FIXME: apply the ICC profile according to the preferences.
   * org.gnome.ColorManager.GetProfileForWindow() does not exist anymore,
   * so we probably need to fetch the color profile of the screen where
   * the area/window was.
   *
   * screenshot_ensure_icc_profile (window);
   */
  if (screenshot_config->file != NULL)
    {
      self->save_uri = g_file_get_uri (screenshot_config->file);
      self->should_overwrite = TRUE;
      screenshot_save_to_file (self);
    }
  else
    screenshot_build_filename_async (screenshot_config->save_dir, NULL, build_filename_ready_cb, self);
}

static gboolean
take_screenshot_timeout (gpointer user_data)
{
  ScreenshotApplication *self = user_data;
  finish_take_screenshot (self);

  return FALSE;
}

static void
start_screenshot_timeout (ScreenshotApplication *self)
{
  guint delay = screenshot_config->delay * 1000;

  if (!screenshot_config->take_area_shot)
    /* hold the GApplication while doing the async screenshot op */
    g_application_hold (G_APPLICATION (self));

  /* HACK: give time to the dialog to actually disappear.
   * We don't have any way to tell when the compositor has finished
   * re-drawing.
   */
  if (delay == 0 && screenshot_config->interactive)
    delay = 200;

  if (delay > 0)
    g_timeout_add (delay,
                   take_screenshot_timeout,
                   self);
  else
    g_idle_add (take_screenshot_timeout, self);
}

static void
rectangle_found_cb (GdkRectangle *rectangle,
                    gpointer user_data)
{
  ScreenshotApplication *self = user_data;

  if (rectangle != NULL)
    {
      self->rectangle = g_memdup (rectangle, sizeof *rectangle);
      start_screenshot_timeout (self);
    }
  else
    {
      /* user dismissed the area selection, possibly show the dialog again */
      g_application_release (G_APPLICATION (self));

      if (screenshot_config->interactive)
        screenshot_show_interactive_dialog (self);
    }
}

static void
screenshot_start (ScreenshotApplication *self)
{
  if (screenshot_config->take_area_shot)
    {
      /* hold the GApplication while selecting the screen area */
      g_application_hold (G_APPLICATION (self));
      screenshot_select_area_async (rectangle_found_cb, self);
    }
  else
    {
      start_screenshot_timeout (self);
    }

  screenshot_save_config ();
}

static gboolean version_arg = FALSE;

static const GOptionEntry entries[] = {
  { "clipboard", 'c', 0, G_OPTION_ARG_NONE, NULL, N_("Send the grab directly to the clipboard"), NULL },
  { "window", 'w', 0, G_OPTION_ARG_NONE, NULL, N_("Grab a window instead of the entire screen"), NULL },
  { "area", 'a', 0, G_OPTION_ARG_NONE, NULL, N_("Grab an area of the screen instead of the entire screen"), NULL },
  { "include-border", 'b', 0, G_OPTION_ARG_NONE, NULL, N_("Include the window border with the screenshot. This option is deprecated and window border is always included"), NULL },
  { "remove-border", 'B', 0, G_OPTION_ARG_NONE, NULL, N_("Remove the window border from the screenshot. This option is deprecated and window border is always included"), NULL },
  { "include-pointer", 'p', 0, G_OPTION_ARG_NONE, NULL, N_("Include the pointer with the screenshot"), NULL },
  { "delay", 'd', 0, G_OPTION_ARG_INT, NULL, N_("Take screenshot after specified delay [in seconds]"), N_("seconds") },
  { "border-effect", 'e', 0, G_OPTION_ARG_STRING, NULL, N_("Effect to add to the border (‘shadow’, ‘border’, ‘vintage’ or ‘none’). Note: This option is deprecated and is assumed to be ‘none’"), N_("effect") },
  { "interactive", 'i', 0, G_OPTION_ARG_NONE, NULL, N_("Interactively set options"), NULL },
  { "file", 'f', 0, G_OPTION_ARG_FILENAME, NULL, N_("Save screenshot directly to this file"), N_("filename") },
  { "version", 0, 0, G_OPTION_ARG_NONE, &version_arg, N_("Print version information and exit"), NULL },
  { NULL },
};

static gint
screenshot_application_handle_local_options (GApplication *app,
                                             GVariantDict *options)
{
  if (version_arg)
    {
      g_print ("%s %s\n", g_get_application_name (), VERSION);
      exit (EXIT_SUCCESS);
    }

  return -1;
}

static gint
screenshot_application_command_line (GApplication            *app,
                                     GApplicationCommandLine *command_line)
{
  ScreenshotApplication *self = SCREENSHOT_APPLICATION (app);
  gboolean clipboard_arg = FALSE;
  gboolean window_arg = FALSE;
  gboolean area_arg = FALSE;
  gboolean include_border_arg = FALSE;
  gboolean disable_border_arg = FALSE;
  gboolean include_pointer_arg = FALSE;
  gboolean interactive_arg = FALSE;
  gchar *border_effect_arg = NULL;
  guint delay_arg = 0;
  gchar *file_arg = NULL;
  GVariantDict *options;
  gint exit_status = EXIT_SUCCESS;
  gboolean res;

  options = g_application_command_line_get_options_dict (command_line);
  g_variant_dict_lookup (options, "clipboard", "b", &clipboard_arg);
  g_variant_dict_lookup (options, "window", "b", &window_arg);
  g_variant_dict_lookup (options, "area", "b", &area_arg);
  g_variant_dict_lookup (options, "include-border", "b", &include_border_arg);
  g_variant_dict_lookup (options, "remove-border", "b", &disable_border_arg);
  g_variant_dict_lookup (options, "include-pointer", "b", &include_pointer_arg);
  g_variant_dict_lookup (options, "interactive", "b", &interactive_arg);
  g_variant_dict_lookup (options, "border-effect", "&s", &border_effect_arg);
  g_variant_dict_lookup (options, "delay", "i", &delay_arg);
  g_variant_dict_lookup (options, "file", "^&ay", &file_arg);

  res = screenshot_config_parse_command_line (clipboard_arg,
                                              window_arg,
                                              area_arg,
                                              include_border_arg,
                                              disable_border_arg,
                                              include_pointer_arg,
                                              border_effect_arg,
                                              delay_arg,
                                              interactive_arg,
                                              file_arg);
  if (!res)
    {
      exit_status = EXIT_FAILURE;
      goto out;
    }

  /* interactive mode: trigger the dialog and wait for the response */
  if (screenshot_config->interactive)
    g_application_activate (app);
  else
    screenshot_start (self);

 out:
  return exit_status;
}

static void
capture_clicked_cb (ScreenshotInteractiveDialog *dialog,
                    ScreenshotApplication       *self)
{
  gtk_widget_destroy (GTK_WIDGET (dialog));
  screenshot_start (self);
}

static void
screenshot_show_interactive_dialog (ScreenshotApplication *self)
{
  ScreenshotInteractiveDialog *dialog;

  dialog = screenshot_interactive_dialog_new (GTK_APPLICATION (self));

  g_signal_connect_object (dialog, "capture", G_CALLBACK (capture_clicked_cb), self, 0);

  gtk_widget_show (GTK_WIDGET (dialog));
}

static void
action_quit (GSimpleAction *action,
             GVariant *parameter,
             gpointer user_data)
{
  GList *windows = gtk_application_get_windows (GTK_APPLICATION (user_data));
  gtk_widget_destroy (g_list_nth_data (windows, 0));
}

static void
action_help (GSimpleAction *action,
             GVariant *parameter,
             gpointer user_data)
{
  GList *windows = gtk_application_get_windows (GTK_APPLICATION (user_data));
  screenshot_display_help (g_list_nth_data (windows, 0));
}

static void
action_about (GSimpleAction *action,
              GVariant *parameter,
              gpointer user_data)
{
  const gchar *authors[] = {
    "Emmanuele Bassi",
    "Jonathan Blandford",
    "Cosimo Cecchi",
    "Alexander Mikhaylenko",
    NULL
  };

  const gchar *artists[] = {
    "Tobias Bernard",
    "Jakub Steiner",
    NULL
  };

  GList *windows = gtk_application_get_windows (GTK_APPLICATION (user_data));
  gtk_show_about_dialog (GTK_WINDOW (g_list_nth_data (windows, 0)),
                         "version", VERSION,
                         "authors", authors,
                         "artists", artists,
                         "program-name", _("Screenshot"),
                         "comments", _("Save images of your screen or individual windows"),
                         "logo-icon-name", SCREENSHOT_ICON_NAME,
                         "translator-credits", _("translator-credits"),
                         "license-type", GTK_LICENSE_GPL_2_0,
                         "wrap-license", TRUE,
                         NULL);
}

static void
action_screen_shot (GSimpleAction *action,
                    GVariant *parameter,
                    gpointer user_data)
{
  ScreenshotApplication *self = SCREENSHOT_APPLICATION (user_data);

  screenshot_config_parse_command_line (FALSE, /* clipboard */
                                        FALSE,  /* window */
                                        FALSE, /* area */
                                        FALSE, /* include border */
                                        FALSE, /* disable border */
                                        FALSE, /* include pointer */
                                        NULL,  /* border effect */
                                        0,     /* delay */
                                        FALSE, /* interactive */
                                        NULL); /* file */
  screenshot_start (self);
}

static void
action_window_shot (GSimpleAction *action,
                    GVariant *parameter,
                    gpointer user_data)
{
  ScreenshotApplication *self = SCREENSHOT_APPLICATION (user_data);

  screenshot_config_parse_command_line (FALSE, /* clipboard */
                                        TRUE,  /* window */
                                        FALSE, /* area */
                                        FALSE, /* include border */
                                        FALSE, /* disable border */
                                        FALSE, /* include pointer */
                                        NULL,  /* border effect */
                                        0,     /* delay */
                                        FALSE, /* interactive */
                                        NULL); /* file */
  screenshot_start (self);
}

static GActionEntry action_entries[] = {
  { "about", action_about, NULL, NULL, NULL },
  { "help", action_help, NULL, NULL, NULL },
  { "quit", action_quit, NULL, NULL, NULL },
  { "screen-shot", action_screen_shot, NULL, NULL, NULL },
  { "window-shot", action_window_shot, NULL, NULL, NULL }
};

static void
screenshot_application_startup (GApplication *app)
{
  const gchar *help_accels[2] = { "F1", NULL };
  const gchar *quit_accels[2] = { "<Primary>q", NULL };
  ScreenshotApplication *self = SCREENSHOT_APPLICATION (app);

  G_APPLICATION_CLASS (screenshot_application_parent_class)->startup (app);

  hdy_init ();
  hdy_style_manager_set_color_scheme (hdy_style_manager_get_default (),
                                      HDY_COLOR_SCHEME_PREFER_LIGHT);

  screenshot_load_config ();

  g_set_application_name (_("Screenshot"));
  g_set_prgname ("org.gnome.Screenshot");
  gtk_window_set_default_icon_name (SCREENSHOT_ICON_NAME);

  g_action_map_add_action_entries (G_ACTION_MAP (self), action_entries,
                                   G_N_ELEMENTS (action_entries), self);

  gtk_application_set_accels_for_action (GTK_APPLICATION (self), "app.help", help_accels);
  gtk_application_set_accels_for_action (GTK_APPLICATION (self), "app.quit", quit_accels);
}

static void
screenshot_application_activate (GApplication *app)
{
  GtkWindow *window;

  window = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (window != NULL)
    {
      gtk_window_present (GTK_WINDOW (window));
      return;
    }

  screenshot_config->interactive = TRUE;
  screenshot_show_interactive_dialog (SCREENSHOT_APPLICATION (app));
}

static void
screenshot_application_finalize (GObject *object)
{
  ScreenshotApplication *self = SCREENSHOT_APPLICATION (object);

  g_clear_object (&self->screenshot);
  g_free (self->icc_profile_base64);
  g_free (self->save_uri);
  g_clear_pointer (&self->rectangle, g_free);

  G_OBJECT_CLASS (screenshot_application_parent_class)->finalize (object);
}

static void
screenshot_application_class_init (ScreenshotApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

  object_class->finalize = screenshot_application_finalize;

  application_class->handle_local_options = screenshot_application_handle_local_options;
  application_class->command_line = screenshot_application_command_line;
  application_class->startup = screenshot_application_startup;
  application_class->activate = screenshot_application_activate;
}

static void
screenshot_application_init (ScreenshotApplication *self)
{
  g_application_add_main_option_entries (G_APPLICATION (self), entries);
}

ScreenshotApplication *
screenshot_application_new (void)
{
  return g_object_new (SCREENSHOT_TYPE_APPLICATION,
                       "application-id", "org.gnome.Screenshot",
                       "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                       NULL);
}
