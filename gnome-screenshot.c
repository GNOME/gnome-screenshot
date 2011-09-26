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

#include "gnome-screenshot.h"
#include "screenshot-area-selection.h"
#include "screenshot-config.h"
#include "screenshot-filename-builder.h"
#include "screenshot-interactive-dialog.h"
#include "screenshot-shadow.h"
#include "screenshot-utils.h"
#include "screenshot-dialog.h"
#include "cheese-flash.h"

#define SCREENSHOOTER_ICON "applets-screenshooter"

#define LAST_SAVE_DIRECTORY_KEY "last-save-directory"

static GdkPixbuf *screenshot = NULL;

/* Global variables*/
static CheeseFlash *flash = NULL;
static gchar *icc_profile_base64 = NULL;

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

  /* we're done, stop the mainloop now */
  gtk_main_quit ();
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
  GFileOutputStream *os;
  GError *error = NULL;
  ScreenshotDialog *dialog = user_data;

  os = g_file_create_finish (G_FILE (source), res, &error);

  if (error != NULL)
    {
      save_file_failed_error (dialog, error);
      g_error_free (error);
      return;
    }

  if (icc_profile_base64 != NULL)
    gdk_pixbuf_save_to_stream_async (screenshot,
                                     G_OUTPUT_STREAM (os),
                                     "png", NULL,
                                     save_pixbuf_ready_cb, dialog,
                                     "icc-profile", icc_profile_base64,
                                     "tEXt::Software", "gnome-screenshot",
                                     NULL);
  else
    gdk_pixbuf_save_to_stream_async (screenshot,
                                     G_OUTPUT_STREAM (os),
                                     "png", NULL,
                                     save_pixbuf_ready_cb, dialog,
                                     "tEXt::Software", "gnome-screenshot",
                                     NULL);

  g_object_unref (os);
}

static void
try_to_save (ScreenshotDialog *dialog)
{
  gchar *target_uri;
  GFile *target_file;

  screenshot_dialog_set_busy (dialog, TRUE);

  target_uri = screenshot_dialog_get_uri (dialog);
  target_file = g_file_new_for_uri (target_uri);

  g_file_create_async (target_file,
                       G_FILE_CREATE_NONE,
                       G_PRIORITY_DEFAULT,
                       NULL,
                       save_file_create_ready_cb, dialog);

  g_object_unref (target_file);
  g_free (target_uri);
}

static void
screenshot_save_to_clipboard (void)
{
  GtkClipboard *clipboard;

  clipboard = gtk_clipboard_get_for_display (gdk_display_get_default (),
                                             GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_image (clipboard, screenshot);
}

static void
screenshot_dialog_response_cb (GtkDialog *d,
                               gint response_id,
                               ScreenshotDialog *dialog)
{
  switch (response_id)
    {
    case GTK_RESPONSE_HELP:
      screenshot_display_help (GTK_WINDOW (d));
      break;
    case GTK_RESPONSE_OK:
      try_to_save (dialog);
      break;
    case SCREENSHOT_RESPONSE_COPY:
      screenshot_save_to_clipboard ();
      break;
    default:
      gtk_widget_destroy (GTK_WIDGET (d));
      gtk_main_quit ();
      break;
    }
}
                               
static void
build_filename_ready_cb (GObject *source,
                         GAsyncResult *res,
                         gpointer user_data)
{
  GtkWidget *toplevel;
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
  toplevel = screenshot_dialog_get_toplevel (dialog);
  gtk_widget_show (toplevel);
  
  g_signal_connect (toplevel,
                    "response",
                    G_CALLBACK (screenshot_dialog_response_cb),
                    dialog);

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

static void
screenshot_ensure_icc_profile (GdkWindow *window)
{
  char *icc_profile_filename;
  guchar *icc_profile;
  gsize icc_profile_size;
  gboolean ret;
  GError *error = NULL;

  if (!screenshot_config->include_icc_profile)
    return;

  /* load ICC profile */
  icc_profile_filename = get_profile_for_window (window);
  if (icc_profile_filename == NULL)
    return;


  ret = g_file_get_contents (icc_profile_filename,
                             (gchar **) &icc_profile,
                             &icc_profile_size,
                             &error);
  if (ret)
    {
      icc_profile_base64 = g_base64_encode (icc_profile,
                                            icc_profile_size);

      g_free (icc_profile);
    }
  else
    {
      g_warning ("could not open ICC file: %s",
                 error->message);
      g_error_free (error);
    }

  g_free (icc_profile_filename);
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
      screenshot_save_to_clipboard ();
      gtk_main_quit ();

      return;
    }

  screenshot_ensure_icc_profile (window);
  screenshot_build_filename_async (screenshot_config->last_save_dir,
                                   build_filename_ready_cb, screenshot);
}

static void
rectangle_found_cb (GdkRectangle *rectangle)
{
  finish_prepare_screenshot (rectangle);
}

static gboolean
prepare_screenshot_timeout (gpointer data)
{
  if (screenshot_config->take_area_shot)
    screenshot_select_area_async (rectangle_found_cb);
  else
    finish_prepare_screenshot (NULL);

  screenshot_save_config ();

  return FALSE;
}

static void
screenshot_start (gboolean interactive)
{
  guint delay = screenshot_config->delay * 1000;

  /* HACK: give time to the dialog to actually disappear.
   * We don't have any way to tell when the compositor has finished 
   * re-drawing.
   */
  if (delay == 0 && interactive)
    delay = 200;

  if (delay > 0)
    g_timeout_add (delay,
                   prepare_screenshot_timeout,
                   NULL);
  else
    g_idle_add (prepare_screenshot_timeout, NULL);
}

static void
interactive_dialog_response_cb (GtkWidget *d,
                                gint response,
                                gpointer _user_data)
{
  gtk_widget_destroy (d);

  switch (response)
    {
    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_CANCEL:
      gtk_main_quit ();
      break;
    case GTK_RESPONSE_OK:
      screenshot_start (TRUE);
      break;
    default:
      g_assert_not_reached ();
      break;
    }
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

static gboolean
init_dbus_session (void)
{
  GError *error = NULL;
  gboolean retval = TRUE;

  g_assert (connection == NULL);

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);

  if (error != NULL)
    {
      g_critical ("Unable to connect to the session bus: %s",
                  error->message);
      g_error_free (error);
      retval = FALSE;
    }

  return retval;
}

static gboolean
screenshot_app_init (void)
{
  gtk_window_set_default_icon_name (SCREENSHOOTER_ICON);
  screenshooter_init_stock_icons ();

  return init_dbus_session ();
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

  if (error)
    {
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

  if (!res || !screenshot_app_init ())
    exit (1);

  /* interactive mode: trigger the dialog and wait for the response */
  if (interactive_arg)
    {
      GtkWidget *dialog;

      dialog = screenshot_interactive_dialog_new ();
      gtk_widget_show (dialog);
      g_signal_connect (dialog, "response",
                        G_CALLBACK (interactive_dialog_response_cb), NULL);
    }
  else
    {
      screenshot_start (FALSE);
    }

  gtk_main ();

  g_clear_object (&flash);
  g_clear_object (&connection);

  return EXIT_SUCCESS;
}
