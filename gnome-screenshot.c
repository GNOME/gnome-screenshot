/* gnome-screenshot.c - Take a screenshot of the desktop
 *
 * Copyright (C) 2001 Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2006 Emmanuele Bassi <ebassi@gnome.org>
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
#include <gnome.h>
#include <gconf/gconf-client.h>
#include <glade/glade.h>
#include <gdk/gdkx.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <locale.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <X11/Xutil.h>

#include "screenshot-shadow.h"
#include "screenshot-utils.h"
#include "screenshot-save.h"
#include "screenshot-dialog.h"
#include "screenshot-xfer.h"

#define SCREENSHOOTER_ICON "applets-screenshooter"

#define GNOME_SCREENSHOT_GCONF  "/apps/gnome-screenshot"
#define INCLUDE_BORDER_KEY      GNOME_SCREENSHOT_GCONF "/include_border"
#define LAST_SAVE_DIRECTORY_KEY GNOME_SCREENSHOT_GCONF "/last_save_directory"
#define BORDER_EFFECT_KEY       GNOME_SCREENSHOT_GCONF "/border_effect"

enum
{
  COLUMN_NICK,
  COLUMN_LABEL,
  COLUMN_ID,

  N_COLUMNS
};

typedef enum {
  SCREENSHOT_EFFECT_NONE,
  SCREENSHOT_EFFECT_SHADOW,
  SCREENSHOT_EFFECT_BORDER
} ScreenshotEffectType;

static GdkPixbuf *screenshot = NULL;

/* Global variables*/
static char *last_save_dir = NULL;
static char *window_title = NULL;
static char *temporary_file = NULL;
static gboolean save_immediately = FALSE;

/* Options */
static gboolean take_window_shot = FALSE;
static gboolean include_border = FALSE;
static char *border_effect = NULL;
static guint delay = 0;

/* some local prototypes */
static void display_help           (GtkWindow *parent);
static void save_done_notification (gpointer   data);
static char *get_desktop_dir (void);

static GtkWidget *border_check = NULL;
static GtkWidget *effect_combo = NULL;

static void
display_help (GtkWindow *parent)
{
  GError *error = NULL;

  gnome_help_display_desktop (NULL,
                              "user-guide",
			      "user-guide.xml",
                              "goseditmainmenu-53",
			      &error);

  if (error)
    {
      screenshot_show_gerror_dialog (parent,
                                     _("Error loading the help page"),
                                     error);
    }
}

static void
interactive_dialog_response_cb (GtkDialog *dialog,
                                gint       response,
                                gpointer   user_data)
{
  switch (response)
    {
    case GTK_RESPONSE_HELP:
      g_signal_stop_emission_by_name (dialog, "response");
      display_help (GTK_WINDOW (dialog));
      break;
    default:
      gtk_widget_hide (GTK_WIDGET (dialog));
      break;
    }
}

static void
target_toggled_cb (GtkToggleButton *button,
                   gpointer         data)
{
  gboolean window_selected = (GPOINTER_TO_INT (data) == TRUE ? TRUE : FALSE);

  if (gtk_toggle_button_get_active (button))
    {
      take_window_shot = window_selected;
      
      gtk_widget_set_sensitive (border_check, take_window_shot);
      gtk_widget_set_sensitive (effect_combo, take_window_shot);
    }
}

static void
delay_spin_value_changed_cb (GtkSpinButton *button)
{
  delay = gtk_spin_button_get_value_as_int (button);
}

static void
include_border_toggled_cb (GtkToggleButton *button,
                           gpointer         data)
{
  include_border = gtk_toggle_button_get_active (button);
}

static void
effect_combo_changed_cb (GtkComboBox *combo,
                         gpointer     user_data)
{
  GtkTreeIter iter;

  if (gtk_combo_box_get_active_iter (combo, &iter))
    {
      GtkTreeModel *model;
      gchar *effect;

      model = gtk_combo_box_get_model (combo);
      gtk_tree_model_get (model, &iter, COLUMN_NICK, &effect, -1);

      g_assert (effect != NULL);

      g_free (border_effect);
      border_effect = effect; /* gets free'd later */
    }
}

