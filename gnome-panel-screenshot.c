/* simple-screenshot.c */
/* Copyright (C) 2001 Jonathan Blandford <jrb@alum.mit.edu>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

/* FIXME: currently undone things */
#undef HAVE_PAPER_WIDTH

/* THERE ARE NO FEATURE REQUESTS ALLOWED */
/* IF YOU WANT YOUR OWN FEATURE -- WRITE THE DAMN THING YOURSELF (-:*/

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkx.h>
#include <png.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_GNOME_PRINT
#include <libgnomeprint/gnome-print.h>
#include <libgnomeprint/gnome-print-master.h>
#include <libgnomeprint/gnome-print-preview.h>
#include <libgnomeprint/gnome-print-dialog.h>
#include <libgnomeprint/gnome-print-master-preview.h>
#endif
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xmu/WinUtil.h>
#include <libart_lgpl/art_rgb_affine.h>

#ifdef HAVE_PAPER_WIDTH
#include <stdio.h>
#include <locale.h>
#include <langinfo.h>
#endif

/* How far down the window tree will we search when looking for top-level
 * windows? Some window managers doubly-reparent the client, so account
 * for that, and add some slop.
 */
#define MAXIMUM_WM_REPARENTING_DEPTH 4

static GladeXML *xml = NULL;
static GtkWidget *toplevel = NULL;
static GtkWidget *preview = NULL;
static GdkPixbuf *screenshot = NULL;
static GdkPixbuf *preview_image = NULL;
static char *web_dir;
static char *desktop_dir;
static char *home_dir;
static char *class_name = NULL;
static pid_t temporary_pid = 0;
static char *temporary_file = NULL;

static GtkTargetEntry drag_types[] =
	{ { "x-special/gnome-icon-list", 0, 0 },
	  { "text/uri-list", 0, 0 } };

/* some prototypes for the glade autoconnecting sutff */
void on_save_rbutton_toggled (GtkWidget *toggle, gpointer data);
void on_preview_expose_event (GtkWidget *drawing_area,
			      GdkEventExpose *event,
			      gpointer data);
void on_preview_configure_event (GtkWidget *drawing_area,
				 GdkEventConfigure *event,
				 gpointer data);
void on_ok_button_clicked (GtkWidget *widget, gpointer data);
void on_cancel_button_clicked (GtkWidget *widget, gpointer data);
int on_toplevel_key_press_event (GtkWidget *widget, GdkEventKey *key);

/* some local prototypes */
static gchar * add_file_to_path (const gchar *path);


/* helper functions */
/* This code is copied from gdk-pixbuf-HEAD.  It does no memory management and
 * is very hard-coded.  Please do not use it anywhere else. */
static gboolean
save_to_file_internal (FILE *fp, const char *file, char **error)
{
	png_structp png_ptr;
	png_infop info_ptr;
	guchar *ptr;
	guchar *pixels;
	int x, y, j;
	png_bytep row_ptr, data = NULL;
	png_color_8 sig_bit;
	int w, h, rowstride;
	int has_alpha;
	int bpc;

	*error = NULL;

	bpc = gdk_pixbuf_get_bits_per_sample (screenshot);
	w = gdk_pixbuf_get_width (screenshot);
	h = gdk_pixbuf_get_height (screenshot);
	rowstride = gdk_pixbuf_get_rowstride (screenshot);
	has_alpha = gdk_pixbuf_get_has_alpha (screenshot);
	pixels = gdk_pixbuf_get_pixels (screenshot);

	png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING,
					   NULL, NULL, NULL);

	if (png_ptr == NULL) {
		*error = _("Unable to initialize png structure.\n"
			   "You probably have a bad version of libpng "
			   "on your system");
		return FALSE;
	}

	info_ptr = png_create_info_struct (png_ptr);
	if (info_ptr == NULL) {
		*error = _("Unable to create png info.\n"
			   "You probably have a bad version of libpng "
			   "on your system");
		return FALSE;
	}

	if (setjmp (png_ptr->jmpbuf)) {
		*error = _("Unable to set png info.\n"
			   "You probably have a bad version of libpng "
			   "on your system");
		return FALSE;
	}

	png_init_io (png_ptr, fp);

	png_set_IHDR (png_ptr, info_ptr, w, h, bpc,
		      PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
		      PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
	data = malloc (w * 3 * sizeof (char));

	if (data == NULL) {
		*error = _("Insufficient memory to save the screenshot.\n"
			   "Please free up some resources and try again.");
		return FALSE;
	}

	sig_bit.red = bpc;
	sig_bit.green = bpc;
	sig_bit.blue = bpc;
	sig_bit.alpha = bpc;
	png_set_sBIT (png_ptr, info_ptr, &sig_bit);
	png_write_info (png_ptr, info_ptr);
	png_set_shift (png_ptr, &sig_bit);
	png_set_packing (png_ptr);

	ptr = pixels;
	for (y = 0; y < h; y++) {
		for (j = 0, x = 0; x < w; x++)
			memcpy (&(data[x*3]), &(ptr[x*3]), 3);

		row_ptr = (png_bytep)data;
		png_write_rows (png_ptr, &row_ptr, 1);
		ptr += rowstride;
	}

       if (data)
               free (data);

       png_write_end (png_ptr, info_ptr);
       png_destroy_write_struct (&png_ptr, (png_infopp) NULL);

       return TRUE;
}

