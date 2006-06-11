/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* dfos-xfer-progress-dialog.c - Progress dialog for transfer operations in the
   GNOME Desktop File Operation Service.

   Copyright (C) 1999, 2000 Free Software Foundation
   Copyright (C) 2000, 2001 Eazel Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; see the file COPYING.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors: 
   	Ettore Perazzoli <ettore@gnu.org> 
   	Pavel Cisler <pavel@eazel.com> 
*/

#include <config.h>
#include <string.h>
#include "gnome-egg-xfer-dialog.h"
#include "gnome-egg-xfer-dialog-icons.h"

#include <gtk/gtkhbox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkmain.h>
#include <gtk/gtkprogressbar.h>
#include <gtk/gtkstock.h>
#include <gtk/gtktable.h>
#include <gtk/gtkvbox.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>

/* The default width of the progress dialog. It will be wider
 * only if the font is really huge and the fixed labels don't
 * fit in the window otherwise.
 */
#define PROGRESS_DIALOG_WIDTH 400

#define OUTER_BORDER       5
#define VERTICAL_SPACING   8
#define HORIZONTAL_SPACING 3

#define MINIMUM_TIME_UP    1000

#define SHOW_TIMEOUT	   1200
#define TIME_REMAINING_TIMEOUT 1000

static GdkPixbuf *empty_jar_pixbuf, *full_jar_pixbuf;

static void gnome_egg_xfer_dialog_class_init (GnomeEggXferDialogClass *klass);
static void gnome_egg_xfer_dialog_init       (GnomeEggXferDialog      *dialog);

G_DEFINE_TYPE (GnomeEggXferDialog,
	       gnome_egg_xfer_dialog,
	       GTK_TYPE_DIALOG)

struct GnomeEggXferDialogPrivate {
	GtkWidget *progress_title_label;
	GtkWidget *progress_count_label;
	GtkWidget *operation_name_label;
	GtkWidget *item_name;
	GtkWidget *from_label;
	GtkWidget *from_path_label;
	GtkWidget *to_label;
	GtkWidget *to_path_label;

	GtkWidget *progress_bar;

	const char *from_prefix;
	const char *to_prefix;

	gulong files_total;
	
	GnomeVFSFileSize bytes_copied;
	GnomeVFSFileSize bytes_total;

	/* system time (microseconds) when show timeout was started */
	gint64 start_time;

	/* system time (microseconds) when dialog was mapped */
	gint64 show_time;
	
	/* time remaining in show timeout if it's paused and resumed */
	guint remaining_time;

	guint delayed_close_timeout_id;
	guint delayed_show_timeout_id;

	/* system time (microseconds) when first file transfer began */
	gint64 first_transfer_time;
	guint time_remaining_timeout_id;
	
	int progress_jar_position;
};

/* Private functions. */

static void
gnome_egg_xfer_dialog_update_icon (GnomeEggXferDialog *progress,
					       double fraction)
{
	GdkPixbuf *pixbuf;
	int position;

	position = gdk_pixbuf_get_height (empty_jar_pixbuf) * (1 - fraction);

	if (position == progress->priv->progress_jar_position) {
		return;
	}

	progress->priv->progress_jar_position = position;
	
	pixbuf = gdk_pixbuf_copy (empty_jar_pixbuf);
	gdk_pixbuf_copy_area (full_jar_pixbuf,
			      0, position,
			      gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf) - position,
			      pixbuf,
			      0, position);

	gtk_window_set_icon (GTK_WINDOW (progress), pixbuf);
	g_object_unref (pixbuf);
}


static void
gnome_egg_xfer_dialog_update (GnomeEggXferDialog *progress)
{
	double fraction;

	if (progress->priv->bytes_total == 0) {
		/* We haven't set up the file count yet, do not update
		 * the progress bar until we do.
		 */
		return;
	}

	fraction = (double)progress->priv->bytes_copied /
		progress->priv->bytes_total;
	
	gtk_progress_bar_set_fraction
		(GTK_PROGRESS_BAR (progress->priv->progress_bar),
		 fraction);
	gnome_egg_xfer_dialog_update_icon (progress, fraction);
}

