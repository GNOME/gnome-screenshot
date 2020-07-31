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
  HdyApplicationWindow parent_instance;

  GdkPixbuf *screenshot;
  GdkPixbuf *preview_image;

  GtkWidget *save_widget;
  GtkWidget *filename_entry;
  GtkWidget *preview_darea;

  gint drag_x;
  gint drag_y;
};

G_DEFINE_TYPE (ScreenshotDialog, screenshot_dialog, HDY_TYPE_APPLICATION_WINDOW)

enum {
  SIGNAL_SAVE,
  SIGNAL_COPY,
  SIGNAL_BACK,
  N_SIGNALS,
};

static guint signals[N_SIGNALS];

enum {
  TYPE_IMAGE_PNG,
  LAST_TYPE
};

static GtkTargetEntry drag_types[] =
{
  { "image/png", 0, TYPE_IMAGE_PNG },
};

static void
preview_draw_cb (GtkWidget        *drawing_area,
                 cairo_t          *cr,
                 ScreenshotDialog *self)
{
  gint scale, width, height, image_width, image_height, x, y;

  scale = gtk_widget_get_scale_factor (drawing_area);
  width = gtk_widget_get_allocated_width (drawing_area) * scale;
  height = gtk_widget_get_allocated_height (drawing_area) * scale;

  image_width = gdk_pixbuf_get_width (self->screenshot);
  image_height = gdk_pixbuf_get_height (self->screenshot);

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

  if (!self->preview_image ||
      gdk_pixbuf_get_width (self->preview_image) != image_width ||
      gdk_pixbuf_get_height (self->preview_image) != image_height)
    {
      g_clear_object (&self->preview_image);
      self->preview_image = gdk_pixbuf_scale_simple (self->screenshot,
                                                     image_width,
                                                     image_height,
                                                     GDK_INTERP_BILINEAR);
    }

  cairo_save (cr);

  cairo_scale (cr, 1.0 / scale, 1.0 / scale);
  gdk_cairo_set_source_pixbuf (cr, self->preview_image, x, y);
  cairo_paint (cr);

  cairo_restore (cr);
}

static gboolean
preview_button_press_event_cb (GtkWidget        *drawing_area,
                               GdkEventButton   *event,
                               ScreenshotDialog *self)
{
  self->drag_x = (int) event->x;
  self->drag_y = (int) event->y;

  return FALSE;
}

static gboolean
preview_button_release_event_cb (GtkWidget        *drawing_area,
                                 GdkEventButton   *event,
                                 ScreenshotDialog *self)
{
  self->drag_x = 0;
  self->drag_y = 0;

  return FALSE;
}

static void
drag_begin_cb (GtkWidget        *widget,
               GdkDragContext   *context,
               ScreenshotDialog *self)
{
  gtk_drag_set_icon_pixbuf (context, self->preview_image,
                            self->drag_x, self->drag_y);
}

static void
drag_data_get_cb (GtkWidget        *widget,
                  GdkDragContext   *context,
                  GtkSelectionData *selection_data,
                  guint             info,
                  guint             time,
                  ScreenshotDialog *self)
{
  if (info == TYPE_IMAGE_PNG)
    gtk_selection_data_set_pixbuf (selection_data, self->screenshot);
  else
    g_warning ("Unknown type %d", info);
}

static gboolean
key_press_cb (GtkWidget        *widget,
              GdkEventKey      *event,
              ScreenshotDialog *self)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      gtk_widget_destroy (widget);
      return TRUE;
    }

  return FALSE;
}

static void
back_clicked_cb (GtkButton        *button,
                 ScreenshotDialog *self)
{
  g_signal_emit (self, signals[SIGNAL_BACK], 0);
}

static void
save_clicked_cb (GtkButton        *button,
                 ScreenshotDialog *self)
{
  g_signal_emit (self, signals[SIGNAL_SAVE], 0);
}

static void
copy_clicked_cb (GtkButton        *button,
                 ScreenshotDialog *self)
{
  g_signal_emit (self, signals[SIGNAL_COPY], 0);
}

