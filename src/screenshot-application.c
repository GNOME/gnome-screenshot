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

static ScreenshotApplication *_app_singleton = NULL;

struct _ScreenshotApplicationPriv {
  GDBusConnection *connection;

  gchar *icc_profile_base64;
  GdkPixbuf *screenshot;

  ScreenshotDialog *dialog;
};

static void
save_folder_to_settings (ScreenshotDialog *dialog)
{
  char *folder;

  folder = screenshot_dialog_get_folder (dialog);
  g_settings_set_string (screenshot_config->settings,
                         LAST_SAVE_DIRECTORY_KEY, folder);

  g_free (folder);
}

static void
set_recent_entry (ScreenshotDialog *dialog)
{
  char *uri, *app_exec = NULL;
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

  uri = screenshot_dialog_get_uri (dialog);
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

  gtk_recent_manager_add_full (recent, uri, &recent_data);

  g_object_unref (app);
  g_free (app_exec);
  g_free (uri);
}

static void
error_dialog_response_cb (GtkDialog *d,
                          gint response,
                          ScreenshotDialog *dialog)
{
  gtk_widget_destroy (GTK_WIDGET (d));

  screenshot_dialog_focus_entry (dialog);
}

static void
save_file_failed_error (ScreenshotDialog *dialog,
                        GError *error)
{
  GtkWidget *toplevel;
  GtkWidget *error_dialog;
  char *folder;

  toplevel = screenshot_dialog_get_toplevel (dialog);
  screenshot_dialog_set_busy (dialog, FALSE);

  /* we had an error, display a dialog to the user and let him choose
   * another name/location to save the screenshot.
   */      
  folder = screenshot_dialog_get_folder (dialog);
  error_dialog = gtk_message_dialog_new (GTK_WINDOW (toplevel),
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_ERROR,
                                         GTK_BUTTONS_OK,
                                         _("Error while saving screenshot"));

  /* translators: first %s is the folder URI, second %s is the VFS error */
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (error_dialog),
                                            _("Impossible to save the screenshot "
                                              "to %s.\n Error was %s.\n Please choose another "
                                              "location and retry."), folder, error->message);
  gtk_widget_show (error_dialog);
  g_signal_connect (error_dialog,
                    "response",
                    G_CALLBACK (error_dialog_response_cb),
                    dialog);

  g_free (folder);
}

static void
save_file_completed (ScreenshotDialog *dialog)
{
  GtkWidget *toplevel;

  toplevel = screenshot_dialog_get_toplevel (dialog);

  save_folder_to_settings (dialog);
  set_recent_entry (dialog);
  gtk_widget_destroy (toplevel);
}

static void
save_pixbuf_ready_cb (GObject *source,
                      GAsyncResult *res,
                      gpointer user_data)
{
  GError *error = NULL;
  ScreenshotDialog *dialog = user_data;

  gdk_pixbuf_save_to_stream_finish (res, &error);

  if (error != NULL)
    {
      save_file_failed_error (dialog, error);
      g_error_free (error);
      return;
    }

  save_file_completed (dialog);
}

static void
save_file_create_ready_cb (GObject *source,
                           GAsyncResult *res,
                           gpointer user_data)
{
  ScreenshotApplication *self = user_data;
  ScreenshotDialog *dialog = self->priv->dialog;
  GFileOutputStream *os;
  GError *error = NULL;

  os = g_file_create_finish (G_FILE (source), res, &error);

  if (error != NULL)
    {
      save_file_failed_error (dialog, error);
      g_error_free (error);
      return;
    }

  if (self->priv->icc_profile_base64 != NULL)
    gdk_pixbuf_save_to_stream_async (self->priv->screenshot,
                                     G_OUTPUT_STREAM (os),
                                     "png", NULL,
                                     save_pixbuf_ready_cb, dialog,
                                     "icc-profile", self->priv->icc_profile_base64,
                                     "tEXt::Software", "gnome-screenshot",
                                     NULL);
  else
    gdk_pixbuf_save_to_stream_async (self->priv->screenshot,
                                     G_OUTPUT_STREAM (os),
                                     "png", NULL,
                                     save_pixbuf_ready_cb, dialog,
                                     "tEXt::Software", "gnome-screenshot",
                                     NULL);

  g_object_unref (os);
}

