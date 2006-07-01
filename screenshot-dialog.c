#include <config.h>
#include <string.h>
#include <stdlib.h>

#include "screenshot-dialog.h"
#include "screenshot-save.h"
#include <libgnomevfs/gnome-vfs.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs-utils.h>


static GtkTargetEntry drag_types[] =
{
  { "image/png", 0, 0 },
  { "x-special/gnome-icon-list", 0, 0 },
  { "text/uri-list", 0, 0 }
};

struct ScreenshotDialog
{
  GladeXML *xml;
  GdkPixbuf *screenshot;
  GdkPixbuf *preview_image;
  GtkWidget *save_widget;
  GtkWidget *filename_entry;
  gint drag_x;
  gint drag_y;
};

static gboolean
on_toplevel_key_press_event (GtkWidget *widget,
			     GdkEventKey *key)
{
  if (key->keyval == GDK_F1)
    {
      gtk_dialog_response (GTK_DIALOG (widget), GTK_RESPONSE_HELP);
      return TRUE;
    }

  return FALSE;
}

static void
on_preview_expose_event (GtkWidget      *drawing_area,
			 GdkEventExpose *event,
			 gpointer        data)
{
  ScreenshotDialog *dialog = data;
  GdkPixbuf *pixbuf = NULL;
  gboolean free_pixbuf = FALSE;

  /* Stolen from GtkImage.  I really should just make the drawing area an
   * image some day */
  if (GTK_WIDGET_STATE (drawing_area) != GTK_STATE_NORMAL)
    {
      GtkIconSource *source;

      source = gtk_icon_source_new ();
      gtk_icon_source_set_pixbuf (source, dialog->preview_image);
      gtk_icon_source_set_size (source, GTK_ICON_SIZE_SMALL_TOOLBAR);
      gtk_icon_source_set_size_wildcarded (source, FALSE);
                  
      pixbuf = gtk_style_render_icon (drawing_area->style,
				      source,
				      gtk_widget_get_direction (drawing_area),
				      GTK_WIDGET_STATE (drawing_area),
				      (GtkIconSize) -1,
				      drawing_area,
				      "gtk-image");
      free_pixbuf = TRUE;
      gtk_icon_source_free (source);
    }
  else
    {
      pixbuf = g_object_ref (dialog->preview_image);
    }
  
  /* FIXME: Draw it insensitive in that case */
  gdk_draw_pixbuf (drawing_area->window,
		   drawing_area->style->white_gc,
		   pixbuf,
		   event->area.x,
		   event->area.y,
		   event->area.x,
		   event->area.y,
		   event->area.width,
		   event->area.height,
		   GDK_RGB_DITHER_NORMAL,
		   0, 0);

  g_object_unref (pixbuf);
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
	       gpointer            data)
{
	char *string;

	string = g_strdup_printf ("file:%s\r\n",
				  screenshot_save_get_filename ());
	gtk_selection_data_set (selection_data,
				selection_data->target,
				8, string, strlen (string)+1);
	g_free (string);
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
		       char      *initial_uri,
		       gboolean   take_window_shot)
{
  ScreenshotDialog *dialog;
  GtkWidget *toplevel;
  GtkWidget *preview_darea;
  GtkWidget *aspect_frame;
  GtkWidget *file_chooser_box;
  gint width, height;
  char *current_folder;
  char *current_name;
  char *ext;
  gint pos;
  GnomeVFSURI *tmp_uri;
  GnomeVFSURI *parent_uri;

  tmp_uri = gnome_vfs_uri_new (initial_uri);
  parent_uri = gnome_vfs_uri_get_parent (tmp_uri);

  current_name = gnome_vfs_uri_extract_short_name (tmp_uri);
  current_folder = gnome_vfs_uri_to_string (parent_uri, GNOME_VFS_URI_HIDE_NONE);
  gnome_vfs_uri_unref (tmp_uri);
  gnome_vfs_uri_unref (parent_uri);

  dialog = g_new0 (ScreenshotDialog, 1);

  dialog->xml = glade_xml_new (GLADEDIR "/gnome-screenshot.glade", NULL, NULL);
  dialog->screenshot = screenshot;

  if (dialog->xml == NULL)
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new (NULL, 0,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("Glade file for the screenshot program is missing.\n"
					 "Please check your installation of gnome-utils"));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      exit (1);
    }

  width = gdk_pixbuf_get_width (screenshot);
  height = gdk_pixbuf_get_height (screenshot);

  width /= 5;
  height /= 5;

  toplevel = glade_xml_get_widget (dialog->xml, "toplevel");
  aspect_frame = glade_xml_get_widget (dialog->xml, "aspect_frame");
  preview_darea = glade_xml_get_widget (dialog->xml, "preview_darea");
  dialog->filename_entry = glade_xml_get_widget (dialog->xml, "filename_entry");
  file_chooser_box = glade_xml_get_widget (dialog->xml, "file_chooser_box");

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
  g_signal_connect (toplevel, "key_press_event", G_CALLBACK (on_toplevel_key_press_event), dialog);
  g_signal_connect (preview_darea, "expose_event", G_CALLBACK (on_preview_expose_event), dialog);
  g_signal_connect (preview_darea, "button_press_event", G_CALLBACK (on_preview_button_press_event), dialog);
  g_signal_connect (preview_darea, "button_release_event", G_CALLBACK (on_preview_button_release_event), dialog);
  g_signal_connect (preview_darea, "configure_event", G_CALLBACK (on_preview_configure_event), dialog);

  if (take_window_shot)
    gtk_frame_set_shadow_type (GTK_FRAME (aspect_frame), GTK_SHADOW_NONE);
  else
    gtk_frame_set_shadow_type (GTK_FRAME (aspect_frame), GTK_SHADOW_IN);

  /* setup dnd */
  g_signal_connect (G_OBJECT (preview_darea), "drag_begin",
		    G_CALLBACK (drag_begin), dialog);
  g_signal_connect (G_OBJECT (preview_darea), "drag_data_get",
		    G_CALLBACK (drag_data_get), dialog);

  gtk_widget_show_all (toplevel);

  /* select the name of the file but leave out the extension if there's any;
   * the dialog must be realized for select_region to work
   */
  ext = g_utf8_strrchr (current_name, -1, '.');
  if (ext)
    pos = ext - current_name;
  else
    pos = -1;

  gtk_editable_select_region (GTK_EDITABLE (dialog->filename_entry),
			      0,
			      pos);
  
  g_free (current_name);

  return dialog;
}