/* Code copied from libeel.
 */
static char *
eel_make_valid_utf8 (const char *name)
{
	GString *string;
	const char *remainder, *invalid;
	int remaining_bytes, valid_bytes;

	string = NULL;
	remainder = name;
	remaining_bytes = strlen (name);

	while (remaining_bytes != 0) {
		if (g_utf8_validate (remainder, remaining_bytes, &invalid)) {
			break;
		}
		valid_bytes = invalid - remainder;

		if (string == NULL) {
			string = g_string_sized_new (remaining_bytes);
		}
		g_string_append_len (string, remainder, valid_bytes);
		g_string_append_c (string, '?');

		remaining_bytes -= valid_bytes + 1;
		remainder = invalid + 1;
	}

	if (string == NULL) {
		return g_strdup (name);
	}

	g_string_append (string, remainder);
	g_string_append (string, _(" (invalid Unicode)"));
	g_assert (g_utf8_validate (string->str, -1, NULL));

	return g_string_free (string, FALSE);
}

static void
eel_gtk_label_make_bold (GtkLabel *label)
{
	PangoFontDescription *font_desc;

	font_desc = pango_font_description_new ();

	pango_font_description_set_weight (font_desc,
					   PANGO_WEIGHT_BOLD);

	/* This will only affect the weight of the font, the rest is
	 * from the current state of the widget, which comes from the
	 * theme or user prefs, since the font desc only has the
	 * weight flag turned on.
	 */
	gtk_widget_modify_font (GTK_WIDGET (label), font_desc);

	pango_font_description_free (font_desc);
}

static gint64
eel_get_system_time (void)
{
	GTimeVal current_time;

	g_get_current_time (&current_time);

	return (gint64)current_time.tv_usec + (gint64)current_time.tv_sec * G_GINT64_CONSTANT (1000000);
}


static void
set_text_unescaped_trimmed (GtkWidget *label, const char *text)
{
	char *unescaped_text;
	char *unescaped_utf8;
	
	if (text == NULL || text[0] == '\0') {
		gtk_label_set_text (GTK_LABEL (label), "");
		return;
	}
	
	unescaped_text = gnome_vfs_unescape_string_for_display (text);
	unescaped_utf8 = eel_make_valid_utf8 (unescaped_text);
	gtk_label_set_text (GTK_LABEL (label), unescaped_utf8);
	g_free (unescaped_utf8);
	g_free (unescaped_text);
}

/* This is just to make sure the dialog is not closed without explicit
 * intervention.
 */
static void
close_callback (GtkDialog *dialog)
{
}

/* GObject methods. */
static void
gnome_egg_xfer_dialog_finalize (GObject *object)
{
	GnomeEggXferDialog *progress;

	progress = GNOME_EGG_XFER_DIALOG (object);

	g_free (progress->priv);

	G_OBJECT_CLASS (gnome_egg_xfer_dialog_parent_class)->finalize (object);
}

/* GtkObject methods.  */

static void
gnome_egg_xfer_dialog_destroy (GtkObject *object)
{
	GnomeEggXferDialog *progress;

	progress = GNOME_EGG_XFER_DIALOG (object);

	if (progress->priv->delayed_close_timeout_id != 0) {
		g_source_remove (progress->priv->delayed_close_timeout_id);
		progress->priv->delayed_close_timeout_id = 0;
	}
	
	if (progress->priv->delayed_show_timeout_id != 0) {
		g_source_remove (progress->priv->delayed_show_timeout_id);
		progress->priv->delayed_show_timeout_id = 0;
	}

	if (progress->priv->time_remaining_timeout_id != 0) {
		g_source_remove (progress->priv->time_remaining_timeout_id);
		progress->priv->time_remaining_timeout_id = 0;
	}
	
	GTK_OBJECT_CLASS (gnome_egg_xfer_dialog_parent_class)->destroy (object);
}