/* nibble on the file a bit and return the file pointer
 * if it tastes good */
static FILE *
nibble_on_file (const char *file)
{
	char *error;
	GtkWidget *dialog;
	FILE *fp;

	if (access (file, F_OK) == 0) {
		error = g_strdup_printf
			(_("File %s already exists. Overwrite?"),
			 file);
		dialog = gnome_message_box_new
			(error, GNOME_MESSAGE_BOX_QUESTION,
			 GNOME_STOCK_BUTTON_YES, 
			 GNOME_STOCK_BUTTON_NO, NULL);
		gnome_dialog_set_parent (GNOME_DIALOG (dialog),
					 GTK_WINDOW (toplevel));
		g_free (error);
		if (gnome_dialog_run (GNOME_DIALOG (dialog)) != 0)
			return NULL;
	}
	fp = fopen (file, "w");
	if (fp == NULL) {
		error = g_strdup_printf (_("Unable to create the file:\n"
					   "\"%s\"\n"
					   "Please check your permissions of "
					   "the parent directory"), file);
		dialog = gnome_error_dialog_parented (error,
						      GTK_WINDOW (toplevel));
		gnome_dialog_run (GNOME_DIALOG (dialog));
		return NULL;
	}
	return fp;
}

static gboolean
save_to_file (FILE *fp, const gchar *file, gboolean gui_errors)
{
	GtkWidget *dialog;
	char *error;

	if (fp == NULL) {
		fp = nibble_on_file (file);
	}

	if ( ! save_to_file_internal (fp, file, &error)) {
		if (gui_errors) {
			dialog = gnome_error_dialog_parented
				(error, GTK_WINDOW (toplevel));
			gnome_dialog_run (GNOME_DIALOG (dialog));
		}
		fclose (fp);
		unlink (file);
		return FALSE;
	} else {
		fclose (fp);
		return TRUE;
	}
}

static void
start_temporary (void)
{
	char *dir;
	char *file = NULL;

	if (temporary_file != NULL) {
		if (access (temporary_file, F_OK) == 0)
			return;

		/* Note: nautilus is a wanker and will happily do a move when
		 * we explicitly told him that we just support copy, so in case
		 * this file is missing, we let nautilus have it and hope
		 * he chokes on it */

		dir = g_dirname (temporary_file);
		rmdir (dir);
		g_free (dir);

		g_free (temporary_file = NULL);
		temporary_file = NULL;

		/* just paranoia */
		if (temporary_pid > 0)
			kill (temporary_pid, SIGKILL);
	}

	/* make a temporary dirname */
	dir = NULL;
	do {
		if (dir != NULL) free (dir);
		dir = tempnam (NULL, "scr");
	} while (mkdir (dir, 0755) < 0);

	file = add_file_to_path (dir);

	free (dir);

	temporary_pid = fork ();

	if (temporary_pid == 0) {
		FILE *fp = fopen (file, "w");
		if (fp == NULL ||
		    ! save_to_file (fp, file, FALSE)) {
			_exit (1);
		} else {
			_exit (0);
		}
	}

	/* can't fork? don't dispair, do synchroniously */
	if (temporary_pid < 0) {
		FILE *fp = fopen (file, "w");
		if (fp == NULL ||
		    ! save_to_file (fp, file, TRUE)) {
			g_free (file);
			temporary_pid = 0;
			return;
		}
		temporary_pid = 0;
	}

	temporary_file = file;
}

