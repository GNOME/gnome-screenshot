/* gnome-screenshot.c - Take a screenshot of the desktop
 *
 * Copyright (C) 2001 Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2006 Emmanuele Bassi <ebassi@gnome.org>
 * Copyright (C) 2008-2012 Cosimo Cecchi <cosimoc@gnome.org>
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
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "screenshot-application.h"
#include "screenshot-area-selection.h"
#include "screenshot-config.h"
#include "screenshot-filename-builder.h"
#include "screenshot-interactive-dialog.h"
#include "screenshot-shadow.h"
#include "screenshot-utils.h"
#include "screenshot-dialog.h"

#define SCREENSHOOTER_ICON "applets-screenshooter"

#define LAST_SAVE_DIRECTORY_KEY "last-save-directory"

G_DEFINE_TYPE (ScreenshotApplication, screenshot_application, GTK_TYPE_APPLICATION);

static void screenshot_save_to_file (ScreenshotApplication *self);
static void screenshot_show_interactive_dialog (ScreenshotApplication *self);

struct _ScreenshotApplicationPriv {
  gchar *icc_profile_base64;
  GdkPixbuf *screenshot;

  gchar *save_uri;
  gboolean should_overwrite;

  ScreenshotDialog *dialog;
};

static void
save_folder_to_settings (ScreenshotApplication *self)
{
  char *folder;

  folder = screenshot_dialog_get_folder (self->priv->dialog);
  g_settings_set_string (screenshot_config->settings,
                         LAST_SAVE_DIRECTORY_KEY, folder);

  g_free (folder);
}

static void
set_recent_entry (ScreenshotApplication *self)
{
  char *app_exec = NULL;
  GtkRecentManager *recent;
  GtkRecentData recent_data;
  GAppInfo *app;
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

  gtk_recent_manager_add_full (recent, self->priv->save_uri, &recent_data);

  g_object_unref (app);
  g_free (app_exec);
}

static void
save_pixbuf_handle_success (ScreenshotApplication *self)
{
  set_recent_entry (self);

  if (screenshot_config->interactive)
    {
      ScreenshotDialog *dialog = self->priv->dialog;

      save_folder_to_settings (self);
      gtk_widget_destroy (dialog->dialog);
    }
  else
    {
      g_application_release (G_APPLICATION (self));
    }
}

static void
save_pixbuf_handle_error (ScreenshotApplication *self,
                          GError *error)
{
  if (screenshot_config->interactive)
    {
      ScreenshotDialog *dialog = self->priv->dialog;

      screenshot_dialog_set_busy (dialog, FALSE);

      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS) &&
          !self->priv->should_overwrite)
        {
          gchar *folder = screenshot_dialog_get_folder (dialog);
          gchar *folder_name = g_path_get_basename (folder);
          gchar *file_name = screenshot_dialog_get_filename (dialog);
          gchar *detail = g_strdup_printf (_("A file named \"%s\" already exists in \"%s\""),
                                           file_name, folder_name);
          gint response;
                                             
          response = screenshot_show_dialog (GTK_WINDOW (dialog->dialog),
                                             GTK_MESSAGE_WARNING,
                                             GTK_BUTTONS_YES_NO,
                                             _("Overwrite existing file?"),
                                             detail);

          g_free (folder);
          g_free (folder_name);
          g_free (file_name);
          g_free (detail);

          if (response == GTK_RESPONSE_YES)
            {
              self->priv->should_overwrite = TRUE;
              screenshot_save_to_file (self);

              return;
            }
        }
      else
        {
          screenshot_show_dialog (GTK_WINDOW (dialog->dialog),
                                  GTK_MESSAGE_ERROR,
                                  GTK_BUTTONS_OK,
                                  _("Unable to capture a screenshot"),
                                  _("Error creating file. Please choose another location and retry."));
        }

      gtk_widget_grab_focus (dialog->filename_entry);
    }
  else
    {
      g_critical ("Unable to save the screenshot: %s", error->message);
      screenshot_play_sound_effect ("dialog-error", _("Unable to capture a screenshot"));
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
  GError *error = NULL;
  ScreenshotApplication *self = user_data;

  gdk_pixbuf_save_to_stream_finish (res, &error);

  if (error != NULL)
    {
      save_pixbuf_handle_error (self, error);
      g_error_free (error);
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
  gchar          **extensions = gdk_pixbuf_format_get_extensions (format);
  gchar          **ptr        = extensions;
  gboolean         found      = FALSE;

  while (*ptr != NULL)
    {
      if (g_strcmp0 (*ptr, *name) == 0 &&
          gdk_pixbuf_format_is_writable (format) == TRUE)
        {
          *name = gdk_pixbuf_format_get_name (format);
	  found = TRUE;
          break;
        }
      ptr++;
    }

  g_strfreev (extensions);

  /* Needing to duplicate string here because
   * gdk_pixbuf_format_get_name will return a duplicated string.
   */
  if (!found)
    *name = g_strdup (*name);
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
  if (self->priv->icc_profile_base64 != NULL)
    return TRUE;
  else
    return FALSE;
}

