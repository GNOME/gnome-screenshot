/* screenshot-dialog.c - main GNOME Screenshot dialog
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

#include "config.h"

#include "screenshot-config.h"
#include "screenshot-dialog.h"
#include "screenshot-utils.h"
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>

struct _ScreenshotDialog
{
  GtkApplicationWindow parent_instance;

  GdkPixbuf *screenshot;
  GdkPixbuf *preview_image;

  GtkWidget *save_widget;
  GtkWidget *filename_entry;
  GtkWidget *save_button;
  GtkWidget *copy_button;
  GtkWidget *back_button;
  GtkWidget *preview_darea;

  gint drag_x;
  gint drag_y;

  SaveScreenshotCallback callback;
  gpointer user_data;
};

G_DEFINE_TYPE (ScreenshotDialog, screenshot_dialog, GTK_TYPE_APPLICATION_WINDOW)

enum {
  TYPE_IMAGE_PNG,
  LAST_TYPE
};

static GtkTargetEntry drag_types[] =
{
  { "image/png", 0, TYPE_IMAGE_PNG },
};

static void
on_preview_draw (GtkWidget      *drawing_area,
                 cairo_t        *cr,
                 gpointer        data)
{
  ScreenshotDialog *dialog = data;
  gint scale, width, height, image_width, image_height, x, y;

  scale = gtk_widget_get_scale_factor (drawing_area);
  width = gtk_widget_get_allocated_width (drawing_area) * scale;
  height = gtk_widget_get_allocated_height (drawing_area) * scale;

  image_width = gdk_pixbuf_get_width (dialog->screenshot);
  image_height = gdk_pixbuf_get_height (dialog->screenshot);

  if (image_width > width)
    {
      image_height = (gdouble) image_height / image_width * width;
      image_width = width;

    }

  if (image_height > height)
    {
      image_width = (gdouble) image_width / image_height * height;
      image_height = height;
    }

  x = (width - image_width) / 2;
  y = (height - image_height) / 2;

  if (!dialog->preview_image ||
      gdk_pixbuf_get_width (dialog->preview_image) != image_width ||
      gdk_pixbuf_get_height (dialog->preview_image) != image_height)
    {
      g_clear_object (&dialog->preview_image);
      dialog->preview_image = gdk_pixbuf_scale_simple (dialog->screenshot,
                                                       image_width,
                                                       image_height,
                                                       GDK_INTERP_BILINEAR);
    }

  cairo_save (cr);

  cairo_scale (cr, 1.0 / scale, 1.0 / scale);
  gdk_cairo_set_source_pixbuf (cr, dialog->preview_image, x, y);
  cairo_paint (cr);

  cairo_restore (cr);
}

static gboolean
on_preview_button_press_event (GtkWidget      *drawing_area,
                               GdkEventButton *event,
                               gpointer        data)
{
  ScreenshotDialog *dialog = data;

  dialog->drag_x = (int) event->x;
  dialog->drag_y = (int) event->y;

  return FALSE;
}

static gboolean
on_preview_button_release_event (GtkWidget      *drawing_area,
                                 GdkEventButton *event,
                                 gpointer        data)
{
  ScreenshotDialog *dialog = data;

  dialog->drag_x = 0;
  dialog->drag_y = 0;

  return FALSE;
}

static void
drag_data_get (GtkWidget          *widget,
               GdkDragContext     *context,
               GtkSelectionData   *selection_data,
               guint               info,
               guint               time,
               ScreenshotDialog   *dialog)
{
  if (info == TYPE_IMAGE_PNG)
    gtk_selection_data_set_pixbuf (selection_data, dialog->screenshot);
  else
    g_warning ("Unknown type %d", info);
}

static void
drag_begin (GtkWidget        *widget,
            GdkDragContext   *context,
            ScreenshotDialog *dialog)
{
  gtk_drag_set_icon_pixbuf (context, dialog->preview_image,
                            dialog->drag_x, dialog->drag_y);
}

static gboolean
dialog_key_press_cb (GtkWidget *widget,
                     GdkEventKey *event,
                     gpointer user_data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      gtk_widget_destroy (widget);
      return TRUE;
    }

  return FALSE;
}

static void
button_clicked (GtkWidget *button, ScreenshotDialog *dialog)
{
  ScreenshotResponse res;

  if (button == dialog->save_button)
      res = SCREENSHOT_RESPONSE_SAVE;
  else if (button == dialog->copy_button)
      res = SCREENSHOT_RESPONSE_COPY;
  else
      res = SCREENSHOT_RESPONSE_BACK;

  dialog->callback (res, dialog->user_data);
}

static void
setup_drawing_area (ScreenshotDialog *dialog)
{
  g_signal_connect (dialog->preview_darea, "draw", G_CALLBACK (on_preview_draw), dialog);
  g_signal_connect (dialog->preview_darea, "button_press_event", G_CALLBACK (on_preview_button_press_event), dialog);
  g_signal_connect (dialog->preview_darea, "button_release_event", G_CALLBACK (on_preview_button_release_event), dialog);

  /* setup dnd */
  gtk_drag_source_set (dialog->preview_darea,
                       GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
                       drag_types, G_N_ELEMENTS (drag_types),
                       GDK_ACTION_COPY);

  g_signal_connect (G_OBJECT (dialog->preview_darea), "drag_begin",
                    G_CALLBACK (drag_begin), dialog);
  g_signal_connect (G_OBJECT (dialog->preview_darea), "drag_data_get",
                    G_CALLBACK (drag_data_get), dialog);
}