static gboolean
ensure_temporary (void)
{
	int status;

	start_temporary ();

	if (temporary_file == NULL)
		return FALSE;

	if (temporary_pid == 0)
		return TRUE;

	/* ok, gotta wait */
	waitpid (temporary_pid, &status, 0);

	temporary_pid = 0;

	if (WIFEXITED (status) &&
	    WEXITSTATUS (status) == 0) {
		return TRUE;
	} else {
		g_free (temporary_file);
		temporary_file = NULL;
		temporary_pid = 0;
		return FALSE;
	}
}

static void
cleanup_temporary (void)
{
	char *file = temporary_file;
	pid_t pid = temporary_pid;

	temporary_file = NULL;
	temporary_pid = 0;

	if (pid > 0) {
		if (kill (pid, SIGTERM) == 0)
			waitpid (pid, NULL, 0);
	}
	
	if (file != NULL) {
		char *dir;

		unlink (file);

		dir = g_dirname (file);
		rmdir (dir);
		g_free (dir);
	}

	g_free (file);
}

#ifdef HAVE_GNOME_PRINT
static GdkPixbuf *
rotate_image (GdkPixbuf *image)
{
	GdkPixbuf *retval;
	double affine[6];
	gint width, height;

	width = gdk_pixbuf_get_width (image);
	height = gdk_pixbuf_get_height (image);

	retval = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, height, width);
	affine[0] = 0.0;    /* = cos(90) */
	affine[2] = 1.0;    /* = sin(90) */
	affine[3] = 0.0;    /* = cos(90) */
	affine[1] = -1.0;   /* = -sin(90) */
	affine[4] = 0;      /* x translation */
	affine[5] = width;  /* y translation */

	art_rgb_affine (gdk_pixbuf_get_pixels (retval),
			0, 0,
			height, width,
			gdk_pixbuf_get_rowstride (retval),
			gdk_pixbuf_get_pixels (image),
			width, height,
			gdk_pixbuf_get_rowstride (image),
			affine,
			ART_FILTER_NEAREST,
			NULL);

	return retval;
}
#endif

#ifdef HAVE_GNOME_PRINT
static void
print_page (GnomePrintContext *context, const GnomePaper *paper)
{
	GdkPixbuf *printed_image;
	gint real_width = gnome_paper_pswidth (paper);
	gint real_height = gnome_paper_psheight (paper);
	gint pix_width;
	gint pix_height;
	gint width, height;

	/* always make sure that it is taller then wide, under the potentially
	 * mistaken assumption that all paper is this way too. */
	if (gdk_pixbuf_get_width (screenshot) > gdk_pixbuf_get_height (screenshot)) {
		printed_image = rotate_image (screenshot);
	} else {
		gdk_pixbuf_ref (screenshot);
		printed_image = screenshot;
	}

	pix_width = gdk_pixbuf_get_width (printed_image);
	pix_height = gdk_pixbuf_get_height (printed_image);

	width = real_width - 2 * gnome_paper_tmargin (paper);
	height = real_height - 2 * gnome_paper_rmargin (paper);

	if (((gdouble) pix_height/pix_width) >
	    ((gdouble)width/height)) {
		/* We scale to the top */
		width = height * (gdouble)pix_width/pix_height;
	} else {
		/* We scale to the sides of the page */
		height = width * (gdouble)pix_height/pix_width;
	}

	gnome_print_beginpage (context, "1");

	gnome_print_gsave (context);
	gnome_print_translate (context, (real_width-width)/2.0, (real_height - height)/2.0);
	gnome_print_scale (context, width, height);
	gnome_print_pixbuf (context, printed_image);
	gnome_print_grestore (context);
  
	gnome_print_showpage (context);
	gnome_print_context_close (context);
	gdk_pixbuf_unref (printed_image);
}
#endif

