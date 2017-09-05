/* screenshot-interactive-dialog.h - Interactive options dialog
 *
 * Copyright (C) 2001 Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2006 Emmanuele Bassi <ebassi@gnome.org>
 * Copyright (C) 2008, 2011 Cosimo Cecchi <cosimoc@gnome.org>
 * Copyright (C) 2013 Nils Dagsson Moskopp <nils@dieweltistgarnichtso.net>
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
  SCREENSHOT_EFFECT_BORDER,
  SCREENSHOT_EFFECT_VINTAGE
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

  if (event->keyval == GDK_KEY_Escape)
    {
      gtk_widget_destroy (widget);
      return TRUE;
    }

  return FALSE;
}

typedef struct {
  ScreenshotEffectType id;
  const gchar *label;
  const gchar *nick;
} ScreenshotEffect;

static const ScreenshotEffect effects[] = {
  /* Translators:
   * these are the names of the effects available which will be
   * displayed inside a combo box in interactive mode for the user
   * to chooser.
   */
  { SCREENSHOT_EFFECT_NONE, N_("None"), "none" },
  { SCREENSHOT_EFFECT_SHADOW, N_("Drop shadow"), "shadow" },
  { SCREENSHOT_EFFECT_BORDER, N_("Border"), "border" },
  { SCREENSHOT_EFFECT_VINTAGE, N_("Vintage"), "vintage" }
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
    case 'v': /* vintage */
      gtk_combo_box_set_active (GTK_COMBO_BOX (retval),
                                SCREENSHOT_EFFECT_VINTAGE);
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
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
  g_free (title);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);
  gtk_widget_set_margin_start (vbox, 12);
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
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
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
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
  g_free (title);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  image = gtk_image_new_from_icon_name (SCREENSHOOTER_ICON, GTK_ICON_SIZE_DIALOG);
  gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
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
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
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
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
  gtk_box_pack_end (GTK_BOX (delay_hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
}

typedef struct {
  GtkWidget *widget;
  CaptureClickedCallback callback;
  gpointer user_data;
} CaptureData;

static void
capture_button_clicked_cb (GtkButton *button, CaptureData *data)
{
  gtk_widget_destroy (data->widget);
  data->callback (data->user_data);
  g_free (data);
}

GtkWidget *
screenshot_interactive_dialog_new (CaptureClickedCallback f, gpointer user_data)
{
  GtkWidget *dialog;
  GtkWidget *main_vbox;
  GtkWidget *header_bar;
  GtkWidget *button;
  GtkStyleContext *context;
  GtkSizeGroup *size_group;
  CaptureData *data;

  dialog = gtk_application_window_new (GTK_APPLICATION (g_application_get_default ()));

  header_bar = gtk_header_bar_new ();
  gtk_header_bar_set_show_close_button (GTK_HEADER_BAR (header_bar), TRUE);
  gtk_header_bar_set_decoration_layout (GTK_HEADER_BAR (header_bar), "menu");
  gtk_window_set_titlebar (GTK_WINDOW (dialog), header_bar);

  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);

  /* main container */
  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 5);
  gtk_container_add (GTK_CONTAINER (dialog), main_vbox);

  create_screenshot_frame (main_vbox, _("Take Screenshot"));
  create_effects_frame (main_vbox, _("Effects"));

  size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

  button = gtk_button_new_with_mnemonic (_("Take _Screenshot"));
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  context = gtk_widget_get_style_context (button);
  gtk_style_context_add_class (context, "suggested-action");
  data = g_new (CaptureData, 1);
  data->widget = dialog;
  data->callback = f;
  data->user_data = user_data;
  g_signal_connect (button, "clicked", G_CALLBACK (capture_button_clicked_cb), data);
  gtk_size_group_add_widget (size_group, button);
  gtk_header_bar_pack_end (GTK_HEADER_BAR (header_bar), button);
  gtk_widget_set_can_default (button, TRUE);
  gtk_widget_grab_default (button);
  g_signal_connect (dialog, "key-press-event",
                    G_CALLBACK (interactive_dialog_key_press_cb), 
                    NULL);

  button = gtk_button_new_with_mnemonic (_("_Cancel"));
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  gtk_size_group_add_widget (size_group, button);
  gtk_header_bar_pack_start (GTK_HEADER_BAR (header_bar), button);
  g_signal_connect_swapped (button, "clicked",
                            G_CALLBACK (gtk_widget_destroy), dialog);

  g_object_unref (size_group);

  gtk_widget_show_all (dialog);

  return dialog;
}
