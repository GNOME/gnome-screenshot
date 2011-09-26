/* gnome-screenshot.c - Take a screenshot of the desktop
 *
 * Copyright (C) 2001 Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2006 Emmanuele Bassi <ebassi@gnome.org>
 * Copyright (C) 2008 Cosimo Cecchi <cosimoc@gnome.org>
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

/* THERE ARE NO FEATURE REQUESTS ALLOWED */
/* IF YOU WANT YOUR OWN FEATURE -- WRITE THE DAMN THING YOURSELF (-: */
/* MAYBE I LIED... -jrb */

#include <config.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <pwd.h>
#include <X11/Xutil.h>
#include <canberra-gtk.h>

#include "screenshot-config.h"
#include "screenshot-filename-builder.h"
#include "screenshot-interactive-dialog.h"
#include "screenshot-shadow.h"
#include "screenshot-utils.h"
#include "screenshot-save.h"
#include "screenshot-dialog.h"
#include "cheese-flash.h"

#define SCREENSHOOTER_ICON "applets-screenshooter"

#define LAST_SAVE_DIRECTORY_KEY "last-save-directory"

static GdkPixbuf *screenshot = NULL;

/* Global variables*/
static char *temporary_file = NULL;
static gboolean save_immediately = FALSE;
static CheeseFlash *flash = NULL;
static GDBusConnection *connection = NULL;

/* some local prototypes */
static void  save_done_notification (gpointer   data);

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
save_callback (GObject *source,
               GAsyncResult *res,
               gpointer user_data)
{
  ScreenshotDialog *dialog = user_data;
  GtkWidget *toplevel;
  GError *error = NULL;
  
  toplevel = screenshot_dialog_get_toplevel (dialog);
  screenshot_dialog_set_busy (dialog, FALSE);

  g_dbus_connection_call_finish (connection, res, &error);

  if (error != NULL)
    {
      /* we had an error, display a dialog to the user and let him choose
       * another name/location to save the screenshot.
       */
      GtkWidget *error_dialog;
      char *folder;
      
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
      g_error_free (error);
    }
  else
    {
      save_folder_to_settings (dialog);
      set_recent_entry (dialog);
      gtk_widget_destroy (toplevel);
      
      /* we're done, stop the mainloop now */
      gtk_main_quit ();
    }
}

static void
try_to_save (ScreenshotDialog *dialog)
{
  gchar *target_folder, *target_filename, *source_uri;
  GFile *source_file;

  g_assert (temporary_file);

  screenshot_dialog_set_busy (dialog, TRUE);

  source_file = g_file_new_for_path (temporary_file);
  source_uri = g_file_get_uri (source_file);

  target_folder = screenshot_dialog_get_folder (dialog);
  target_filename = screenshot_dialog_get_filename (dialog);

  g_dbus_connection_call (connection,
                          "org.gnome.Nautilus",
                          "/org/gnome/Nautilus",
                          "org.gnome.Nautilus.FileOperations",
                          "CopyFile",
                          g_variant_new ("(ssss)",
                                         source_uri,
                                         target_filename,
                                         target_folder,
                                         target_filename),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          save_callback,
                          dialog);

  g_object_unref (source_file);

  g_free (source_uri);
  g_free (target_folder);
  g_free (target_filename);
}

static void
save_done_notification (gpointer data)
{
  ScreenshotDialog *dialog = data;

  temporary_file = g_strdup (screenshot_save_get_filename ());
  screenshot_dialog_enable_dnd (dialog);

  if (save_immediately)
    {
      GtkWidget *toplevel;

      toplevel = screenshot_dialog_get_toplevel (dialog);
      gtk_dialog_response (GTK_DIALOG (toplevel), GTK_RESPONSE_OK);
    }
}

static void
screenshot_dialog_response_cb (GtkDialog *d,
                               gint response_id,
                               ScreenshotDialog *dialog)
{
  if (response_id == GTK_RESPONSE_HELP)
    {
      screenshot_display_help (GTK_WINDOW (d));
    }
  else if (response_id == GTK_RESPONSE_OK)
    {
      if (temporary_file == NULL)
        {
          save_immediately = TRUE;
          screenshot_dialog_set_busy (dialog, TRUE);
        }
      else
        {
          /* we've saved the temporary file, lets try to copy it to the
           * correct location.
           */
          try_to_save (dialog);
        }
    }
  else if (response_id == SCREENSHOT_RESPONSE_COPY)
    {
      GtkClipboard *clipboard;
      GdkPixbuf    *screenshot;

      clipboard = gtk_clipboard_get_for_display (gtk_widget_get_display (GTK_WIDGET (d)),
                                                 GDK_SELECTION_CLIPBOARD);
      screenshot = screenshot_dialog_get_screenshot (dialog);
      gtk_clipboard_set_image (clipboard, screenshot);
    }
  else /* dialog was canceled */
    {
      gtk_widget_destroy (GTK_WIDGET (d));
      gtk_main_quit ();
    }
}