static void
save_with_description_and_profile (ScreenshotApplication *self,
                                   GFileOutputStream     *os,
                                   gchar                 *format)
{
  gdk_pixbuf_save_to_stream_async (self->priv->screenshot,
                                   G_OUTPUT_STREAM (os),
                                   format, NULL,
                                   save_pixbuf_ready_cb, self,
                                   "icc-profile", self->priv->icc_profile_base64,
                                   "tEXt::Software", "gnome-screenshot",
                                   NULL);
}
static void
save_with_description (ScreenshotApplication *self,
                       GFileOutputStream     *os,
                       gchar                 *format)
{
  gdk_pixbuf_save_to_stream_async (self->priv->screenshot,
                                   G_OUTPUT_STREAM (os),
                                   format, NULL,
                                   save_pixbuf_ready_cb, self,
                                   "tEXt::Software", "gnome-screenshot",
                                   NULL);
}

static void
save_with_profile (ScreenshotApplication *self,
                   GFileOutputStream     *os,
                   gchar                 *format)
{
  gdk_pixbuf_save_to_stream_async (self->priv->screenshot,
                                   G_OUTPUT_STREAM (os),
                                   format, NULL,
                                   save_pixbuf_ready_cb, self,
                                   "icc-profile", self->priv->icc_profile_base64,
                                   NULL);
}

