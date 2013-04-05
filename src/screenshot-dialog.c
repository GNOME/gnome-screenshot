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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 */

#include <config.h>
#include <string.h>
#include <stdlib.h>

#include "screenshot-config.h"
#include "screenshot-dialog.h"
#include <glib/gi18n.h>
#include <gio/gio.h>

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
on_preview_configure_event (GtkWidget         *drawing_area,
			    GdkEventConfigure *event,
			    gpointer           data)
{
  ScreenshotDialog *dialog = data;

  if (dialog->preview_image)
    g_object_unref (G_OBJECT (dialog->preview_image));

  dialog->preview_image = gdk_pixbuf_scale_simple (dialog->screenshot,
						   event->width,
						   event->height,
						   GDK_INTERP_BILINEAR);
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

ScreenshotDialog *
screenshot_dialog_new (GdkPixbuf *screenshot,
		       char      *initial_uri)
{
  ScreenshotDialog *dialog;
  GtkBuilder *ui;
  GtkWidget *preview_darea;
  GtkWidget *aspect_frame;
  GtkWidget *file_chooser_box;
  gint width, height;
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

  ui = gtk_builder_new ();
  res = gtk_builder_add_from_resource (ui, "/org/gnome/screenshot/screenshot-dialog.ui", NULL);
  g_assert (res != 0);

  width = gdk_pixbuf_get_width (screenshot);
  height = gdk_pixbuf_get_height (screenshot);

  width /= 5;
  height /= 5;

  dialog->dialog = GTK_WIDGET (gtk_builder_get_object (ui, "toplevel"));
  gtk_window_set_application (GTK_WINDOW (dialog->dialog), GTK_APPLICATION (g_application_get_default ()));
  gtk_window_set_resizable (GTK_WINDOW (dialog->dialog), FALSE);
  gtk_window_set_title (GTK_WINDOW (dialog->dialog), _("Save Screenshot"));
  gtk_window_set_position (GTK_WINDOW (dialog->dialog), GTK_WIN_POS_CENTER);
  gtk_widget_realize (dialog->dialog);

  aspect_frame = GTK_WIDGET (gtk_builder_get_object (ui, "aspect_frame"));
  preview_darea = GTK_WIDGET (gtk_builder_get_object (ui, "preview_darea"));
  dialog->filename_entry = GTK_WIDGET (gtk_builder_get_object (ui, "filename_entry"));
  file_chooser_box = GTK_WIDGET (gtk_builder_get_object (ui, "file_chooser_box"));
  g_object_unref (ui);

  dialog->save_widget = gtk_file_chooser_button_new (_("Select a folder"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (dialog->save_widget), FALSE);
  gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog->save_widget), current_folder);
  gtk_entry_set_text (GTK_ENTRY (dialog->filename_entry), current_name);

  gtk_box_pack_start (GTK_BOX (file_chooser_box), dialog->save_widget, TRUE, TRUE, 0);
  g_free (current_folder);

  gtk_widget_set_size_request (preview_darea, width, height);
  gtk_aspect_frame_set (GTK_ASPECT_FRAME (aspect_frame), 0.0, 0.5,
			gdk_pixbuf_get_width (screenshot)/
			(gfloat) gdk_pixbuf_get_height (screenshot),
			FALSE);
  g_signal_connect (preview_darea, "draw", G_CALLBACK (on_preview_draw), dialog);
  g_signal_connect (preview_darea, "button_press_event", G_CALLBACK (on_preview_button_press_event), dialog);
  g_signal_connect (preview_darea, "button_release_event", G_CALLBACK (on_preview_button_release_event), dialog);
  g_signal_connect (preview_darea, "configure_event", G_CALLBACK (on_preview_configure_event), dialog);

  gtk_drag_source_set (preview_darea,
		       GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
		       drag_types, G_N_ELEMENTS (drag_types),
		       GDK_ACTION_COPY);

  if (screenshot_config->take_window_shot)
    gtk_frame_set_shadow_type (GTK_FRAME (aspect_frame), GTK_SHADOW_NONE);
  else
    gtk_frame_set_shadow_type (GTK_FRAME (aspect_frame), GTK_SHADOW_IN);

  /* setup dnd */
  g_signal_connect (G_OBJECT (preview_darea), "drag_begin",
		    G_CALLBACK (drag_begin), dialog);
  g_signal_connect (G_OBJECT (preview_darea), "drag_data_get",
		    G_CALLBACK (drag_data_get), dialog);

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