static void
run_dialog (ScreenshotDialog *dialog)
{
  GtkWidget *toplevel;

  toplevel = screenshot_dialog_get_toplevel (dialog);
  
  gtk_widget_show (toplevel);
  
  g_signal_connect (toplevel,
                    "response",
                    G_CALLBACK (screenshot_dialog_response_cb),
                    dialog);
}
                               
static void
build_filename_ready_cb (GObject *source,
                         GAsyncResult *res,
                         gpointer user_data)
{
  ScreenshotDialog *dialog;
  GdkPixbuf *screenshot = user_data;
  gchar *save_uri;
  GError *error = NULL;

  save_uri = screenshot_build_filename_finish (res, &error);

  if (error != NULL)
    {
      g_critical ("Impossible to find a valid location to save the screenshot: %s",
                  error->message);
      g_error_free (error);

      exit(1);
    }

  dialog = screenshot_dialog_new (screenshot, save_uri);
  screenshot_save_start (screenshot, save_done_notification, dialog);
  run_dialog (dialog);

  g_free (save_uri);
}

static void
play_sound_effect (GdkWindow *window)
{
  ca_context *c;
  ca_proplist *p = NULL;
  int res;

  c = ca_gtk_context_get ();

  res = ca_proplist_create (&p);
  if (res < 0)
    goto done;

  res = ca_proplist_sets (p, CA_PROP_EVENT_ID, "screen-capture");
  if (res < 0)
    goto done;

  res = ca_proplist_sets (p, CA_PROP_EVENT_DESCRIPTION, _("Screenshot taken"));
  if (res < 0)
    goto done;

  if (window != NULL)
    {
      res = ca_proplist_setf (p,
                              CA_PROP_WINDOW_X11_XID,
                              "%lu",
                              (unsigned long) GDK_WINDOW_XID (window));
      if (res < 0)
        goto done;
    }

  ca_context_play_full (c, 0, p, NULL, NULL);

 done:
  if (p != NULL)
    ca_proplist_destroy (p);

}

