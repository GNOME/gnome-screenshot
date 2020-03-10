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

#include "screenshot-application.h"
#include "screenshot-config.h"
#include "screenshot-interactive-dialog.h"
#include "screenshot-utils.h"

static GtkWidget *pointer_row = NULL;
static GtkWidget *shadow_row = NULL;
static GtkWidget *delay_row = NULL;

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

      gtk_widget_set_sensitive (shadow_row, take_window_shot);

      gtk_widget_set_sensitive (pointer_row, !take_area_shot);
      gtk_widget_set_sensitive (delay_row, !take_area_shot);

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
include_pointer_toggled_cb (GtkSwitch *toggle,
                            gpointer         data)
{
  screenshot_config->include_pointer = gtk_switch_get_active (toggle);
  gtk_switch_set_state (toggle, gtk_switch_get_active (toggle));
}

static void
use_shadow_toggled_cb (GtkSwitch *toggle,
                         gpointer     user_data)
{
  screenshot_config->use_shadow = gtk_switch_get_active (toggle);
  gtk_switch_set_state (toggle, gtk_switch_get_active (toggle));
}

static void
connect_effects_frame (GtkBuilder *ui)
{
  GtkWidget *pointer;
  GtkWidget *shadow;

  /** Include pointer **/
  pointer = GTK_WIDGET (gtk_builder_get_object (ui, "pointer"));
  gtk_switch_set_active (GTK_SWITCH (pointer), screenshot_config->include_pointer);
  g_signal_connect (pointer, "state-set",
                    G_CALLBACK (include_pointer_toggled_cb),
                    NULL);

  /** Use shadow **/
  shadow = GTK_WIDGET (gtk_builder_get_object (ui, "shadow"));
  gtk_switch_set_active (GTK_SWITCH (shadow), screenshot_config->use_shadow);
  g_signal_connect (shadow, "state-set",
                    G_CALLBACK (use_shadow_toggled_cb),
                    NULL);
}

static void
connect_screenshot_frame (GtkBuilder *ui)
{
  GtkAdjustment *adjust;
  GtkWidget *screen;
  GtkWidget *selection;
  GtkWidget *window;
  GtkWidget *delay;
  GSList *group;

  /** Grab whole screen **/
  group = NULL;
  screen = GTK_WIDGET (gtk_builder_get_object (ui, "screen"));

  if (screenshot_config->take_window_shot ||
      screenshot_config->take_area_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (screen), FALSE);

  g_signal_connect (screen, "toggled",
                    G_CALLBACK (target_toggled_cb),
                    GINT_TO_POINTER (TARGET_TOGGLE_DESKTOP));
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (screen));

  /** Grab current window **/
  window = GTK_WIDGET (gtk_builder_get_object (ui, "window"));
  gtk_radio_button_set_group (GTK_RADIO_BUTTON (window), group);

  if (screenshot_config->take_window_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (window), TRUE);
  g_signal_connect (window, "toggled",
                    G_CALLBACK (target_toggled_cb),
                    GINT_TO_POINTER (TARGET_TOGGLE_WINDOW));
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (window));

  shadow_row = GTK_WIDGET (gtk_builder_get_object (ui, "shadowrow"));
  gtk_widget_set_sensitive (shadow_row, screenshot_config->take_window_shot);

  /** Grab area of the desktop **/
  selection = GTK_WIDGET (gtk_builder_get_object (ui, "selection"));

  if (screenshot_config->take_area_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (selection), TRUE);
  g_signal_connect (selection, "toggled",
                    G_CALLBACK (target_toggled_cb),
                    GINT_TO_POINTER (TARGET_TOGGLE_AREA));
  pointer_row = GTK_WIDGET (gtk_builder_get_object (ui, "pointerrow"));
  gtk_widget_set_sensitive (pointer_row, !screenshot_config->take_area_shot);

  /** Grab after delay **/
  delay = GTK_WIDGET (gtk_builder_get_object (ui, "delay"));
  delay_row = GTK_WIDGET (gtk_builder_get_object (ui, "delayrow"));
  gtk_widget_set_sensitive (delay_row, !screenshot_config->take_area_shot);

  adjust = GTK_ADJUSTMENT (gtk_adjustment_new ((gdouble) screenshot_config->delay,
                                               0.0, 99.0,
                                               1.0,  1.0,
                                               0.0));

  gtk_spin_button_set_adjustment (GTK_SPIN_BUTTON (delay), adjust);
  g_signal_connect (delay, "value-changed",
                    G_CALLBACK (delay_spin_value_changed_cb),
                    NULL);
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

static void
screenshot_listbox_update_header_func (GtkListBoxRow *row,
                                       GtkListBoxRow *before,
                                       gpointer user_data)
{
  GtkWidget *current;

  if (before == NULL)
    {
      gtk_list_box_row_set_header (row, NULL);
      return;
    }

  current = gtk_list_box_row_get_header (row);
  if (current == NULL)
    {
      current = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gtk_widget_show (current);
      gtk_list_box_row_set_header (row, current);
    }
}

GtkWidget *
screenshot_interactive_dialog_new (CaptureClickedCallback f, gpointer user_data)
{
  ScreenshotApplication *self = user_data;
  GtkWidget *dialog;
  GtkWidget *capture_button;
  GtkWidget *listbox;
  GtkBuilder *ui;
  CaptureData *data;

  ui = gtk_builder_new_from_resource ("/org/gnome/Screenshot/ui/screenshot-interactive.ui");

  dialog = GTK_WIDGET (gtk_builder_get_object (ui, "screenshot_window"));
  gtk_window_set_application (GTK_WINDOW (dialog), GTK_APPLICATION (self));

  capture_button = GTK_WIDGET (gtk_builder_get_object (ui, "capture_button"));

  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);

  listbox = GTK_WIDGET (gtk_builder_get_object (ui, "listbox"));
  gtk_list_box_set_header_func (GTK_LIST_BOX (listbox),
                                screenshot_listbox_update_header_func,
                                user_data,
                                NULL);

  data = g_new (CaptureData, 1);
  data->widget = dialog;
  data->callback = f;
  data->user_data = user_data;
  g_signal_connect (capture_button, "clicked", G_CALLBACK (capture_button_clicked_cb), data);
  gtk_widget_set_can_default (capture_button, TRUE);
  gtk_widget_grab_default (capture_button);

  gtk_widget_show_all (dialog);

  connect_screenshot_frame (ui);
  connect_effects_frame (ui);

  return dialog;
}
