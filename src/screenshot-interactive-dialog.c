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

typedef enum {
  SCREENSHOT_MODE_SCREEN,
  SCREENSHOT_MODE_WINDOW,
  SCREENSHOT_MODE_SELECTION,
} ScreenshotMode;

struct _ScreenshotInteractiveDialog
{
  GtkApplicationWindow parent_instance;

  GtkWidget *capture_button;
  GtkWidget *listbox;
  GtkWidget *pointer;
  GtkWidget *pointer_row;
  GtkWidget *delay;
  GtkAdjustment *delay_adjustment;

  GtkWidget *screen;
  GtkWidget *window;
  GtkWidget *selection;

  CaptureClickedCallback callback;
  gpointer user_data;
};

G_DEFINE_TYPE (ScreenshotInteractiveDialog, screenshot_interactive_dialog, GTK_TYPE_APPLICATION_WINDOW)

static void
set_mode (ScreenshotInteractiveDialog *self,
          ScreenshotMode               mode)
{
  gboolean take_window_shot = (mode == SCREENSHOT_MODE_WINDOW);
  gboolean take_area_shot = (mode == SCREENSHOT_MODE_SELECTION);

  gtk_widget_set_sensitive (self->pointer_row, !take_area_shot);

  screenshot_config->take_window_shot = take_window_shot;
  screenshot_config->take_area_shot = take_area_shot;
}

static void
screen_toggled_cb (GtkToggleButton             *button,
                   ScreenshotInteractiveDialog *self)
{
  if (gtk_toggle_button_get_active (button))
    set_mode (self, SCREENSHOT_MODE_SCREEN);
}

static void
window_toggled_cb (GtkToggleButton             *button,
                   ScreenshotInteractiveDialog *self)
{
  if (gtk_toggle_button_get_active (button))
    set_mode (self, SCREENSHOT_MODE_WINDOW);
}

static void
selection_toggled_cb (GtkToggleButton             *button,
                      ScreenshotInteractiveDialog *self)
{
  if (gtk_toggle_button_get_active (button))
    set_mode (self, SCREENSHOT_MODE_SELECTION);
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
capture_button_clicked_cb (GtkButton                   *button,
                           ScreenshotInteractiveDialog *self)
{
  CaptureClickedCallback callback = self->callback;
  gpointer user_data = self->user_data;

  gtk_widget_destroy (GTK_WIDGET (self));
  callback (user_data);
}

static void
header_func (GtkListBoxRow               *row,
             GtkListBoxRow               *before,
             ScreenshotInteractiveDialog *self)
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

static void
screenshot_interactive_dialog_class_init (ScreenshotInteractiveDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Screenshot/ui/screenshot-interactive-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, capture_button);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, listbox);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, pointer);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, pointer_row);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, delay);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, delay_adjustment);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, screen);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, window);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, selection);
  gtk_widget_class_bind_template_callback (widget_class, screen_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, window_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, selection_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, delay_spin_value_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, include_pointer_toggled_cb);
  gtk_widget_class_bind_template_callback (widget_class, capture_button_clicked_cb);
}

static void
screenshot_interactive_dialog_init (ScreenshotInteractiveDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_list_box_set_header_func (GTK_LIST_BOX (self->listbox),
                                (GtkListBoxUpdateHeaderFunc) header_func,
                                self,
                                NULL);
}

GtkWidget *
screenshot_interactive_dialog_new (CaptureClickedCallback f, gpointer user_data)
{
  ScreenshotApplication *self = user_data;
  ScreenshotInteractiveDialog *dialog;

  dialog = g_object_new (SCREENSHOT_TYPE_INTERACTIVE_DIALOG, NULL);
  gtk_window_set_application (GTK_WINDOW (dialog), GTK_APPLICATION (self));

  dialog->callback = f;
  dialog->user_data = user_data;

  gtk_widget_show_all (GTK_WIDGET (dialog));

  if (screenshot_config->take_window_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->window), TRUE);

  if (screenshot_config->take_area_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dialog->selection), TRUE);

  gtk_widget_set_sensitive (dialog->pointer_row, !screenshot_config->take_area_shot);
  gtk_switch_set_active (GTK_SWITCH (dialog->pointer), screenshot_config->include_pointer);

  gtk_adjustment_set_value (dialog->delay_adjustment, (gdouble) screenshot_config->delay);

  return GTK_WIDGET (dialog);
}