static gchar *
get_profile_for_window (GdkWindow *window)
{
  GError *error = NULL;
  guint xid;
  gchar *icc_profile = NULL;
  GVariant *args = NULL;
  GVariant *response = NULL;
  GVariant *response_child = NULL;
  GVariantIter *iter = NULL;

  /* get color profile */
  xid = GDK_WINDOW_XID (window);
  args = g_variant_new ("(u)", xid),
  response = g_dbus_connection_call_sync (connection,
                                          "org.gnome.ColorManager",
                                          "/org/gnome/ColorManager",
                                          "org.gnome.ColorManager",
                                          "GetProfileForWindow",
                                          args,
                                          G_VARIANT_TYPE ("(s)"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1, NULL, &error);
  if (response == NULL)
    {
      /* not a warning, as GCM might not be installed / running */
      g_debug ("The GetProfileForWindow request failed: %s",
               error->message);
      g_error_free (error);
      goto out;
    }

  /* get icc profile filename */
  response_child = g_variant_get_child_value (response, 0);
  icc_profile = g_variant_dup_string (response_child, NULL);
out:
  if (iter != NULL)
    g_variant_iter_free (iter);
  if (args != NULL)
    g_variant_unref (args);
  if (response != NULL)
    g_variant_unref (response);
  return icc_profile;
}

static GdkWindow *
find_current_window (void)
{
  GdkWindow *window = NULL;

  if (!screenshot_grab_lock ())
    exit (0);

  if (screenshot_config->take_window_shot)
    {
      window = screenshot_find_current_window ();

      if (window == NULL)
        screenshot_config->take_window_shot = FALSE;
    }

  if (window == NULL)
    window = gdk_get_default_root_window ();

  return window;
}

static void
finish_prepare_screenshot (GdkRectangle *rectangle)
{  
  char *icc_profile_filename;
  guchar *icc_profile;
  gsize icc_profile_size;
  char *icc_profile_base64;
  gboolean ret;
  GError *error = NULL;
  GdkRectangle rect;
  GdkWindow *window;

  window = find_current_window ();
  screenshot = screenshot_get_pixbuf (window, rectangle);

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

  /* release now the lock, it was acquired when we were finding the window */
  screenshot_release_lock ();

  if (screenshot == NULL)
    {
      screenshot_show_error_dialog (NULL,
                                    _("Unable to take a screenshot of the current window"),
                                    NULL);
      exit (1);
    }

  flash = cheese_flash_new ();

  if (rectangle != NULL)
    rect = *rectangle;
  else
    screenshot_get_window_rect (window, &rect);

  cheese_flash_fire (flash, &rect);
  play_sound_effect (window);

  if (screenshot_config->copy_to_clipboard)
    {
      GtkClipboard *clipboard;

      clipboard = gtk_clipboard_get_for_display (gdk_display_get_default (),
                                                 GDK_SELECTION_CLIPBOARD);
      gtk_clipboard_set_image (clipboard, screenshot);
      gtk_main_quit ();
    }
  else
    {
      /* load ICC profile */
      if (screenshot_config->include_icc_profile)
        {
          icc_profile_filename = get_profile_for_window (window);
          if (icc_profile_filename != NULL)
            {
              ret = g_file_get_contents (icc_profile_filename,
                                         (gchar **) &icc_profile,
                                         &icc_profile_size,
                                         &error);
              if (ret)
                {
                  icc_profile_base64 = g_base64_encode (icc_profile,
                                                        icc_profile_size);

                  /* use this profile for saving the image */
                  screenshot_set_icc_profile (icc_profile_base64);
                  g_free (icc_profile);
                  g_free (icc_profile_base64);
                }
              else
                {
                  g_warning ("could not open ICC file: %s",
                             error->message);
                  g_error_free (error);
                }
              g_free (icc_profile_filename);
            }
        }

      screenshot_build_filename_async (screenshot_config->last_save_dir,
                                       build_filename_ready_cb, screenshot);
    }
}

static void
rectangle_found_cb (GdkRectangle *rectangle)
{
  finish_prepare_screenshot (rectangle);
}

static void
prepare_screenshot (void)
{
  if (screenshot_config->take_area_shot)
    screenshot_select_area_async (rectangle_found_cb);
  else
    finish_prepare_screenshot (NULL);
}

static gboolean
prepare_screenshot_timeout (gpointer data)
{
  prepare_screenshot ();
  screenshot_save_config ();

  return FALSE;
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
init_dbus_session (void)
{
  GError *error = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

  if (error != NULL)
    {
      g_critical ("Unable to connect to the session bus: %s",
                  error->message);
      g_error_free (error);

      exit (1);
    }
}

/* main */
int
main (int argc, char *argv[])
{
  GOptionContext *context;
  gboolean clipboard_arg = FALSE;
  gboolean window_arg = FALSE;
  gboolean area_arg = FALSE;
  gboolean include_border_arg = FALSE;
  gboolean disable_border_arg = FALSE;
  gboolean interactive_arg = FALSE;
  gchar *border_effect_arg = NULL;
  guint delay_arg = 0;
  GError *error = NULL;
  gboolean res;

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

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_thread_init (NULL);

  context = g_option_context_new (_("Take a picture of the screen"));
  g_option_context_set_ignore_unknown_options (context, FALSE);
  g_option_context_set_help_enabled (context, TRUE);
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gtk_get_option_group (TRUE));

  g_option_context_parse (context, &argc, &argv, &error);

  if (error) {
    g_critical ("Unable to parse arguments: %s", error->message);
    g_error_free (error);
    g_option_context_free (context);
    exit (1);
  }

  g_option_context_free (context);

  res = screenshot_load_config (clipboard_arg,
                                window_arg,
                                area_arg,
                                include_border_arg,
                                disable_border_arg,
                                border_effect_arg,
                                delay_arg);

  if (!res)
    exit (1);

  gtk_window_set_default_icon_name (SCREENSHOOTER_ICON);
  screenshooter_init_stock_icons ();

  init_dbus_session ();

  /* interactive mode overrides everything */
  if (interactive_arg)
    {
      GtkWidget *dialog;
      gint response;

      dialog = screenshot_interactive_dialog_new ();
      response = gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      switch (response)
        {
        case GTK_RESPONSE_DELETE_EVENT:
        case GTK_RESPONSE_CANCEL:
          return EXIT_SUCCESS;
        case GTK_RESPONSE_OK:
          break;
        default:
          g_assert_not_reached ();
          break;
        }
    }

  if (((screenshot_config->delay > 0 && interactive_arg) || delay_arg > 0) &&
      !screenshot_config->take_area_shot)
    {      
      g_timeout_add (screenshot_config->delay * 1000,
		     prepare_screenshot_timeout,
		     NULL);
    }
  else
    {
      if (interactive_arg)
        {
          /* HACK: give time to the dialog to actually disappear.
           * We don't have any way to tell when the compositor has finished 
           * re-drawing.
           */
          g_timeout_add (200,
                         prepare_screenshot_timeout, NULL);
        }
      else
        g_idle_add (prepare_screenshot_timeout, NULL);
    }

  gtk_main ();

  g_clear_object (&flash);
  g_clear_object (&connection);

  return EXIT_SUCCESS;
}