void
screenshot_dialog_enable_dnd (ScreenshotDialog *dialog)
{
  GtkWidget *preview_darea;

  g_return_if_fail (dialog != NULL);

  preview_darea = glade_xml_get_widget (dialog->xml, "preview_darea");
  gtk_drag_source_set (preview_darea,
		       GDK_BUTTON1_MASK|GDK_BUTTON3_MASK,
		       drag_types, G_N_ELEMENTS (drag_types),
		       GDK_ACTION_COPY);
}

GtkWidget *
screenshot_dialog_get_toplevel (ScreenshotDialog *dialog)
{
  return glade_xml_get_widget (dialog->xml, "toplevel");
}

char *
screenshot_dialog_get_uri (ScreenshotDialog *dialog)
{
  gchar *folder;
  const gchar *file_name;
  gchar *uri, *file, *tmp;

  folder = gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog->save_widget));
  file_name = gtk_entry_get_text (GTK_ENTRY (dialog->filename_entry));

  tmp = g_filename_from_utf8 (file_name, -1, NULL, NULL, NULL);
  file = gnome_vfs_escape_host_and_path_string (tmp);
  uri = g_build_filename (folder, file, NULL);
  g_free (folder);
  g_free (tmp);
  g_free (file);

  return uri;
}

char *
screenshot_dialog_get_folder (ScreenshotDialog *dialog)
{
  return gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog->save_widget));
}
void
screenshot_dialog_set_busy (ScreenshotDialog *dialog,
			    gboolean          busy)
{
  GtkWidget *toplevel;

  toplevel = screenshot_dialog_get_toplevel (dialog);

  if (busy)
    {
      GdkCursor *cursor;
      /* Change cursor to busy */
      cursor = gdk_cursor_new (GDK_WATCH);
      gdk_window_set_cursor (toplevel->window, cursor);
      gdk_cursor_unref (cursor);
    }
  else
    {
      gdk_window_set_cursor (toplevel->window, NULL);
    }

  gtk_widget_set_sensitive (toplevel, ! busy);

  gdk_flush ();
}