static void
save_with_no_profile_or_description (ScreenshotApplication *self,
                                     GFileOutputStream     *os,
                                     gchar                 *format)
{
  gdk_pixbuf_save_to_stream_async (self->priv->screenshot,
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
  GFileOutputStream *os;
  GError *error = NULL;
  gchar *basename = g_file_get_basename (G_FILE (source));
  gchar *extension = g_strrstr (basename, ".");
  gchar *format = NULL;
  GSList *formats = NULL;

  if (extension == NULL)
    extension = "png";
  else
    extension++;

  format = extension;

  formats = gdk_pixbuf_get_formats();
  g_slist_foreach (formats,
                   find_out_writable_format_by_extension,
                   (gpointer) &format);
  g_slist_free (formats);
  g_free (basename);

  if (self->priv->should_overwrite)
    os = g_file_replace_finish (G_FILE (source), res, &error);
  else
    os = g_file_create_finish (G_FILE (source), res, &error);

  if (error != NULL)
    {
      save_pixbuf_handle_error (self, error);
      g_error_free (error);
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

  g_object_unref (os);
  g_free (format);
}

static void
screenshot_save_to_file (ScreenshotApplication *self)
{
  GFile *target_file;

  if (self->priv->dialog != NULL)
    screenshot_dialog_set_busy (self->priv->dialog, TRUE);

  target_file = g_file_new_for_uri (self->priv->save_uri);

  if (self->priv->should_overwrite)
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

  g_object_unref (target_file);
}

static void
screenshot_save_to_clipboard (ScreenshotApplication *self)
{
  GtkClipboard *clipboard;

  clipboard = gtk_clipboard_get_for_display (gdk_display_get_default (),
                                             GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_image (clipboard, self->priv->screenshot);
}

static void
screenshot_dialog_response_cb (GtkDialog *d,
                               gint response_id,
                               gpointer user_data)
{
  ScreenshotApplication *self = user_data;

  switch (response_id)
    {
    case GTK_RESPONSE_HELP:
      screenshot_display_help (GTK_WINDOW (d));
      break;
    case GTK_RESPONSE_OK:
      /* update to the new URI */
      g_free (self->priv->save_uri);
      self->priv->save_uri = screenshot_dialog_get_uri (self->priv->dialog);
      screenshot_save_to_file (self);
      break;
    case SCREENSHOT_RESPONSE_COPY:
      screenshot_save_to_clipboard (self);
      break;
    default:
      gtk_widget_destroy (GTK_WIDGET (d));
      break;
    }
}

static void
build_filename_ready_cb (GObject *source,
                         GAsyncResult *res,
                         gpointer user_data)
{
  ScreenshotApplication *self = user_data;
  GError *error = NULL;
  char *save_path;

  save_path = screenshot_build_filename_finish (res, &error);
  if (save_path != NULL)
    {
      GFile *file;

      file = g_file_new_for_path (save_path);
      g_free (save_path);

      self->priv->save_uri = g_file_get_uri (file);
      g_object_unref (file);
    }
  else
    self->priv->save_uri = NULL;

  /* now release the application */
  g_application_release (G_APPLICATION (self));

  if (error != NULL)
    {
      g_critical ("Impossible to find a valid location to save the screenshot: %s",
                  error->message);
      g_error_free (error);

      if (screenshot_config->interactive)
        screenshot_show_dialog (NULL,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_OK,
                                _("Unable to capture a screenshot"),
                                _("Error creating file"));
      else
        {
          screenshot_play_sound_effect ("dialog-error", _("Unable to capture a screenshot"));
          if (screenshot_config->file != NULL)
            exit (EXIT_FAILURE);
        }

      return;
    }

  screenshot_play_sound_effect ("screen-capture", _("Screenshot taken"));

  if (screenshot_config->interactive)
    {
      self->priv->dialog = screenshot_dialog_new (self->priv->screenshot, self->priv->save_uri);
      g_signal_connect (self->priv->dialog->dialog,
                        "response",
                        G_CALLBACK (screenshot_dialog_response_cb),
                        self);
    }
  else
    {
      g_application_hold (G_APPLICATION (self));
      screenshot_save_to_file (self);
    }
}

static void
finish_prepare_screenshot (ScreenshotApplication *self,
                           GdkRectangle *rectangle)
{
  GdkPixbuf *screenshot;

  screenshot = screenshot_get_pixbuf (rectangle);

  if (screenshot == NULL)
    {
      g_critical ("Unable to capture a screenshot of any window");

      if (screenshot_config->interactive)
        screenshot_show_dialog (NULL,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_OK,
                                _("Unable to capture a screenshot"),
                                _("All possible methods failed"));
      else
        screenshot_play_sound_effect ("dialog-error", _("Unable to capture a screenshot"));

      g_application_release (G_APPLICATION (self));
      if (screenshot_config->file != NULL)
        exit (EXIT_FAILURE);

      return;
    }

  if (screenshot_config->take_window_shot)
    {
      switch (screenshot_config->border_effect[0])
        {
        case 's': /* shadow */
          screenshot_add_shadow (&screenshot);
          break;
        case 'b': /* border */
          screenshot_add_border (&screenshot);
          break;
        case 'n': /* none */
        default:
          break;
        }
    }

  self->priv->screenshot = screenshot;

  if (screenshot_config->copy_to_clipboard)
    {
      screenshot_save_to_clipboard (self);
      screenshot_play_sound_effect ("screen-capture", _("Screenshot taken"));

      g_application_release (G_APPLICATION (self));

      return;
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
      self->priv->save_uri = g_file_get_uri (screenshot_config->file);
      self->priv->should_overwrite = TRUE;
      screenshot_save_to_file (self);
    }
  else
    screenshot_build_filename_async (screenshot_config->save_dir, NULL, build_filename_ready_cb, self);
}

static void
rectangle_found_cb (GdkRectangle *rectangle,
                    gpointer user_data)
{
  ScreenshotApplication *self = user_data;

  if (rectangle != NULL)
    {
      finish_prepare_screenshot (self, rectangle);
    }
  else
    {
      /* user dismissed the area selection, possibly show the dialog again */
      g_application_release (G_APPLICATION (self));

      if (screenshot_config->interactive)
        screenshot_show_interactive_dialog (self);
    }
}

static gboolean
prepare_screenshot_timeout (gpointer user_data)
{
  ScreenshotApplication *self = user_data;

  if (screenshot_config->take_area_shot)
    screenshot_select_area_async (rectangle_found_cb, self);
  else
    finish_prepare_screenshot (self, NULL);

  screenshot_save_config ();

  return FALSE;
}

static void
screenshot_start (ScreenshotApplication *self)
{
  guint delay = screenshot_config->delay * 1000;

  /* hold the GApplication while doing the async screenshot op */
  g_application_hold (G_APPLICATION (self));

  if (screenshot_config->take_area_shot)
    delay = 0;

  /* HACK: give time to the dialog to actually disappear.
   * We don't have any way to tell when the compositor has finished 
   * re-drawing.
   */
  if (delay == 0 && screenshot_config->interactive)
    delay = 200;

  if (delay > 0)
    g_timeout_add (delay,
                   prepare_screenshot_timeout,
                   self);
  else
    g_idle_add (prepare_screenshot_timeout, self);
}

static gboolean
screenshot_application_local_command_line (GApplication *app,
                                           gchar ***arguments,
                                           gint *exit_status)
{
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
  const GOptionEntry entries[] = {
    { "clipboard", 'c', 0, G_OPTION_ARG_NONE, &clipboard_arg, N_("Send the grab directly to the clipboard"), NULL },
    { "window", 'w', 0, G_OPTION_ARG_NONE, &window_arg, N_("Grab a window instead of the entire screen"), NULL },
    { "area", 'a', 0, G_OPTION_ARG_NONE, &area_arg, N_("Grab an area of the screen instead of the entire screen"), NULL },
    { "include-border", 'b', 0, G_OPTION_ARG_NONE, &include_border_arg, N_("Include the window border with the screenshot"), NULL },
    { "remove-border", 'B', 0, G_OPTION_ARG_NONE, &disable_border_arg, N_("Remove the window border from the screenshot"), NULL },
    { "include-pointer", 'p', 0, G_OPTION_ARG_NONE, &include_pointer_arg, N_("Include the pointer with the screenshot"), NULL },
    { "delay", 'd', 0, G_OPTION_ARG_INT, &delay_arg, N_("Take screenshot after specified delay [in seconds]"), N_("seconds") },
    { "border-effect", 'e', 0, G_OPTION_ARG_STRING, &border_effect_arg, N_("Effect to add to the border (shadow, border or none)"), N_("effect") },
    { "interactive", 'i', 0, G_OPTION_ARG_NONE, &interactive_arg, N_("Interactively set options"), NULL },
    { "file", 'f', 0, G_OPTION_ARG_FILENAME, &file_arg, N_("Save screenshot directly to this file"), N_("filename") },
    { NULL },
  };

  GOptionContext *context;
  GError *error = NULL;
  gint argc = 0;
  gchar **argv = NULL;
  gboolean res;

  *exit_status = EXIT_SUCCESS;
  argv = *arguments;
  argc = g_strv_length (argv);

  context = g_option_context_new (_("Take a picture of the screen"));
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gtk_get_option_group (TRUE));

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_critical ("Unable to parse arguments: %s", error->message);
      g_error_free (error);

      *exit_status = EXIT_FAILURE;
      goto out;
    }

  res = screenshot_load_config (clipboard_arg,
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
      *exit_status = EXIT_FAILURE;
      goto out;
    }

  if (!g_application_register (app, NULL, &error)) 
    {
      g_printerr ("Could not register the application: %s\n", error->message);
      g_error_free (error);

      *exit_status = EXIT_FAILURE;
    }

 out:
  g_option_context_free (context);
  g_free (border_effect_arg);
  g_free (file_arg);

  return TRUE;	
}

static void
register_screenshooter_icon (GtkIconFactory * factory)
{
  GtkIconSource *source;
  GtkIconSet *icon_set;

  source = gtk_icon_source_new ();
  gtk_icon_source_set_icon_name (source, SCREENSHOOTER_ICON);

  icon_set = gtk_icon_set_new ();
  gtk_icon_set_add_source (icon_set, source);

  gtk_icon_factory_add (factory, SCREENSHOOTER_ICON, icon_set);
  gtk_icon_set_unref (icon_set);
  gtk_icon_source_free (source);
}

static void
screenshooter_init_stock_icons (void)
{
  GtkIconFactory *factory;

  factory = gtk_icon_factory_new ();
  gtk_icon_factory_add_default (factory);

  register_screenshooter_icon (factory);
  g_object_unref (factory);
}

static void
interactive_dialog_response_cb (GtkWidget *d,
                                gint response,
                                gpointer user_data)
{
  ScreenshotApplication *self = user_data;

  if (response != GTK_RESPONSE_HELP)
    gtk_widget_destroy (d);

  switch (response)
    {
    case GTK_RESPONSE_DELETE_EVENT:
      break;
    case GTK_RESPONSE_OK:
      screenshot_start (self);
      break;
    case GTK_RESPONSE_HELP:
      screenshot_display_help (GTK_WINDOW (d));
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}

static void
screenshot_show_interactive_dialog (ScreenshotApplication *self)
{
  GtkWidget *dialog;

  dialog = screenshot_interactive_dialog_new ();
  g_signal_connect (dialog, "response",
                    G_CALLBACK (interactive_dialog_response_cb), self);
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
    NULL
  };

  GList *windows = gtk_application_get_windows (GTK_APPLICATION (user_data));
  gtk_show_about_dialog (GTK_WINDOW (g_list_nth_data (windows, 0)),
                         "version", VERSION,
                         "authors", authors,
                         "program-name", _("Screenshot"),
                         "comments", _("Save images of your screen or individual windows"),
                         "logo-icon-name", "applets-screenshooter",
                         "translator-credits", _("translator-credits"),
                         "license-type", GTK_LICENSE_GPL_2_0,
                         "wrap-license", TRUE,
                         NULL);
}

static GActionEntry action_entries[] = {
  { "about", action_about, NULL, NULL, NULL },
  { "help", action_help, NULL, NULL, NULL },
  { "quit", action_quit, NULL, NULL, NULL }
};

static void
screenshot_application_startup (GApplication *app)
{
  ScreenshotApplication *self = SCREENSHOT_APPLICATION (app);
  GtkBuilder *builder;
  GMenuModel *menu;

  G_APPLICATION_CLASS (screenshot_application_parent_class)->startup (app);

  gtk_window_set_default_icon_name (SCREENSHOOTER_ICON);
  screenshooter_init_stock_icons ();

  g_action_map_add_action_entries (G_ACTION_MAP (self), action_entries,
                                   G_N_ELEMENTS (action_entries), self);

  builder = gtk_builder_new ();
  gtk_builder_add_from_resource (builder, "/org/gnome/screenshot/screenshot-app-menu.ui", NULL);
  menu = G_MENU_MODEL (gtk_builder_get_object (builder, "app-menu"));
  gtk_application_set_app_menu (GTK_APPLICATION (self), menu);

  g_object_unref (builder);
  g_object_unref (menu);

  /* interactive mode: trigger the dialog and wait for the response */
  if (screenshot_config->interactive)
    screenshot_show_interactive_dialog (self);
  else
    screenshot_start (self);
}

static void
screenshot_application_finalize (GObject *object)
{
  ScreenshotApplication *self = SCREENSHOT_APPLICATION (object);

  g_clear_object (&self->priv->screenshot);
  g_free (self->priv->icc_profile_base64);
  g_free (self->priv->save_uri);

  G_OBJECT_CLASS (screenshot_application_parent_class)->finalize (object);
}

static void
screenshot_application_class_init (ScreenshotApplicationClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GApplicationClass *aclass = G_APPLICATION_CLASS (klass);

  oclass->finalize = screenshot_application_finalize;

  aclass->local_command_line = screenshot_application_local_command_line;
  aclass->startup = screenshot_application_startup;

  g_type_class_add_private (klass, sizeof (ScreenshotApplicationPriv));
}

static void
screenshot_application_init (ScreenshotApplication *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, SCREENSHOT_TYPE_APPLICATION,
                                            ScreenshotApplicationPriv);
}

ScreenshotApplication *
screenshot_application_new (void)
{
  return g_object_new (SCREENSHOT_TYPE_APPLICATION, 
                       "application-id", "org.gnome.Screenshot",
                       NULL);
}