#ifdef HAVE_GNOME_PRINT
static gboolean
print_pixbuf (void)
{
	GnomePrintDialog *print_dialog;
	GnomePrintContext *context;
	GnomePrintMaster *gpm = NULL;
	GnomePrintMasterPreview *gpmp;
	const GnomePaper *paper;
	gint do_preview = FALSE;
	gint copies, collate;
	gint result;
	GdkCursor *cursor;
#ifdef HAVE_PAPER_WIDTH
	GnomeUnit *unit;
	guint width, height;
#endif

	print_dialog = GNOME_PRINT_DIALOG (gnome_print_dialog_new (_("Print Screenshot"), GNOME_PRINT_DIALOG_COPIES));
	gnome_dialog_set_parent (GNOME_DIALOG (print_dialog), GTK_WINDOW (toplevel));
	do {
		result = gnome_dialog_run (GNOME_DIALOG (print_dialog)); 
		switch (result) {
		case GNOME_PRINT_CANCEL:
			gnome_dialog_close (GNOME_DIALOG (print_dialog));
		case -1:
			return FALSE;
		case GNOME_PRINT_PREVIEW:
			do_preview = TRUE;
			break;
		default:
			do_preview = FALSE;
			break;
		}

		/* set up the gnome_print_master */
		gpm = gnome_print_master_new ();
		gnome_print_dialog_get_copies (print_dialog, &copies, &collate);
		gnome_print_master_set_copies (gpm, copies, collate);
		gnome_print_master_set_printer (gpm, gnome_print_dialog_get_printer (print_dialog));

#ifdef HAVE_PAPER_WIDTH
		unit = gnome_unit_with_name ("Millimeter");

		width = (unsigned int)(size_t)nl_langinfo (_NL_PAPER_WIDTH);
		height = (unsigned int)(size_t)nl_langinfo (_NL_PAPER_HEIGHT);

		g_print ("%f %f\n", gnome_paper_convert_to_points (width, unit),
			 gnome_paper_convert_to_points (height, unit));
		paper = gnome_paper_with_size (gnome_paper_convert_to_points (width, unit),
					       gnome_paper_convert_to_points (height, unit));
#else
		paper = gnome_paper_with_name (gnome_paper_name_default ());
#endif

		gnome_print_master_set_paper (gpm, paper);
		context = gnome_print_master_get_context (gpm);

		print_page (context, paper);

		if (do_preview == FALSE) {
			gnome_dialog_close (GNOME_DIALOG (print_dialog));
			gnome_print_master_print (gpm);
			gnome_print_master_close (gpm);
			return TRUE;
		}
		gpmp = gnome_print_master_preview_new (gpm, _("Screenshot Print Preview"));
		gtk_signal_connect (GTK_OBJECT (gpmp), "destroy", gtk_main_quit, NULL);
		gtk_widget_set_sensitive (GTK_WIDGET (print_dialog), FALSE);
		cursor = gdk_cursor_new (GDK_WATCH);
		gdk_window_set_cursor (GTK_WIDGET (print_dialog)->window, cursor);
		gdk_cursor_destroy (cursor);

		gtk_widget_show (GTK_WIDGET (gpmp));
		gtk_window_set_modal (GTK_WINDOW (gpmp), TRUE);
		gtk_window_set_transient_for (GTK_WINDOW (gpmp), GTK_WINDOW (print_dialog));
		gtk_main ();
		gtk_widget_set_sensitive (GTK_WIDGET (print_dialog), TRUE);
		gdk_window_set_cursor (GTK_WIDGET (print_dialog)->window, NULL);
	} while (TRUE);
}
#endif