/* Initialization.  */

static void
create_titled_label (GtkTable *table, int row, GtkWidget **title_widget, GtkWidget **label_text_widget)
{
	*title_widget = gtk_label_new ("");
	eel_gtk_label_make_bold (GTK_LABEL (*title_widget));
	gtk_misc_set_alignment (GTK_MISC (*title_widget), 1, 0);
	gtk_table_attach (table, *title_widget,
			  0, 1,
			  row, row + 1,
			  GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (*title_widget);

	*label_text_widget = gtk_label_new ("");
	gtk_label_set_ellipsize (GTK_LABEL (*label_text_widget), PANGO_ELLIPSIZE_START);
	gtk_table_attach (table, *label_text_widget,
			  1, 2,
			  row, row + 1,
			  GTK_FILL | GTK_EXPAND, 0,
			  0, 0);
	gtk_widget_show (*label_text_widget);
	gtk_misc_set_alignment (GTK_MISC (*label_text_widget), 0, 0);
}

static void
map_callback (GtkWidget *widget)
{
	GnomeEggXferDialog *progress;

	progress = GNOME_EGG_XFER_DIALOG (widget);

	GTK_WIDGET_CLASS (gnome_egg_xfer_dialog_parent_class)->map (widget);

	progress->priv->show_time = eel_get_system_time ();
}

static gboolean
delete_event_callback (GtkWidget *widget,
		       GdkEventAny *event)
{
	/* Do nothing -- we shouldn't be getting a close event because
	 * the dialog should not have a close box.
	 */
	return TRUE;
}

static void
gnome_egg_xfer_dialog_init (GnomeEggXferDialog *progress)
{
	GtkWidget *hbox, *vbox;
	GtkTable *titled_label_table;

	progress->priv = g_new0 (GnomeEggXferDialogPrivate, 1);

	vbox = gtk_vbox_new (FALSE, VERTICAL_SPACING);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), OUTER_BORDER);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (progress)->vbox), vbox, TRUE, TRUE, VERTICAL_SPACING);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, HORIZONTAL_SPACING);

	/* label- */
	/* Files remaining to be copied: */
	progress->priv->progress_title_label = gtk_label_new ("");
	gtk_label_set_justify (GTK_LABEL (progress->priv->progress_title_label), GTK_JUSTIFY_LEFT);
	gtk_box_pack_start (GTK_BOX (hbox), progress->priv->progress_title_label, FALSE, FALSE, 0);
	eel_gtk_label_make_bold (GTK_LABEL (progress->priv->progress_title_label));


	/* label -- */
	/* 24 of 30 */
	progress->priv->progress_count_label = gtk_label_new ("");
	gtk_label_set_justify (GTK_LABEL (progress->priv->progress_count_label), GTK_JUSTIFY_RIGHT);
	gtk_box_pack_end (GTK_BOX (hbox), progress->priv->progress_count_label, FALSE, FALSE, 0);
	eel_gtk_label_make_bold (GTK_LABEL (progress->priv->progress_count_label));

	/* progress bar */
	progress->priv->progress_bar = gtk_progress_bar_new ();
	gtk_window_set_default_size (GTK_WINDOW (progress), PROGRESS_DIALOG_WIDTH, -1);
	gtk_box_pack_start (GTK_BOX (vbox), progress->priv->progress_bar, FALSE, TRUE, 0);
	/* prevent a resizing of the bar when a real text is inserted later */
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress->priv->progress_bar), " ");

	titled_label_table = GTK_TABLE (gtk_table_new (3, 2, FALSE));
	gtk_table_set_row_spacings (titled_label_table, 4);
	gtk_table_set_col_spacings (titled_label_table, 4);

	create_titled_label (titled_label_table, 0,
			     &progress->priv->operation_name_label, 
			     &progress->priv->item_name);
	create_titled_label (titled_label_table, 1,
			     &progress->priv->from_label, 
			     &progress->priv->from_path_label);
	create_titled_label (titled_label_table, 2,
			     &progress->priv->to_label, 
			     &progress->priv->to_path_label);

	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (titled_label_table), FALSE, FALSE, 0);

	/* Set window icon */
	gtk_window_set_icon (GTK_WINDOW (progress), empty_jar_pixbuf);

	/* Set progress jar position */
	progress->priv->progress_jar_position = gdk_pixbuf_get_height (empty_jar_pixbuf);

	gtk_widget_show_all (vbox);
}

