/* screenshot-interactive-dialog.h - Interactive options dialog
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
#include "screenshot-interactive-dialog.h"
#include "screenshot-utils.h"

#define SCREENSHOOTER_ICON "applets-screenshooter"

static GtkWidget *border_check = NULL;
static GtkWidget *effect_combo = NULL;
static GtkWidget *effect_label = NULL;
static GtkWidget *effects_vbox = NULL;
static GtkWidget *delay_hbox = NULL;

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

#define TARGET_TOGGLE_DESKTOP 0
#define TARGET_TOGGLE_WINDOW  1
#define TARGET_TOGGLE_AREA    2

static void
target_toggled_cb (GtkToggleButton *button,
                   gpointer         data)
{
  int target_toggle = GPOINTER_TO_INT (data);
  gboolean take_area_shot, take_window_shot;

  if (gtk_toggle_button_get_active (button))
    {
      take_window_shot = (target_toggle == TARGET_TOGGLE_WINDOW);
      take_area_shot = (target_toggle == TARGET_TOGGLE_AREA);
      
      gtk_widget_set_sensitive (border_check, take_window_shot);
      gtk_widget_set_sensitive (effect_combo, take_window_shot);
      gtk_widget_set_sensitive (effect_label, take_window_shot);

      gtk_widget_set_sensitive (delay_hbox, !take_area_shot);
      gtk_widget_set_sensitive (effects_vbox, !take_area_shot);

      screenshot_config->take_window_shot = take_window_shot;
      screenshot_config->take_area_shot = take_area_shot;
    }
}

static void
delay_spin_value_changed_cb (GtkSpinButton *button)
{
  screenshot_config->delay = gtk_spin_button_get_value_as_int (button);
}

static void
include_border_toggled_cb (GtkToggleButton *button,
                           gpointer         data)
{
  screenshot_config->include_border = gtk_toggle_button_get_active (button);
}

static void
include_pointer_toggled_cb (GtkToggleButton *button,
                            gpointer         data)
{
  screenshot_config->include_pointer = gtk_toggle_button_get_active (button);
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

      g_free (screenshot_config->border_effect);
      screenshot_config->border_effect = effect; /* gets free'd later */
    }
}