static gchar *
add_file_to_path (const gchar *path)
{
	gchar *retval;
	gint i = 1;

	if (class_name) {
		/* translators: this is the file that gets made up with the screenshot if a specific window is taken */
		retval = g_strdup_printf (_("%s%cScreenshot-%s.png"), path,
					  G_DIR_SEPARATOR, class_name);
	}
	else {
		/* translators: this is the file that gets made up with the screenshot if the entire screen is taken */
		retval = g_strdup_printf (_("%s%cScreenshot.png"), path,
					  G_DIR_SEPARATOR);
	}
	
	do {
		struct stat s;

		if (stat (retval, &s) &&
		    errno == ENOENT)
			return retval;

		g_free (retval);

		if (class_name) {
			/* translators: this is the file that gets made up with the screenshot if a specific window is taken */
			retval = g_strdup_printf (_("%s%cScreenshot-%s-%d.png"), path,
						  G_DIR_SEPARATOR, class_name, i);
		}
		else {
			/* translators: this is the file that gets made up with the screenshot if the entire screen is taken */
			retval = g_strdup_printf (_("%s%cScreenshot-%d.png"), path,
						  G_DIR_SEPARATOR, i);
		}
		
		i++;
	} while (TRUE);
}

/* Callbacks */
void
on_save_rbutton_toggled (GtkWidget *toggle, gpointer data)
{
	GtkWidget *save_fileentry = glade_xml_get_widget (xml, "save_fileentry");

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle)))
		gtk_widget_set_sensitive (save_fileentry, TRUE);
	else
		gtk_widget_set_sensitive (save_fileentry, FALSE);
}

void
on_preview_expose_event (GtkWidget      *drawing_area,
			 GdkEventExpose *event,
			 gpointer        data)
{
	gdk_draw_rgb_image (drawing_area->window,
			    drawing_area->style->white_gc,
			    event->area.x, event->area.y,
			    event->area.width,
			    event->area.height,
			    GDK_RGB_DITHER_NORMAL,
			    gdk_pixbuf_get_pixels (preview_image)
			    + (event->area.y * gdk_pixbuf_get_rowstride (preview_image))
			    + (event->area.x * gdk_pixbuf_get_n_channels (preview_image)),
			    gdk_pixbuf_get_rowstride (preview_image));
}

void
on_preview_configure_event (GtkWidget         *drawing_area,
			    GdkEventConfigure *event,
			    gpointer           data)
{
	if (preview_image)
		gdk_pixbuf_unref (preview_image);

	preview_image = gdk_pixbuf_scale_simple (screenshot,
						 event->width,
						 event->height,
						 GDK_INTERP_BILINEAR);
}

static void
setup_busy (gboolean busy)
{
	GdkCursor *cursor;

	if (busy) {
		/* Change cursor to busy */
		cursor = gdk_cursor_new (GDK_WATCH);
		gdk_window_set_cursor (toplevel->window, cursor);
		gdk_cursor_destroy (cursor);
	} else {
		gdk_window_set_cursor (toplevel->window, NULL);
	}

	/* block expose on the, since we don't want to redraw the preview
	 * in the draw. It'd make no sense and would just generate X traffic */
	gtk_signal_handler_block_by_func
		(GTK_OBJECT (preview),
		 GTK_SIGNAL_FUNC (on_preview_expose_event),
		 NULL);

	gtk_widget_set_sensitive (toplevel, ! busy);
	gtk_widget_draw (toplevel, NULL);

	gtk_signal_handler_unblock_by_func
		(GTK_OBJECT (preview),
		 GTK_SIGNAL_FUNC (on_preview_expose_event),
		 NULL);

	gdk_flush ();

}