static void
gnome_egg_xfer_dialog_class_init (GnomeEggXferDialogClass *klass)
{
	GObjectClass *gobject_class;
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkDialogClass *dialog_class;

	gobject_class = G_OBJECT_CLASS (klass);
	object_class = GTK_OBJECT_CLASS (klass);
	widget_class = GTK_WIDGET_CLASS (klass);
	dialog_class = GTK_DIALOG_CLASS (klass);


	gobject_class->finalize = gnome_egg_xfer_dialog_finalize;
	
	object_class->destroy = gnome_egg_xfer_dialog_destroy;

	/* The progress dialog should not have a title and a close box.
	 * Some broken window manager themes still show the window title.
	 * Make clicking the close box do nothing in that case to prevent
	 * a crash.
	 */
	widget_class->delete_event = delete_event_callback;
	widget_class->map = map_callback;

	dialog_class->close = close_callback;

	/* Load the jar pixbufs */
	empty_jar_pixbuf = gdk_pixbuf_new_from_inline (-1, progress_jar_empty_icon, FALSE, NULL);
	full_jar_pixbuf = gdk_pixbuf_new_from_inline (-1, progress_jar_full_icon, FALSE, NULL);
	
}

static gboolean
time_remaining_callback (gpointer callback_data)
{
	int elapsed_time;
	int transfer_rate;
	int time_remaining;
	char *str;
	GnomeEggXferDialog *progress;
	
	progress = GNOME_EGG_XFER_DIALOG (callback_data);
	
	elapsed_time = (eel_get_system_time () - progress->priv->first_transfer_time) / 1000000;

	if (elapsed_time == 0) {
		progress->priv->time_remaining_timeout_id =
			g_timeout_add (TIME_REMAINING_TIMEOUT, time_remaining_callback, progress);
		
		return FALSE;
	}
	
	transfer_rate = progress->priv->bytes_copied / elapsed_time;

	if (transfer_rate == 0) {
		progress->priv->time_remaining_timeout_id =
			g_timeout_add (TIME_REMAINING_TIMEOUT, time_remaining_callback, progress);

		return FALSE;
	}

	time_remaining = (progress->priv->bytes_total -
			  progress->priv->bytes_copied) / transfer_rate;

	if (time_remaining >= 3600) {
		str = g_strdup_printf (_("(%d:%02d:%d Remaining)"), 
				       time_remaining / 3600, (time_remaining % 3600) / 60, (time_remaining % 3600) % 60);

	}
	else {
		str = g_strdup_printf (_("(%d:%02d Remaining)"), 
				       time_remaining / 60, time_remaining % 60);
	}
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress->priv->progress_bar), str);
	
	g_free (str);

	progress->priv->time_remaining_timeout_id =
		g_timeout_add (TIME_REMAINING_TIMEOUT, time_remaining_callback, progress);
	
	return FALSE;
}

static gboolean
delayed_show_callback (gpointer callback_data)
{
	GnomeEggXferDialog *progress;
	
	progress = GNOME_EGG_XFER_DIALOG (callback_data);
	
	progress->priv->delayed_show_timeout_id = 0;
	
	gtk_widget_show (GTK_WIDGET (progress));
	
	return FALSE;
}

