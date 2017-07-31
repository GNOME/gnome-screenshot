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
  GtkStyleContext *context;
  int width, height;

  width = gtk_widget_get_allocated_width (drawing_area);
  height = gtk_widget_get_allocated_height (drawing_area);

  if (!dialog->preview_image ||
      gdk_pixbuf_get_width (dialog->preview_image) != width ||
      gdk_pixbuf_get_height (dialog->preview_image) != height)
    {
      g_clear_object (&dialog->preview_image);
      dialog->preview_image = gdk_pixbuf_scale_simple (dialog->screenshot,
                                                       width,
                                                       height,
                                                       GDK_INTERP_BILINEAR);
    }

  context = gtk_widget_get_style_context (drawing_area);
  gtk_style_context_save (context);

  gtk_style_context_set_state (context, gtk_widget_get_state_flags (drawing_area));
  gtk_render_icon (context, cr, dialog->preview_image, 0, 0);

  gtk_style_context_restore (context);
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
setup_drawing_area (ScreenshotDialog *dialog, GtkBuilder *ui)
{
  GtkWidget *preview_darea;
  GtkWidget *aspect_frame;
  gint width, height, scale_factor;

  aspect_frame = GTK_WIDGET (gtk_builder_get_object (ui, "aspect_frame"));
  preview_darea = GTK_WIDGET (gtk_builder_get_object (ui, "preview_darea"));

  width = gdk_pixbuf_get_width (dialog->screenshot);
  height = gdk_pixbuf_get_height (dialog->screenshot);
  scale_factor = gtk_widget_get_scale_factor (dialog->dialog);

  width /= 5 * scale_factor;
  height /= 5 * scale_factor;

  gtk_widget_set_size_request (preview_darea, width, height);
  gtk_aspect_frame_set (GTK_ASPECT_FRAME (aspect_frame), 0.0, 0.5,
			(gfloat) width / (gfloat) height,
			FALSE);

  if (screenshot_config->take_window_shot)
    gtk_frame_set_shadow_type (GTK_FRAME (aspect_frame), GTK_SHADOW_NONE);
  else
    gtk_frame_set_shadow_type (GTK_FRAME (aspect_frame), GTK_SHADOW_IN);

  g_signal_connect (preview_darea, "draw", G_CALLBACK (on_preview_draw), dialog);
  g_signal_connect (preview_darea, "button_press_event", G_CALLBACK (on_preview_button_press_event), dialog);
  g_signal_connect (preview_darea, "button_release_event", G_CALLBACK (on_preview_button_release_event), dialog);

  /* setup dnd */
  gtk_drag_source_set (preview_darea,
		       GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
		       drag_types, G_N_ELEMENTS (drag_types),
		       GDK_ACTION_COPY);

  g_signal_connect (G_OBJECT (preview_darea), "drag_begin",
		    G_CALLBACK (drag_begin), dialog);
  g_signal_connect (G_OBJECT (preview_darea), "drag_data_get",
		    G_CALLBACK (drag_data_get), dialog);
}

ScreenshotDialog *
screenshot_dialog_new (GdkPixbuf              *screenshot,
		       char                   *initial_uri,
		       SaveScreenshotCallback f,
		       gpointer               user_data)
{
  ScreenshotDialog *dialog;
  GtkBuilder *ui;
  char *current_folder;
  char *current_name;
  char *ext;
  gint pos;
  GFile *tmp_file;
  GFile *parent_file;
  guint res;

  tmp_file = g_file_new_for_uri (initial_uri);
  parent_file = g_file_get_parent (tmp_file);

  current_name = g_file_get_basename (tmp_file);
  current_folder = g_file_get_uri (parent_file);
  g_object_unref (tmp_file);
  g_object_unref (parent_file);

  dialog = g_new0 (ScreenshotDialog, 1);
  dialog->screenshot = screenshot;
  dialog->callback = f;
  dialog->user_data = user_data;

  ui = gtk_builder_new ();
  res = gtk_builder_add_from_resource (ui, "/org/gnome/screenshot/screenshot-dialog.ui", NULL);
  g_assert (res != 0);

  dialog->dialog = GTK_WIDGET (gtk_builder_get_object (ui, "toplevel"));
  gtk_window_set_application (GTK_WINDOW (dialog->dialog), GTK_APPLICATION (g_application_get_default ()));
  gtk_widget_realize (dialog->dialog);
  g_signal_connect (dialog->dialog, "key-press-event",
                    G_CALLBACK (dialog_key_press_cb),
                    NULL);

  dialog->filename_entry = GTK_WIDGET (gtk_builder_get_object (ui, "filename_entry"));
  dialog->save_widget = GTK_WIDGET (gtk_builder_get_object (ui, "save_widget"));
  gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog->save_widget), current_folder);
  gtk_entry_set_text (GTK_ENTRY (dialog->filename_entry), current_name);

  dialog->save_button = GTK_WIDGET (gtk_builder_get_object (ui, "save_button"));
  g_signal_connect (dialog->save_button, "clicked", G_CALLBACK (button_clicked), dialog);
  dialog->copy_button = GTK_WIDGET (gtk_builder_get_object (ui, "copy_button"));
  g_signal_connect (dialog->copy_button, "clicked", G_CALLBACK (button_clicked), dialog);
  dialog->back_button = GTK_WIDGET (gtk_builder_get_object (ui, "back_button"));
  g_signal_connect (dialog->back_button, "clicked", G_CALLBACK (button_clicked), dialog);

  setup_drawing_area (dialog, ui);

  gtk_widget_show_all (dialog->dialog);

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
  
  g_free (current_name);
  g_free (current_folder);
  g_object_unref (ui);

  return dialog;
}

char *
screenshot_dialog_get_uri (ScreenshotDialog *dialog)
{
  gchar *folder, *file;
  gchar *uri;
  gchar *tmp;

  folder = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog->save_widget));
  tmp = screenshot_dialog_get_filename (dialog);
  file = g_uri_escape_string (tmp, NULL, FALSE);
  g_free (tmp);
  uri = g_build_filename (folder, file, NULL);

  g_free (folder);
  g_free (file);

  return uri;
}

char *
screenshot_dialog_get_folder (ScreenshotDialog *dialog)
{
  return gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog->save_widget));
}

char *
screenshot_dialog_get_filename (ScreenshotDialog *dialog)
{
  const gchar *file_name;
  gchar *tmp;
  GError *error;

  file_name = gtk_entry_get_text (GTK_ENTRY (dialog->filename_entry));

  error = NULL;
  tmp = g_filename_from_utf8 (file_name, -1, NULL, NULL, &error);
  if (error)
    {
      g_warning ("Unable to convert `%s' to valid UTF-8: %s\n"
                 "Falling back to default file.",
                 file_name,
                 error->message);
      g_error_free (error);
      tmp = g_strdup (_("Screenshot.png"));
    }

  return tmp;
}

void
screenshot_dialog_set_busy (ScreenshotDialog *dialog,
			    gboolean          busy)
{
  GdkWindow *window;

  window = gtk_widget_get_window (dialog->dialog);

  if (busy)
    {
      GdkCursor *cursor;
      /* Change cursor to busy */
      cursor = gdk_cursor_new (GDK_WATCH);
      gdk_window_set_cursor (window, cursor);
      g_object_unref (cursor);
    }
  else
    {
      gdk_window_set_cursor (window, NULL);
    }

  gtk_widget_set_sensitive (dialog->dialog, ! busy);

  gdk_flush ();
}