static gboolean
gimme_file (const char *filename)
{
	FILE *fp;

	fp = nibble_on_file (filename);
	if (fp == NULL)
		return FALSE;

	/* if there is a temporary in the works
	 * gimme it */
	if (temporary_file != NULL)
		ensure_temporary ();

	/* if we actually got a temporary, move or copy it */
	if (temporary_file != NULL) {
		char buf[4096];
		int bytes;
		int infd, outfd;

		/* we'll we're gonna reopen this sucker */
		fclose (fp);

		if (rename (temporary_file, filename) == 0) {
			g_free (temporary_file);
			temporary_file = NULL;
			return TRUE;
		}
		infd = open (temporary_file, O_RDONLY);
		if (infd < 0) {
			/* Eeeeek! this can never happen, but we're paranoid */
			return FALSE;
		}

		outfd = open (filename, O_CREAT|O_TRUNC|O_WRONLY, 0644);
		if (outfd < 0) {
			char *error;
			GtkWidget *dialog;
			error = g_strdup_printf
				(_("Unable to create the file:\n"
				   "\"%s\"\n"
				   "Please check your permissions of "
				   "the parent directory"), filename);
			dialog = gnome_error_dialog_parented
				(error, GTK_WINDOW (toplevel));
			g_free (error);
			close (infd);
			return FALSE;
		}

		while ((bytes = read (infd, buf, sizeof (buf))) > 0) {
			if (write (outfd, buf, bytes) != bytes) {
				char *error;
				GtkWidget *dialog;
				close (infd);
				close (outfd);
				unlink (filename);
				error = g_strdup_printf
					(_("Not enough room to write file %s"),
					 filename);
				dialog = gnome_error_dialog_parented
					(error, GTK_WINDOW (toplevel));
				g_free (error);
				gnome_dialog_run (GNOME_DIALOG (dialog));
				return FALSE;
			}
		}

		close (infd);
		close (outfd);

		return TRUE;
	} else {
		return save_to_file (fp, filename, TRUE);
	}
}

void
on_ok_button_clicked (GtkWidget *widget,
		      gpointer   data)
{
	GtkWidget *button;
	gchar *file;

	setup_busy (TRUE);

	button = glade_xml_get_widget (xml, "save_rbutton");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		GtkWidget *entry;
		entry = glade_xml_get_widget (xml, "save_entry");
		if (gimme_file (gtk_entry_get_text (GTK_ENTRY (entry)))) {
			gtk_main_quit ();
		}
		setup_busy (FALSE);
		return;
	}

	button = glade_xml_get_widget (xml, "desktop_rbutton");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		file = add_file_to_path (desktop_dir);
		if (gimme_file (file)) {
			gtk_main_quit ();
		}
		g_free (file);
		setup_busy (FALSE);
		return;
	}
#ifdef HAVE_GNOME_PRINT
	button = glade_xml_get_widget (xml, "print_rbutton");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		if (print_pixbuf ()) {
			gtk_main_quit ();
		}
		setup_busy (FALSE);
		return;
	}
#endif

	file = add_file_to_path (web_dir);
	if ( ! gimme_file (file)) {
		g_free (file);
		setup_busy (FALSE);
		return;
	}

	g_free (file);
	gtk_main_quit ();

	setup_busy (FALSE);
}

void
on_cancel_button_clicked (GtkWidget *widget, gpointer data)
{
	gtk_main_quit ();
}

int
on_toplevel_key_press_event (GtkWidget *widget,
			     GdkEventKey *key)
{
	if (key->keyval != GDK_Escape)
		return FALSE;

	gtk_main_quit ();
	return TRUE;
}

/* This function is partly stolen from eel, it was written by John Harper */
static Window
find_toplevel_window (int depth, Window xid, gboolean *keep_going)
{
	static Atom wm_state = 0;

	Atom actual_type;
	int actual_format;
	gulong nitems, bytes_after;
	gulong *prop;

	Window root, parent, *children, window;
	int nchildren, i;

	if (wm_state == 0) {
		wm_state = XInternAtom (GDK_DISPLAY (), "WM_STATE", False);
	}

	/* Check if the window is a top-level client window.
	 * Windows will have a WM_STATE property iff they're top-level.
	 */
	if (XGetWindowProperty (GDK_DISPLAY (), xid, wm_state, 0, 1,
				False, AnyPropertyType, &actual_type,
				&actual_format, &nitems, &bytes_after,
				(guchar **) &prop) == Success
	    && prop != NULL && actual_format == 32 && prop[0] == NormalState)
	{
		/* Found a top-level window */

		if (prop != NULL) {
			XFree (prop);
		}

		*keep_going = FALSE;

		return xid;
	}

	/* Not found a top-level window yet, so keep recursing. */
	if (depth < MAXIMUM_WM_REPARENTING_DEPTH) {
		if (XQueryTree (GDK_DISPLAY (), xid, &root,
				&parent, &children, &nchildren) != 0)
		{
			window = 0;

			for (i = 0; *keep_going && i < nchildren; i++) {
				window = find_toplevel_window (depth + 1,
							       children[i],
							       keep_going);
			}

			if (children != NULL) {
				XFree (children);
			}

			if (! *keep_going) {
				return window;
			}
		}
	}

	return 0;
}