typedef struct {
  ScreenshotEffectType id;
  const gchar *label;
  const gchar *nick;
} ScreenshotEffect;

/* Translators:
 * these are the names of the effects available which will be
 * displayed inside a combo box in interactive mode for the user
 * to chooser.
 */
static const ScreenshotEffect effects[] = {
  { SCREENSHOT_EFFECT_NONE, N_("None"), "none" },
  { SCREENSHOT_EFFECT_SHADOW, N_("Drop shadow"), "shadow" },
  { SCREENSHOT_EFFECT_BORDER, N_("Border"), "border" }
};

static guint n_effects = G_N_ELEMENTS (effects);

static GtkWidget *
create_effects_combo (void)
{
  GtkWidget *retval;
  GtkListStore *model;
  GtkCellRenderer *renderer;
  gint i;

  model = gtk_list_store_new (N_COLUMNS,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_UINT);
  
  for (i = 0; i < n_effects; i++)
    {
      GtkTreeIter iter;

      gtk_list_store_insert (model, &iter, i);
      gtk_list_store_set (model, &iter,
                          COLUMN_ID, effects[i].id,
                          COLUMN_LABEL, gettext (effects[i].label),
                          COLUMN_NICK, effects[i].nick,
                          -1);
    }

  retval = gtk_combo_box_new ();
  gtk_combo_box_set_model (GTK_COMBO_BOX (retval),
                           GTK_TREE_MODEL (model));
  g_object_unref (model);

  switch (border_effect[0])
    {
    case 's': /* shadow */
      gtk_combo_box_set_active (GTK_COMBO_BOX (retval),
                                SCREENSHOT_EFFECT_SHADOW);
      break;
    case 'b': /* border */
      gtk_combo_box_set_active (GTK_COMBO_BOX (retval),
                                SCREENSHOT_EFFECT_BORDER);
      break;
    case 'n': /* none */
      gtk_combo_box_set_active (GTK_COMBO_BOX (retval),
                                SCREENSHOT_EFFECT_NONE);
      break;
    default:
      break;
    }
  
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (retval), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (retval), renderer,
                                  "text", COLUMN_LABEL,
                                  NULL);

  g_signal_connect (retval, "changed",
                    G_CALLBACK (effect_combo_changed_cb),
                    NULL);

  return retval;
}

static void
create_effects_frame (GtkWidget   *outer_vbox,
                      const gchar *frame_title)
{
  GtkWidget *main_vbox, *vbox, *hbox;
  GtkWidget *align;
  GtkWidget *label;
  GtkWidget *check;
  GtkWidget *combo;
  gchar *title;

  main_vbox = gtk_vbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (outer_vbox), main_vbox, FALSE, FALSE, 0);
  gtk_widget_show (main_vbox);

  title = g_strconcat ("<b>", frame_title, "</b>", NULL);
  label = gtk_label_new (title);
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
  g_free (title);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  align = gtk_alignment_new (0.0, 0.0, 0.0, 0.0);
  gtk_widget_set_size_request (align, 48, -1);
  gtk_box_pack_start (GTK_BOX (hbox), align, FALSE, FALSE, 0);
  gtk_widget_show (align);

#if 0
  image = gtk_image_new_from_stock (SCREENSHOOTER_ICON,
                                    GTK_ICON_SIZE_DIALOG);
  gtk_container_add (GTK_CONTAINER (align), image);
  gtk_widget_show (image);