static void
screenshot_dialog_class_init (ScreenshotDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Screenshot/ui/screenshot-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, filename_entry);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, save_widget);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, save_button);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, copy_button);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, back_button);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, preview_darea);
}

static void
screenshot_dialog_init (ScreenshotDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

ScreenshotDialog *
screenshot_dialog_new (GdkPixbuf              *screenshot,
                       char                   *initial_uri,
                       SaveScreenshotCallback f,
                       gpointer               user_data)
{
  g_autoptr(GFile) tmp_file = NULL, parent_file = NULL;
  g_autofree gchar *current_folder = NULL, *current_name = NULL;
  ScreenshotDialog *dialog;
  char *ext;
  gint pos;

  tmp_file = g_file_new_for_uri (initial_uri);
  parent_file = g_file_get_parent (tmp_file);

  current_name = g_file_get_basename (tmp_file);
  current_folder = g_file_get_uri (parent_file);

  dialog = g_object_new (SCREENSHOT_TYPE_DIALOG, NULL);
  dialog->screenshot = screenshot;
  dialog->callback = f;
  dialog->user_data = user_data;

  gtk_window_set_application (GTK_WINDOW (dialog), GTK_APPLICATION (g_application_get_default ()));
  gtk_widget_realize (GTK_WIDGET (dialog));
  g_signal_connect (dialog, "key-press-event",
                    G_CALLBACK (dialog_key_press_cb),
                    NULL);

  gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog->save_widget), current_folder);
  gtk_entry_set_text (GTK_ENTRY (dialog->filename_entry), current_name);

  g_signal_connect (dialog->save_button, "clicked", G_CALLBACK (button_clicked), dialog);
  g_signal_connect (dialog->copy_button, "clicked", G_CALLBACK (button_clicked), dialog);
  g_signal_connect (dialog->back_button, "clicked", G_CALLBACK (button_clicked), dialog);

  setup_drawing_area (dialog);

  gtk_widget_show_all (GTK_WIDGET (dialog));

  /* select the name of the file but leave out the extension if there's any;
   * the dialog must be realized for select_region to work
   */
  ext = g_utf8_strrchr (current_name, -1, '.');
  if (ext)
    pos = g_utf8_strlen (current_name, -1) - g_utf8_strlen (ext, -1);
  else
    pos = -1;

  gtk_widget_grab_focus (dialog->filename_entry);
  gtk_editable_select_region (GTK_EDITABLE (dialog->filename_entry),
                              0,
                              pos);

  return dialog;
}

char *
screenshot_dialog_get_uri (ScreenshotDialog *dialog)
{
  g_autofree gchar *folder = NULL, *file = NULL, *tmp = NULL;

  folder = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog->save_widget));
  tmp = screenshot_dialog_get_filename (dialog);
  file = g_uri_escape_string (tmp, NULL, FALSE);

  return g_build_filename (folder, file, NULL);
}

char *
screenshot_dialog_get_folder (ScreenshotDialog *dialog)
{
  return gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog->save_widget));
}

char *
screenshot_dialog_get_filename (ScreenshotDialog *dialog)
{
  g_autoptr(GError) error = NULL;
  const gchar *file_name;
  gchar *tmp;

  file_name = gtk_entry_get_text (GTK_ENTRY (dialog->filename_entry));
  tmp = g_filename_from_utf8 (file_name, -1, NULL, NULL, &error);

  if (error != NULL)
    {
      g_warning ("Unable to convert `%s' to valid UTF-8: %s\n"
                 "Falling back to default file.",
                 file_name,
                 error->message);
      tmp = g_strdup (_("Screenshot.png"));
    }

  return tmp;
}

void
screenshot_dialog_set_busy (ScreenshotDialog *dialog,
                            gboolean          busy)
{
  GdkWindow *window;

  window = gtk_widget_get_window (GTK_WIDGET (dialog));

  if (busy)
    {
      g_autoptr(GdkCursor) cursor = NULL;
      GdkDisplay *display;
      /* Change cursor to busy */
      display = gtk_widget_get_display (GTK_WIDGET (dialog));
      cursor = gdk_cursor_new_for_display (display, GDK_WATCH);
      gdk_window_set_cursor (window, cursor);
    }
  else
    {
      gdk_window_set_cursor (window, NULL);
    }

  gtk_widget_set_sensitive (GTK_WIDGET (dialog), ! busy);

  gdk_flush ();
}

GtkWidget *
screenshot_dialog_get_dialog (ScreenshotDialog *dialog)
{
  return GTK_WIDGET (dialog);
}

GtkWidget *
screenshot_dialog_get_filename_entry (ScreenshotDialog *dialog)
{
  return dialog->filename_entry;
}