static void
take_window_shot (void)
{
	GdkWindow *window;
	Display *disp;
	Window w, root, child, toplevel;
	int unused;
	guint mask;
	gint x_orig, y_orig;
	gint x = 0, y = 0;
	gint width, height;
	XClassHint class_hint;
	gchar *result = NULL;
	gboolean keep_going;
	
	disp = GDK_DISPLAY ();
	w = GDK_ROOT_WINDOW ();

	XQueryPointer (disp, w, &root, &child,
		       &unused,
		       &unused,
		       &unused,
		       &unused,
		       &mask);

	if (child == None)
                window = GDK_ROOT_PARENT ();
	else {

                window = gdk_window_foreign_new (child);

		keep_going = TRUE;

		toplevel = find_toplevel_window (0, child, &keep_going);

		/* Get the Class Hint */
		if (toplevel && (XGetClassHint (GDK_DISPLAY (), toplevel, &class_hint) != 0)) {
			if (class_hint.res_class)
				result = class_hint.res_class;

			XFree (class_hint.res_name);
		}
	}

	gdk_window_get_size (window, &width, &height);
	gdk_window_get_origin (window, &x_orig, &y_orig);

	
	if (x_orig < 0) {
		x = - x_orig;
		width = width + x_orig;
		x_orig = 0;
	}
	if (y_orig < 0) {
		y = - y_orig;
		height = height + y_orig;
		y_orig = 0;
	}

	if (x_orig + width > gdk_screen_width ())
		width = gdk_screen_width () - x_orig;
	if (y_orig + height > gdk_screen_height ())
		height = gdk_screen_height () - y_orig;
			
	screenshot = gdk_pixbuf_get_from_drawable (NULL, window,
						   NULL,
						   x, y, 0, 0,
						   width, height);

	class_name = result;
}

static void
take_screen_shot (void)
{
	gint width, height;

	width = gdk_screen_width ();
	height = gdk_screen_height ();

	screenshot = gdk_pixbuf_get_from_drawable (NULL, GDK_ROOT_PARENT (),
						   NULL, 0, 0, 0, 0,
						   width, height);
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

	if ( ! ensure_temporary ()) {
		/*FIXME: cancel the drag*/
		return;
	}

	string = g_strdup_printf ("file:%s\r\n", temporary_file);
	gtk_selection_data_set (selection_data,
				selection_data->target,
				8, string, strlen (string)+1);
	g_free (string);
}

static void
got_signal (int sig)
{
	cleanup_temporary ();
	
	/* whack thyself */
	signal (sig, SIG_DFL);
	kill (getpid (), sig);
}

static void
drag_begin (GtkWidget *widget, GdkDragContext *context)
{
	static GdkPixmap *pixmap;
	GdkBitmap *mask;

	gdk_pixbuf_render_pixmap_and_mask
		(preview_image, &pixmap, &mask,
		 128);
	
	gtk_drag_set_icon_pixmap
		(context, gdk_rgb_get_cmap (), pixmap, mask, 0, 0);
	
	start_temporary ();
}