GtkWidget *
gnome_egg_xfer_dialog_new (const char *title,
			   const char *operation_string,
			   const char *from_prefix,
			   const char *to_prefix,
			   gulong total_files,
			   GnomeVFSFileSize total_bytes,
			   gboolean use_timeout)
{
	GtkWidget *widget;
	GnomeEggXferDialog *progress;

	widget = gtk_widget_new (gnome_egg_xfer_dialog_get_type (), NULL);
	progress = GNOME_EGG_XFER_DIALOG (widget);

	gnome_egg_xfer_dialog_set_operation_string (progress, operation_string);
	gnome_egg_xfer_dialog_set_total (progress, total_files, total_bytes);

	gtk_window_set_title (GTK_WINDOW (widget), title);
	gtk_window_set_wmclass (GTK_WINDOW (widget), "file_progress", "Nautilus");

	gtk_dialog_add_button (GTK_DIALOG (widget), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	progress->priv->from_prefix = from_prefix;
	progress->priv->to_prefix = to_prefix;

	if (use_timeout) {
		progress->priv->start_time = eel_get_system_time ();	
		progress->priv->delayed_show_timeout_id =
			g_timeout_add (SHOW_TIMEOUT, delayed_show_callback, progress);
	}
	
	return widget;
}

void
gnome_egg_xfer_dialog_set_total (GnomeEggXferDialog *progress,
					     gulong files_total,
					     GnomeVFSFileSize bytes_total)
{
	g_return_if_fail (GNOME_IS_EGG_XFER_DIALOG (progress));

	progress->priv->files_total = files_total;
	progress->priv->bytes_total = bytes_total;

	gnome_egg_xfer_dialog_update (progress);
}

void
gnome_egg_xfer_dialog_set_operation_string (GnomeEggXferDialog *progress,
							const char *operation_string)
{
	g_return_if_fail (GNOME_IS_EGG_XFER_DIALOG (progress));

	gtk_label_set_text (GTK_LABEL (progress->priv->progress_title_label),
			    operation_string);
}

void
gnome_egg_xfer_dialog_new_file (GnomeEggXferDialog *progress,
					    const char *progress_verb,
					    const char *item_name,
					    const char *from_path,
					    const char *to_path,
					    const char *from_prefix,
					    const char *to_prefix,
					    gulong file_index,
					    GnomeVFSFileSize size)
{
	char *progress_count;

	g_return_if_fail (GNOME_IS_EGG_XFER_DIALOG (progress));

	progress->priv->from_prefix = from_prefix;
	progress->priv->to_prefix = to_prefix;

	if (progress->priv->bytes_total > 0) {
		/* we haven't set up the file count yet, do not update the progress
		 * count until we do
		 */
		gtk_label_set_text (GTK_LABEL (progress->priv->operation_name_label),
				    progress_verb);
		set_text_unescaped_trimmed (progress->priv->item_name, item_name);

		progress_count = g_strdup_printf (_("%ld of %ld"),
						  file_index, 
						  progress->priv->files_total);
		gtk_label_set_text (GTK_LABEL (progress->priv->progress_count_label), progress_count);
		g_free (progress_count);

		gtk_label_set_text (GTK_LABEL (progress->priv->from_label), from_prefix);
		set_text_unescaped_trimmed (progress->priv->from_path_label, from_path);
	
		if (progress->priv->to_prefix != NULL && progress->priv->to_path_label != NULL) {
			gtk_label_set_text (GTK_LABEL (progress->priv->to_label), to_prefix);
			set_text_unescaped_trimmed (progress->priv->to_path_label, to_path);
		}

		if (progress->priv->first_transfer_time == 0) {
			progress->priv->first_transfer_time = eel_get_system_time ();
		}
	}

	gnome_egg_xfer_dialog_update (progress);
}

void
gnome_egg_xfer_dialog_clear (GnomeEggXferDialog *progress)
{
	gtk_label_set_text (GTK_LABEL (progress->priv->from_label), "");
	gtk_label_set_text (GTK_LABEL (progress->priv->from_path_label), "");
	gtk_label_set_text (GTK_LABEL (progress->priv->to_label), "");
	gtk_label_set_text (GTK_LABEL (progress->priv->to_path_label), "");

	progress->priv->files_total = 0;
	progress->priv->bytes_total = 0;

	gnome_egg_xfer_dialog_update (progress);
}

void
gnome_egg_xfer_dialog_update_sizes (GnomeEggXferDialog *progress,
						GnomeVFSFileSize bytes_done_in_file,
						GnomeVFSFileSize bytes_done)
{
	g_return_if_fail (GNOME_IS_EGG_XFER_DIALOG (progress));

	progress->priv->bytes_copied = bytes_done;


	if (progress->priv->time_remaining_timeout_id == 0) {
		/* The first time we wait five times as long before
		 * starting to show the time remaining */
		progress->priv->time_remaining_timeout_id =
				g_timeout_add (TIME_REMAINING_TIMEOUT * 5, time_remaining_callback, progress);

	}
	
	gnome_egg_xfer_dialog_update (progress);
}

static gboolean
delayed_close_callback (gpointer callback_data)
{
	GnomeEggXferDialog *progress;

	progress = GNOME_EGG_XFER_DIALOG (callback_data);

	progress->priv->delayed_close_timeout_id = 0;
	gtk_object_destroy (GTK_OBJECT (progress));
	return FALSE;
}

void
gnome_egg_xfer_dialog_done (GnomeEggXferDialog *progress)
{
	guint time_up;

	if (!GTK_WIDGET_MAPPED (progress)) {
		gtk_object_destroy (GTK_OBJECT (progress));
		return;
	}
	g_assert (progress->priv->start_time != 0);

	/* compute time up in milliseconds */
	time_up = (eel_get_system_time () - progress->priv->show_time) / 1000;
	if (time_up >= MINIMUM_TIME_UP) {
		gtk_object_destroy (GTK_OBJECT (progress));
		return;
	}
	
	/* No cancel button once the operation is done. */
	gtk_dialog_set_response_sensitive (GTK_DIALOG (progress), GTK_RESPONSE_CANCEL, FALSE);

	progress->priv->delayed_close_timeout_id = g_timeout_add
		(MINIMUM_TIME_UP - time_up,
		 delayed_close_callback,
		 progress);
}

void
gnome_egg_xfer_dialog_pause_timeout (GnomeEggXferDialog *progress)
{
	guint time_up;

	if (progress->priv->delayed_show_timeout_id == 0) {
		progress->priv->remaining_time = 0;
		return;
	}
	
	time_up = (eel_get_system_time () - progress->priv->start_time) / 1000;
	
	if (time_up >= SHOW_TIMEOUT) {
		progress->priv->remaining_time = 0;
		return;
	}
	
	g_source_remove (progress->priv->delayed_show_timeout_id);
	progress->priv->delayed_show_timeout_id = 0;
	progress->priv->remaining_time = SHOW_TIMEOUT - time_up;
}

void
gnome_egg_xfer_dialog_resume_timeout (GnomeEggXferDialog *progress)
{
	if (progress->priv->delayed_show_timeout_id != 0) {
		return;
	}
	
	if (progress->priv->remaining_time <= 0) {
		return;
	}
	
	progress->priv->delayed_show_timeout_id =
		g_timeout_add (progress->priv->remaining_time,
			       delayed_show_callback,
			       progress);
			       
	progress->priv->start_time = eel_get_system_time () - 
			1000 * (SHOW_TIMEOUT - progress->priv->remaining_time);
					
	progress->priv->remaining_time = 0;
}