static gint 
interactive_dialog_key_press_cb (GtkWidget *widget, 
                                 GdkEventKey *event,
                                 gpointer user_data)
{
  if (event->keyval == GDK_KEY_F1)
    {
      screenshot_display_help (GTK_WINDOW (widget));
      return TRUE;
    }

  return FALSE;
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

  switch (screenshot_config->border_effect[0])
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

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (outer_vbox), main_vbox, FALSE, FALSE, 0);
  gtk_widget_show (main_vbox);
  effects_vbox = main_vbox;

  title = g_strconcat ("<b>", frame_title, "</b>", NULL);
  label = gtk_label_new (title);
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
  g_free (title);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  align = gtk_alignment_new (0.0, 0.0, 0.0, 0.0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 12, 0);
  gtk_box_pack_start (GTK_BOX (hbox), align, FALSE, FALSE, 0);
  gtk_widget_show (align);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add (GTK_CONTAINER (align), vbox);
  gtk_widget_show (vbox);

  /** Include pointer **/
  check = gtk_check_button_new_with_mnemonic (_("Include _pointer"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
                                screenshot_config->include_pointer);
  g_signal_connect (check, "toggled",
                    G_CALLBACK (include_pointer_toggled_cb),
                    NULL);
  gtk_box_pack_start (GTK_BOX (vbox), check, FALSE, FALSE, 0);
  gtk_widget_show (check);

  /** Include window border **/
  check = gtk_check_button_new_with_mnemonic (_("Include the window _border"));
  gtk_widget_set_sensitive (check,
                            screenshot_config->take_window_shot);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
                                screenshot_config->include_border);
  g_signal_connect (check, "toggled",
                    G_CALLBACK (include_border_toggled_cb),
                    NULL);
  gtk_box_pack_start (GTK_BOX (vbox), check, FALSE, FALSE, 0);
  gtk_widget_show (check);
  border_check = check;

  /** Effects **/
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic (_("Apply _effect:"));
  gtk_widget_set_sensitive (label, screenshot_config->take_window_shot);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
  effect_label = label;

  combo = create_effects_combo ();
  gtk_widget_set_sensitive (combo, screenshot_config->take_window_shot);
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

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (outer_vbox), main_vbox, FALSE, FALSE, 0);
  gtk_widget_show (main_vbox);

  title = g_strconcat ("<b>", frame_title, "</b>", NULL);
  label = gtk_label_new (title);
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
  g_free (title);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
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

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  /** Grab whole screen **/
  group = NULL;
  radio = gtk_radio_button_new_with_mnemonic (group,
                                              _("Grab the whole sc_reen"));
  if (screenshot_config->take_window_shot ||
      screenshot_config->take_area_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), FALSE);

  g_signal_connect (radio, "toggled",
                    G_CALLBACK (target_toggled_cb),
                    GINT_TO_POINTER (TARGET_TOGGLE_DESKTOP));
  gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
  gtk_widget_show (radio);

  /** Grab current window **/
  radio = gtk_radio_button_new_with_mnemonic (group,
                                              _("Grab the current _window"));
  if (screenshot_config->take_window_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
  g_signal_connect (radio, "toggled",
                    G_CALLBACK (target_toggled_cb),
                    GINT_TO_POINTER (TARGET_TOGGLE_WINDOW));
  gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
  gtk_widget_show (radio);

  /** Grab area of the desktop **/
  radio = gtk_radio_button_new_with_mnemonic (group,
                                              _("Select _area to grab"));
  if (screenshot_config->take_area_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
  g_signal_connect (radio, "toggled",
                    G_CALLBACK (target_toggled_cb),
                    GINT_TO_POINTER (TARGET_TOGGLE_AREA));
  gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
  gtk_widget_show (radio);

  /** Grab after delay **/
  delay_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (vbox), delay_hbox, FALSE, FALSE, 0);
  gtk_widget_show (delay_hbox);

  if (screenshot_config->take_area_shot)
    gtk_widget_set_sensitive (delay_hbox, FALSE);

  /* translators: this is the first part of the "grab after a
   * delay of <spin button> seconds".
   */
  label = gtk_label_new_with_mnemonic (_("Grab after a _delay of"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (delay_hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  adjust = GTK_ADJUSTMENT (gtk_adjustment_new ((gdouble) screenshot_config->delay,
                                               0.0, 99.0,
                                               1.0,  1.0,
                                               0.0));
  spin = gtk_spin_button_new (adjust, 1.0, 0);
  g_signal_connect (spin, "value-changed",
                    G_CALLBACK (delay_spin_value_changed_cb),
                    NULL);
  gtk_box_pack_start (GTK_BOX (delay_hbox), spin, FALSE, FALSE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), spin);
  gtk_widget_show (spin);

  /* translators: this is the last part of the "grab after a
   * delay of <spin button> seconds".
   */
  label = gtk_label_new (_("seconds"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_end (GTK_BOX (delay_hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
}


GtkWidget *
screenshot_interactive_dialog_new (void)
{
  GtkWidget *dialog;
  GtkWidget *main_vbox;
  GtkWidget *content_area;
  gboolean shows_app_menu;
  GtkSettings *settings;

  dialog = gtk_dialog_new ();
  gtk_window_set_application (GTK_WINDOW (dialog), GTK_APPLICATION (g_application_get_default ()));
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_window_set_title (GTK_WINDOW (dialog), _("Take Screenshot"));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_set_spacing (GTK_BOX (content_area), 2);

  /* main container */
  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 5);
  gtk_box_pack_start (GTK_BOX (content_area), main_vbox, TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  create_screenshot_frame (main_vbox, _("Take Screenshot"));
  create_effects_frame (main_vbox, _("Effects"));

  gtk_dialog_add_button (GTK_DIALOG (dialog),
                         _("Take _Screenshot"), GTK_RESPONSE_OK);

  /* add help as a dialog button if we're not showing the application menu */
  settings = gtk_settings_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (dialog)));
  g_object_get (settings,
                "gtk-shell-shows-app-menu", &shows_app_menu,
                NULL);
  if (!shows_app_menu)
    gtk_dialog_add_button (GTK_DIALOG (dialog),
                           GTK_STOCK_HELP, GTK_RESPONSE_HELP);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

  g_signal_connect (dialog, "key-press-event",
                    G_CALLBACK (interactive_dialog_key_press_cb), 
                    NULL);

  gtk_widget_show_all (dialog);

  return dialog;
}