static void
try_to_save (ScreenshotApplication *self)
{
  ScreenshotDialog *dialog = self->priv->dialog;
  gchar *target_uri;
  GFile *target_file;

  screenshot_dialog_set_busy (dialog, TRUE);

  target_uri = screenshot_dialog_get_uri (dialog);
  target_file = g_file_new_for_uri (target_uri);

  g_file_create_async (target_file,
                       G_FILE_CREATE_NONE,
                       G_PRIORITY_DEFAULT,
                       NULL,
                       save_file_create_ready_cb, self);

  g_object_unref (target_file);
  g_free (target_uri);
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
      try_to_save (self);
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
  GtkWidget *toplevel;
  ScreenshotDialog *dialog;
  gchar *save_uri;
  GError *error = NULL;

  save_uri = screenshot_build_filename_finish (res, &error);

  /* now release the application */
  g_application_release (G_APPLICATION (self));

  if (error != NULL)
    {
      g_critical ("Impossible to find a valid location to save the screenshot: %s",
                  error->message);
      g_error_free (error);

      if (screenshot_config->interactive)
        screenshot_show_error_dialog (NULL,
                                      _("Unable to capture a screenshot"),
                                      _("Error creating file"));
      else
        screenshot_play_sound_effect ("dialog-error", _("Unable to capture a screenshot"));

      return;
    }

  screenshot_play_sound_effect ("screen-capture", _("Screenshot taken"));

  self->priv->dialog = screenshot_dialog_new (self->priv->screenshot, save_uri);
  toplevel = screenshot_dialog_get_toplevel (self->priv->dialog);
  gtk_widget_show (toplevel);

  gtk_application_add_window (GTK_APPLICATION (self), GTK_WINDOW (toplevel));
  
  g_signal_connect (toplevel,
                    "response",
                    G_CALLBACK (screenshot_dialog_response_cb),
                    self);

  g_free (save_uri);
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
        screenshot_show_error_dialog (NULL,
                                      _("Unable to capture a screenshot"),
                                      _("All possible methods failed"));
      else
        screenshot_play_sound_effect ("dialog-error", _("Unable to capture a screenshot"));

      g_application_release (G_APPLICATION (self));

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
  screenshot_build_filename_async (screenshot_config->last_save_dir,
                                   build_filename_ready_cb, self);
}

static void
rectangle_found_cb (GdkRectangle *rectangle,
                    gpointer user_data)
{
  ScreenshotApplication *self = user_data;

  if (rectangle != NULL)
    finish_prepare_screenshot (self, rectangle);
  else
    /* user dismissed the rectangle with Esc, no error; just quit */
    g_application_release (G_APPLICATION (self));
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
  gboolean interactive_arg = FALSE;
  gchar *border_effect_arg = NULL;
  guint delay_arg = 0;
  const GOptionEntry entries[] = {
    { "clipboard", 'c', 0, G_OPTION_ARG_NONE, &clipboard_arg, N_("Send the grab directly to the clipboard"), NULL },
    { "window", 'w', 0, G_OPTION_ARG_NONE, &window_arg, N_("Grab a window instead of the entire screen"), NULL },
    { "area", 'a', 0, G_OPTION_ARG_NONE, &area_arg, N_("Grab an area of the screen instead of the entire screen"), NULL },
    { "include-border", 'b', 0, G_OPTION_ARG_NONE, &include_border_arg, N_("Include the window border with the screenshot"), NULL },
    { "remove-border", 'B', 0, G_OPTION_ARG_NONE, &disable_border_arg, N_("Remove the window border from the screenshot"), NULL },
    { "delay", 'd', 0, G_OPTION_ARG_INT, &delay_arg, N_("Take screenshot after specified delay [in seconds]"), N_("seconds") },
    { "border-effect", 'e', 0, G_OPTION_ARG_STRING, &border_effect_arg, N_("Effect to add to the border (shadow, border or none)"), N_("effect") },
    { "interactive", 'i', 0, G_OPTION_ARG_NONE, &interactive_arg, N_("Interactively set options"), NULL },
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
                                border_effect_arg,
                                delay_arg,
                                interactive_arg);

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

  gtk_widget_destroy (d);

  switch (response)
    {
    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_CANCEL:
      break;
    case GTK_RESPONSE_OK:
      screenshot_start (self);
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}

static ScreenshotApplication *
get_singleton (void)
{
  if (_app_singleton == NULL)
    _app_singleton = g_object_new (SCREENSHOT_TYPE_APPLICATION, 
                                   "application-id", "org.gnome.Screenshot",
                                   NULL);

  return _app_singleton;
}

static void
screenshot_application_startup (GApplication *app)
{
  ScreenshotApplication *self = SCREENSHOT_APPLICATION (app);
  GError *error = NULL;

  G_APPLICATION_CLASS (screenshot_application_parent_class)->startup (app);

  gtk_window_set_default_icon_name (SCREENSHOOTER_ICON);
  screenshooter_init_stock_icons ();

  self->priv->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

  if (error != NULL)
    {
      g_critical ("Unable to connect to the session bus: %s",
                  error->message);
      g_error_free (error);

      return;
    }

  /* interactive mode: trigger the dialog and wait for the response */
  if (screenshot_config->interactive)
    {
      GtkWidget *dialog;

      dialog = screenshot_interactive_dialog_new ();
      gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (dialog));
      gtk_widget_show (dialog);
      g_signal_connect (dialog, "response",
                        G_CALLBACK (interactive_dialog_response_cb), self);
    }
  else
    {
      screenshot_start (self);
    }
}

static void
screenshot_application_finalize (GObject *object)
{
  ScreenshotApplication *self = SCREENSHOT_APPLICATION (object);

  g_clear_object (&self->priv->connection);
  g_clear_object (&self->priv->screenshot);
  g_free (self->priv->icc_profile_base64);

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

GDBusConnection *
screenshot_application_get_session_bus (void)
{
  ScreenshotApplication *self = get_singleton ();
  return self->priv->connection;
}

ScreenshotApplication *
screenshot_application_get (void)
{
  return get_singleton ();
}