/* main */
int
main (int argc, char *argv[])
{
	GtkWidget *save_entry;
	GtkWidget *frame;
	GnomeClient *client;
	struct stat s;
	gchar *file;
	gboolean window = FALSE;
	gint width, height;
	struct poptOption opts[] = {
		{"window", '\0', POPT_ARG_NONE, &window, 0, N_("Grab a window instead of the entire screen"), NULL},
		{NULL, '\0', 0, NULL, 0, NULL, NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnome_init_with_popt_table ("gnome-panel-screenshot", VERSION,
				    argc, argv, opts, 0, NULL);
	glade_gnome_init();
	client = gnome_master_client ();
	gnome_client_set_restart_style (client, GNOME_RESTART_NEVER);

	if (window)
		take_window_shot ();
	else
		take_screen_shot ();

	if (g_file_exists ("gnome-panel-screenshot.glade")) {
		xml = glade_xml_new ("gnome-panel-screenshot.glade", NULL);
	}
	if (xml == NULL) {
		xml = glade_xml_new (GLADEDIR "/gnome-panel-screenshot.glade",
				     NULL);
	}
	if (xml == NULL) {
		GtkWidget *dialog = gnome_error_dialog
			(_("Glade file for the screenshot program is missing.\n"
			   "Please check your installation of gnome-core"));
		gnome_dialog_run (GNOME_DIALOG (dialog));
		exit (1);
	}
	glade_xml_signal_autoconnect (xml);

#ifndef HAVE_GNOME_PRINT
	{
		GtkWidget *button = glade_xml_get_widget (xml, "print_rbutton");
		if (button != NULL)
			gtk_widget_hide (button);
	}
#endif

	if (screenshot == NULL) {
		GtkWidget *dialog = gnome_error_dialog
			(_("Unable to take a screenshot of "
			   "the current desktop."));
		gnome_dialog_run (GNOME_DIALOG (dialog));
		exit (1);
	}

	width = gdk_pixbuf_get_width (screenshot);
	height = gdk_pixbuf_get_height (screenshot);

	width /= 5;
	height /= 5;

	toplevel = glade_xml_get_widget (xml, "toplevel");
	frame = glade_xml_get_widget (xml, "aspect_frame");
	preview = glade_xml_get_widget (xml, "preview");
	save_entry = glade_xml_get_widget (xml, "save_entry");

	gtk_window_set_default_size (GTK_WINDOW (toplevel), width * 2, -1);
	gtk_widget_set_usize (preview, width, height);
	gtk_aspect_frame_set (GTK_ASPECT_FRAME (frame), 0.5, 0.5,
			      gdk_pixbuf_get_width (screenshot)/
			      (gfloat) gdk_pixbuf_get_height (screenshot),
			      FALSE);
	if (window)
		gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	home_dir = g_get_home_dir ();
	web_dir = g_strconcat (home_dir, G_DIR_SEPARATOR_S,
			       "public_html", NULL);
	desktop_dir = g_strconcat (home_dir, G_DIR_SEPARATOR_S,
				   ".gnome-desktop", NULL);
	file = add_file_to_path (home_dir);
	gtk_entry_set_text (GTK_ENTRY (save_entry), file);
	g_free (file);

	if (! stat (web_dir, &s) &&
	    S_ISDIR (s.st_mode)) {
		GtkWidget *cbutton;
		cbutton = glade_xml_get_widget (xml, "web_rbutton");
		gtk_widget_show (cbutton);
	}

	/* setup dnd */
	/* just in case some wanker like nautilus took our image */
	gtk_signal_connect (GTK_OBJECT (preview), "drag_begin",
			    GTK_SIGNAL_FUNC (drag_begin), NULL);
	gtk_signal_connect (GTK_OBJECT (preview), "drag_data_get",
			    GTK_SIGNAL_FUNC (drag_data_get), NULL);
	gtk_drag_source_set (preview,
			     GDK_BUTTON1_MASK|GDK_BUTTON3_MASK,
			     drag_types, 2,
			     GDK_ACTION_COPY);

	gtk_widget_grab_focus (save_entry);
	gtk_editable_select_region (GTK_EDITABLE (save_entry), 0, -1);
	gtk_signal_connect (GTK_OBJECT (save_entry), "activate",
			    GTK_SIGNAL_FUNC (on_ok_button_clicked),
			    NULL);

	gtk_widget_show_now (toplevel);

	/*
	 * Start working on the temporary file in a fork, now this is
	 * a little evil since we might save a file the user might cancel
	 * and we'll jsut end up deleting it and/or killing the forked
	 * process.  But it makes it snappy and makes dnd not hang.  Go
	 * figure.
	 */
	start_temporary ();

	signal (SIGINT, got_signal);
	signal (SIGTERM, got_signal);

	gtk_main ();

	cleanup_temporary ();

	return 0;
}
