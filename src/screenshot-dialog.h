/* screenshot-dialog.h - main GNOME Screenshot dialog
 *
 * Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 */

#ifndef __SCREENSHOT_DIALOG_H__
#define __SCREENSHOT_DIALOG_H__

#include <gtk/gtk.h>

typedef enum {
  SCREENSHOT_RESPONSE_SAVE,
  SCREENSHOT_RESPONSE_COPY
} ScreenshotResponse;

typedef void (*SaveScreenshotCallback) (ScreenshotResponse response, gpointer *user_data);

typedef struct {
  GdkPixbuf *screenshot;
  GdkPixbuf *preview_image;

  GtkWidget *dialog;
  GtkWidget *save_widget;
  GtkWidget *filename_entry;
  GtkWidget *save_button;
  GtkWidget *copy_button;

  gint drag_x;
  gint drag_y;

  SaveScreenshotCallback callback;
  gpointer user_data;
}  ScreenshotDialog;

ScreenshotDialog *screenshot_dialog_new          (GdkPixbuf              *screenshot,
						  char                   *initial_uri,
						  SaveScreenshotCallback f,
						  gpointer               user_data);

char             *screenshot_dialog_get_uri      (ScreenshotDialog *dialog);
char             *screenshot_dialog_get_folder   (ScreenshotDialog *dialog);
char             *screenshot_dialog_get_filename (ScreenshotDialog *dialog);
void              screenshot_dialog_set_busy     (ScreenshotDialog *dialog,
						  gboolean          busy);

#endif /* __SCREENSHOT_DIALOG_H__ */