#endif

  vbox = gtk_vbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  /** Include window border **/
  check = gtk_check_button_new_with_mnemonic (_("Include the window _border"));
  gtk_widget_set_sensitive (check, take_window_shot);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), include_border);
  g_signal_connect (check, "toggled",
                    G_CALLBACK (include_border_toggled_cb),
                    NULL);
  gtk_box_pack_start (GTK_BOX (vbox), check, FALSE, FALSE, 0);
  gtk_widget_show (check);
  border_check = check;

  /** Effects **/
  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic (_("Apply _effect:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  combo = create_effects_combo ();
  gtk_widget_set_sensitive (combo, take_window_shot);
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  gtk_widget_show (combo);
  effect_combo = combo;
}

static void
create_screenshot_frame (GtkWidget   *outer_vbox,
                         const gchar *frame_title)
{
  GtkWidget *main_vbox, *vbox, *hbox;
  GtkWidget *align;
  GtkWidget *radio;
  GtkWidget *image;
  GtkWidget *spin;
  GtkWidget *label;
  GtkAdjustment *adjust;
  GSList *group;
  gchar *title;

  main_vbox = gtk_vbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (outer_vbox), main_vbox, FALSE, FALSE, 0);
  gtk_widget_show (main_vbox);

  title = g_strconcat ("<b>", frame_title, "</b>", NULL);
  label = gtk_label_new (title);
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
  g_free (title);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  align = gtk_alignment_new (0.0, 0.0, 0.0, 0.0);
  gtk_widget_set_size_request (align, 48, -1);
  gtk_box_pack_start (GTK_BOX (hbox), align, FALSE, FALSE, 0);
  gtk_widget_show (align);

  image = gtk_image_new_from_stock (SCREENSHOOTER_ICON,
                                    GTK_ICON_SIZE_DIALOG);
  gtk_container_add (GTK_CONTAINER (align), image);
  gtk_widget_show (image);

  vbox = gtk_vbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  /** Grab whole desktop **/
  group = NULL;
  radio = gtk_radio_button_new_with_mnemonic (group,
                                              _("Grab the whole _desktop"));
  if (take_window_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), FALSE);
  g_signal_connect (radio, "toggled",
                    G_CALLBACK (target_toggled_cb),
                    GINT_TO_POINTER (FALSE));
  gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
  gtk_widget_show (radio);

  /** Grab current window **/
  radio = gtk_radio_button_new_with_mnemonic (group,
                                              _("Grab the current _window"));
  if (take_window_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
  g_signal_connect (radio, "toggled",
                    G_CALLBACK (target_toggled_cb),
                    GINT_TO_POINTER (TRUE));
  gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
  gtk_widget_show (radio);

  /** Grab after delay **/
  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  /* translators: this is the first part of the "grab after a
   * delay of <spin button> seconds".
   */
  label = gtk_label_new_with_mnemonic (_("Grab _after a delay of"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  adjust = GTK_ADJUSTMENT (gtk_adjustment_new ((gdouble) delay,
                                               0.0, 99.0,
                                               1.0,  1.0,
                                               1.0));
  spin = gtk_spin_button_new (adjust, 1.0, 0);
  g_signal_connect (spin, "value-changed",
                    G_CALLBACK (delay_spin_value_changed_cb),
                    NULL);
  gtk_box_pack_start (GTK_BOX (hbox), spin, FALSE, FALSE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), spin);
  gtk_widget_show (spin);

  /* translators: this is the last part of the "grab after a
   * delay of <spin button> seconds".
   */
  label = gtk_label_new (_("seconds"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_end (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
}

static GtkWidget *
create_interactive_dialog (void)
{
  GtkWidget *retval;
  GtkWidget *main_vbox;

  retval = gtk_dialog_new ();
  gtk_window_set_resizable (GTK_WINDOW (retval), FALSE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (retval), TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (retval), 5);
  gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (retval)->vbox), 2);
  gtk_window_set_title (GTK_WINDOW (retval), "");

  /* main container */
  main_vbox = gtk_vbox_new (FALSE, 18);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (retval)->vbox),
                      main_vbox,
                      TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  create_screenshot_frame (main_vbox, _("Take Screenshot"));
  create_effects_frame (main_vbox, _("Effects"));

  gtk_dialog_set_has_separator (GTK_DIALOG (retval), FALSE);
  gtk_dialog_add_buttons (GTK_DIALOG (retval),
                          GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                          _("Take _Screenshot"), GTK_RESPONSE_OK,
                          NULL);

  /* we need to block on "response" and keep showing the interactive
   * dialog in case the user did choose "help"
   */
  g_signal_connect (retval, "response",
                    G_CALLBACK (interactive_dialog_response_cb),
                    NULL);

  return retval;
}

/* We assume that uri is valid and has been tested elsewhere
 */
static char *
generate_filename_for_uri (const char *uri)
{
  char *retval;
  char *file_name;
  int i = 1;

  if (window_title)
    {
      /* translators: this is the name of the file that gets made up
       * with the screenshot if a specific window is taken */
      file_name = g_strdup_printf (_("Screenshot-%s.png"), window_title);
    }
  else
    {
      /* translators: this is the name of the file that gets made up
       * with the screenshot if the entire screen is taken */
      file_name = g_strdup (_("Screenshot.png"));
    }

  retval = g_build_filename (uri, file_name, NULL);
  g_free (file_name);

  do
    {
      GnomeVFSFileInfo *info;
      GnomeVFSResult result;

      info = gnome_vfs_file_info_new ();
      result = gnome_vfs_get_file_info (retval, info,
                                        GNOME_VFS_FILE_INFO_DEFAULT |
                                        GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
      gnome_vfs_file_info_unref (info);

      switch (result)
	{
	case GNOME_VFS_ERROR_NOT_FOUND:
	  return retval;
	case GNOME_VFS_OK:
	  g_free (retval);
	  break;
	case GNOME_VFS_ERROR_PROTOCOL_ERROR:
	  /* try again?  I'm getting these errors sporadically */
	default:
	  g_warning ("ERROR: %s:%s\n",
                     retval,
                     gnome_vfs_result_to_string (result));
	  g_free (retval);
	  return NULL;
	}

      /* We had a hit.  We need to make a new file */
      if (window_title)
	{
	  /* translators: this is the name of the file that gets
	   * made up with the screenshot if a specific window is
	   * taken */
	  file_name = g_strdup_printf (_("Screenshot-%s-%d.png"),
				       window_title, i);
	}
      else
	{
	  /* translators: this is the name of the file that gets
	   * made up with the screenshot if the entire screen is
	   * taken */
	  file_name = g_strdup_printf (_("Screenshot-%d.png"), i);
	}

      retval = g_build_filename (uri, file_name, NULL);
      g_free (file_name);

      i++;
    }
  while (TRUE);
}

static void
save_folder_to_gconf (ScreenshotDialog *dialog)
{
  GConfClient *gconf_client;
  char *folder;

  gconf_client = gconf_client_get_default ();

  folder = screenshot_dialog_get_folder (dialog);
  /* Error is NULL, as there's nothing we can do */
  gconf_client_set_string (gconf_client,
			   LAST_SAVE_DIRECTORY_KEY, folder,
                           NULL);

  g_object_unref (gconf_client);
}

static gboolean
try_to_save (ScreenshotDialog *dialog,
	     const char       *target)
{
  GnomeVFSResult result;
  GnomeVFSURI *source_uri;
  GnomeVFSURI *target_uri;

  g_assert (temporary_file);

  screenshot_dialog_set_busy (dialog, TRUE);

  source_uri = gnome_vfs_uri_new (temporary_file);
  target_uri = gnome_vfs_uri_new (target);

  result = screenshot_xfer_uri (source_uri,
				target_uri,
				screenshot_dialog_get_toplevel (dialog));

  gnome_vfs_uri_unref (source_uri);
  gnome_vfs_uri_unref (target_uri);

  screenshot_dialog_set_busy (dialog, FALSE);

  if (result == GNOME_VFS_OK)
    {
      save_folder_to_gconf (dialog);
      return TRUE;
    }

  return FALSE;
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
run_dialog (ScreenshotDialog *dialog)
{
  GtkWidget *toplevel;
  int result;
  int keep_going;
  char *uri;

  toplevel = screenshot_dialog_get_toplevel (dialog);

  do
    {
      keep_going = FALSE;
      result = gtk_dialog_run (GTK_DIALOG (toplevel));
      switch (result)
	{
	case GTK_RESPONSE_HELP:
	  display_help (GTK_WINDOW (toplevel));
	  keep_going = TRUE;
	  break;
	case GTK_RESPONSE_OK:
	  uri = screenshot_dialog_get_uri (dialog);
	  if (temporary_file == NULL)
	    {
	      save_immediately = TRUE;
	      screenshot_dialog_set_busy (dialog, TRUE);
	      keep_going = TRUE;
	    }
	  else
	    {
	      /* We've saved the temporary file.  Lets try to copy it to the
	       * correct location */
	      if (! try_to_save (dialog, uri))
		keep_going = TRUE;
	    }
	  break;
	default:
	  break;
	}
    }
  while (keep_going);
}

static void
prepare_screenshot (void)
{
  ScreenshotDialog *dialog;
  Window win;
  char *initial_uri;

  if (!screenshot_grab_lock ())
    exit (0);

  if (take_window_shot)
    {
      win = screenshot_find_current_window (include_border);
      if (win == None)
	{
	  take_window_shot = FALSE;
	  win = GDK_ROOT_WINDOW ();
	}
      else
	{
	  gchar *tmp;

	  window_title = screenshot_get_window_title (win);
	  tmp = screenshot_sanitize_filename (window_title);
	  g_free (window_title);
	  window_title = tmp;
	}
    }
  else
    {
      win = GDK_ROOT_WINDOW ();
    }

  screenshot = screenshot_get_pixbuf (win);

  if (take_window_shot) {
    switch (border_effect[0])
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

  screenshot_release_lock ();

  if (screenshot == NULL)
    {
      screenshot_show_error_dialog (NULL,
                                    _("Unable to take a screenshot of the current window"),
                                    NULL);
      exit (1);
    }


  /* If uri isn't local, we should do this async before taking the sshot */
  initial_uri = generate_filename_for_uri (last_save_dir);
  if (initial_uri == NULL)
    {
      gchar *desktop_dir;
      /* We failed to make a new name for the last save dir.  We try again
       * with our home dir as a fallback.  If that fails, we don't have a
       * place to put it. */
      desktop_dir = get_desktop_dir ();
      initial_uri = generate_filename_for_uri (desktop_dir);
      g_free (desktop_dir);

      if (initial_uri == NULL)
	{
	  initial_uri = g_strdup ("file:///");
	}
  }
  dialog = screenshot_dialog_new (screenshot, initial_uri, take_window_shot);
  g_free (initial_uri);

  screenshot_save_start (screenshot, save_done_notification, dialog);

  run_dialog (dialog);

}

static gboolean
prepare_screenshot_timeout (gpointer data)
{
  gtk_main_quit ();
  prepare_screenshot ();

  return FALSE;
}


static gchar *
get_desktop_dir (void)
{
  GConfClient *gconf_client;
  gboolean desktop_is_home_dir = FALSE;
  gchar *desktop_dir;

  gconf_client = gconf_client_get_default ();
  desktop_is_home_dir = gconf_client_get_bool (gconf_client,
                                               "/apps/nautilus/preferences/desktop_is_home_dir",
                                               NULL);
  if (desktop_is_home_dir)
    desktop_dir = g_build_filename (g_get_home_dir (), NULL);
  else
    desktop_dir = g_build_filename (g_get_home_dir (), "Desktop", NULL);

  g_object_unref (gconf_client);

  return desktop_dir;
}

/* Load options */
static void
load_options (void)
{
  GnomeClient *client;
  GConfClient *gconf_client;

  client = gnome_master_client ();
  gnome_client_set_restart_style (client, GNOME_RESTART_NEVER);

  gconf_client = gconf_client_get_default ();

  /* Find various dirs */
  last_save_dir = gconf_client_get_string (gconf_client,
                                           LAST_SAVE_DIRECTORY_KEY,
                                           NULL);
  if (!last_save_dir || !last_save_dir[0])
    {
      last_save_dir = get_desktop_dir ();
    }
  else if (last_save_dir[0] == '~')
    {
      char *tmp = gnome_vfs_expand_initial_tilde (last_save_dir);
      g_free (last_save_dir);
      last_save_dir = tmp;
    }

  include_border = gconf_client_get_bool (gconf_client,
                                          INCLUDE_BORDER_KEY,
                                          NULL);

  border_effect = gconf_client_get_string (gconf_client,
                                           BORDER_EFFECT_KEY,
                                           NULL);
  if (!border_effect)
    border_effect = g_strdup ("none");

  g_object_unref (gconf_client);
}

static void
register_screenshooter_icon (GtkIconFactory * factory)
{
  GtkIconSource * source;
  GtkIconSet * icon_set;

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
  GtkIconFactory * factory;
  GtkIconSize screenshooter_icon_size;

  screenshooter_icon_size = gtk_icon_size_register ("panel-menu", 
	                                            GTK_ICON_SIZE_DIALOG,
	                                            GTK_ICON_SIZE_DIALOG);
  factory = gtk_icon_factory_new ();
  gtk_icon_factory_add_default (factory);

  register_screenshooter_icon (factory);
  g_object_unref (factory);
}

/* main */
int
main (int argc, char *argv[])
{
  GnomeProgram *program;
  GOptionContext *context;
  GOptionGroup *group;
  gboolean window_arg = FALSE;
  gboolean include_border_arg = FALSE;
  gboolean interactive_arg = FALSE;
  gchar *border_effect_arg = NULL;
  guint delay_arg = 0;

  const GOptionEntry entries[] = {
    { "window", 'w', 0, G_OPTION_ARG_NONE, &window_arg, N_("Grab a window instead of the entire screen"), NULL },
    { "include-border", 'b', 0, G_OPTION_ARG_NONE, &include_border_arg, N_("Include the window border with the screenshot"), NULL },
    { "delay", 'd', 0, G_OPTION_ARG_INT, &delay_arg, N_("Take screenshot after specified delay [in seconds]"), N_("seconds") },
    { "border-effect", 'e', 0, G_OPTION_ARG_STRING, &border_effect_arg, N_("Effect to add to the border (shadow, border or none)"), N_("effect") },
    { "interactive", 'i', 0, G_OPTION_ARG_NONE, &interactive_arg, N_("Interactively set options"), NULL },
    { NULL },
  };

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  group = g_option_group_new ("gnome-screenshot",
		  	      _("Options for Screenshot"),
			      _("Show Screenshot options"),
			      NULL, NULL);
  g_option_group_add_entries (group, entries);

  context = g_option_context_new (_("Take a picture of the screen"));
  g_option_context_set_ignore_unknown_options (context, FALSE);
  g_option_context_set_help_enabled (context, TRUE);
  g_option_context_set_main_group (context, group);

  gnome_authentication_manager_init ();

  program = gnome_program_init ("gnome-screenshot", VERSION,
				LIBGNOMEUI_MODULE,
				argc, argv,
				GNOME_PARAM_GOPTION_CONTEXT, context,
				GNOME_PROGRAM_STANDARD_PROPERTIES,
				NULL);
  glade_gnome_init();
  gtk_window_set_default_icon_name (SCREENSHOOTER_ICON);
  screenshooter_init_stock_icons ();

  load_options ();
  /* allow the command line to override options */
  if (window_arg)
    take_window_shot = TRUE;

  if (include_border_arg)
    include_border = TRUE;

  if (border_effect_arg)
    {
      g_free (border_effect);
      border_effect = border_effect_arg;
    }

  if (delay_arg > 0)
    delay = delay_arg;

  /* interactive mode overrides everything */
  if (interactive_arg)
    {
      GtkWidget *dialog;
      gint response;

      dialog = create_interactive_dialog ();
      response = gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);

      switch (response)
        {
        case GTK_RESPONSE_DELETE_EVENT:
        case GTK_RESPONSE_CANCEL:
          g_object_unref (program);
          return EXIT_SUCCESS;
        case GTK_RESPONSE_OK:
          break;
        default:
          g_assert_not_reached ();
          break;
        }
    }

  if (delay > 0)
    {      
      g_timeout_add (delay * 1000,
		     prepare_screenshot_timeout,
		     NULL);
      gtk_main ();
    }
  else
    {
      prepare_screenshot ();
    }

  g_object_unref (program);

  return EXIT_SUCCESS;
}
