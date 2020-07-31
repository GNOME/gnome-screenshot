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

typedef enum {
  SCREENSHOT_MODE_SCREEN,
  SCREENSHOT_MODE_WINDOW,
  SCREENSHOT_MODE_SELECTION,
} ScreenshotMode;

struct _ScreenshotInteractiveDialog
{
  HdyApplicationWindow parent_instance;

  GtkWidget *listbox;
  GtkWidget *pointer;
  GtkWidget *pointer_row;
  GtkAdjustment *delay_adjustment;
  GtkWidget *window;
  GtkWidget *selection;
};

G_DEFINE_TYPE (ScreenshotInteractiveDialog, screenshot_interactive_dialog, HDY_TYPE_APPLICATION_WINDOW)

enum {
  SIGNAL_CAPTURE,
  N_SIGNALS,
};

static guint signals[N_SIGNALS];

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
delay_spin_value_changed_cb (GtkSpinButton               *button,
                             ScreenshotInteractiveDialog *self)
{
  screenshot_config->delay = gtk_spin_button_get_value_as_int (button);
}

static void
include_pointer_toggled_cb (GtkSwitch                   *toggle,
                            ScreenshotInteractiveDialog *self)
{
  screenshot_config->include_pointer = gtk_switch_get_active (toggle);
  gtk_switch_set_state (toggle, gtk_switch_get_active (toggle));
}

static void
capture_button_clicked_cb (GtkButton                   *button,
                           ScreenshotInteractiveDialog *self)
{
  g_signal_emit (self, signals[SIGNAL_CAPTURE], 0);
}

static void
screenshot_interactive_dialog_class_init (ScreenshotInteractiveDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  signals[SIGNAL_CAPTURE] =
    g_signal_new ("capture",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Screenshot/ui/screenshot-interactive-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, listbox);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, pointer);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, pointer_row);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotInteractiveDialog, delay_adjustment);
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

  if (screenshot_config->take_window_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->window), TRUE);

  if (screenshot_config->take_area_shot)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->selection), TRUE);

  gtk_widget_set_sensitive (self->pointer_row, !screenshot_config->take_area_shot);
  gtk_switch_set_active (GTK_SWITCH (self->pointer), screenshot_config->include_pointer);

  gtk_adjustment_set_value (self->delay_adjustment, (gdouble) screenshot_config->delay);
}

ScreenshotInteractiveDialog *
screenshot_interactive_dialog_new (GtkApplication *app)
{
  g_return_val_if_fail (GTK_IS_APPLICATION (app), NULL);

  return g_object_new (SCREENSHOT_TYPE_INTERACTIVE_DIALOG,
                       "application", app,
                       NULL);
}