static void
screenshot_dialog_class_init (ScreenshotDialogClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  signals[SIGNAL_SAVE] =
    g_signal_new ("save",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);

  signals[SIGNAL_COPY] =
    g_signal_new ("copy",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);

  signals[SIGNAL_BACK] =
    g_signal_new ("back",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/org/gnome/Screenshot/ui/screenshot-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, filename_entry);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, save_widget);
  gtk_widget_class_bind_template_child (widget_class, ScreenshotDialog, preview_darea);
  gtk_widget_class_bind_template_callback (widget_class, key_press_cb);
  gtk_widget_class_bind_template_callback (widget_class, back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, save_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, copy_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, preview_draw_cb);
  gtk_widget_class_bind_template_callback (widget_class, preview_button_press_event_cb);
  gtk_widget_class_bind_template_callback (widget_class, preview_button_release_event_cb);
  gtk_widget_class_bind_template_callback (widget_class, drag_begin_cb);
  gtk_widget_class_bind_template_callback (widget_class, drag_data_get_cb);
}

static void
screenshot_dialog_init (ScreenshotDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

ScreenshotDialog *
screenshot_dialog_new (GtkApplication *app,
                       GdkPixbuf      *screenshot,
                       char           *initial_uri)
{
  g_autoptr(GFile) tmp_file = NULL, parent_file = NULL;
  g_autofree gchar *current_folder = NULL, *current_name = NULL;
  ScreenshotDialog *self;
  char *ext;
  gint pos;

  tmp_file = g_file_new_for_uri (initial_uri);
  parent_file = g_file_get_parent (tmp_file);

  current_name = g_file_get_basename (tmp_file);
  current_folder = g_file_get_uri (parent_file);

  self = g_object_new (SCREENSHOT_TYPE_DIALOG,
                       "application", app,
                       NULL);

  self->screenshot = screenshot;

  gtk_widget_realize (GTK_WIDGET (self));

  gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (self->save_widget), current_folder);
  gtk_entry_set_text (GTK_ENTRY (self->filename_entry), current_name);

  /* setup dnd */
  gtk_drag_source_set (self->preview_darea,
                       GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
                       drag_types, G_N_ELEMENTS (drag_types),
                       GDK_ACTION_COPY);

  gtk_widget_show_all (GTK_WIDGET (self));

  /* select the name of the file but leave out the extension if there's any;
   * the dialog must be realized for select_region to work
   */
  ext = g_utf8_strrchr (current_name, -1, '.');
  if (ext)
    pos = g_utf8_strlen (current_name, -1) - g_utf8_strlen (ext, -1);
  else
    pos = -1;

  gtk_widget_grab_focus (self->filename_entry);
  gtk_editable_select_region (GTK_EDITABLE (self->filename_entry),
                              0,
                              pos);

  return self;
}

char *
screenshot_dialog_get_uri (ScreenshotDialog *self)
{
  g_autofree gchar *folder = NULL, *file = NULL, *tmp = NULL;

  folder = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (self->save_widget));
  tmp = screenshot_dialog_get_filename (self);
  file = g_uri_escape_string (tmp, NULL, FALSE);

  return g_build_filename (folder, file, NULL);
}

char *
screenshot_dialog_get_folder (ScreenshotDialog *self)
{
  return gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (self->save_widget));
}

char *
screenshot_dialog_get_filename (ScreenshotDialog *self)
{
  g_autoptr(GError) error = NULL;
  const gchar *file_name;
  gchar *tmp;

  file_name = gtk_entry_get_text (GTK_ENTRY (self->filename_entry));
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
screenshot_dialog_set_busy (ScreenshotDialog *self,
                            gboolean          busy)
{
  GdkWindow *window;

  window = gtk_widget_get_window (GTK_WIDGET (self));

  if (busy)
    {
      g_autoptr(GdkCursor) cursor = NULL;
      GdkDisplay *display;
      /* Change cursor to busy */
      display = gtk_widget_get_display (GTK_WIDGET (self));
      cursor = gdk_cursor_new_for_display (display, GDK_WATCH);
      gdk_window_set_cursor (window, cursor);
    }
  else
    {
      gdk_window_set_cursor (window, NULL);
    }

  gtk_widget_set_sensitive (GTK_WIDGET (self), !busy);
}

GtkWidget *
screenshot_dialog_get_filename_entry (ScreenshotDialog *self)
{
  return self->filename_entry;
}
