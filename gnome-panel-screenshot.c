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

/* THERE ARE NO FEATURE REQUESTS ALLOWED */
/* IF YOU WANT YOUR OWN FEATURE -- WRITE THE DAMN THING YOURSELF (-: */
/* MAYBE I LIED... -jrb */

/* PLAN:
grab lock()
find Window
take screenshot
remove lock
(add drop shadow)
get temporary filename/directory
fork()
 - save to png in /tmp
(show dialog to get filename)
transfer file
(popup transfer dialog)
*/

#include <config.h>
#include <gnome.h>
#include <gconf/gconf-client.h>
#include <glade/glade.h>
#include <gdk/gdkx.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <locale.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xmu/WinUtil.h>

#include "screenshot-shadow.h"
#include "screenshot-utils.h"
#include "screenshot-save.h"

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
static const char *home_dir;
static char *window_title = NULL;
static char *temporary_file = NULL;
static gboolean drop_shadow = TRUE;

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
void on_help_button_clicked (GtkWidget *widget, gpointer data);
int on_save_entry_key_press_event (GtkWidget *widget, GdkEventKey *key);
int on_toplevel_key_press_event (GtkWidget *widget, GdkEventKey *key);

/* some local prototypes */
static gchar * add_file_to_path (const gchar *path);
static void    display_help (void);
static void    save_done_notification (void);

/* nibble on the file a bit and return the file pointer
 * if it tastes good */
static FILE *
nibble_on_file (const char *file)
{
	GtkWidget *dialog;
	FILE *fp;
	mode_t old_mask;

	if (file == NULL)
		return NULL;

	if (access (file, F_OK) == 0) {
		int response;
		char *utf8_name = g_filename_to_utf8 (file, -1, NULL, NULL, NULL);

		dialog = gtk_message_dialog_new
			(GTK_WINDOW (toplevel),
			 0 /* flags */,
			 GTK_MESSAGE_QUESTION,
			 GTK_BUTTONS_YES_NO,
			 _("File %s already exists. Overwrite?"),
			 utf8_name);
		g_free (utf8_name);

		response = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		if (response != GTK_RESPONSE_YES)
			return NULL;
	}

	old_mask = umask(022);

	fp = fopen (file, "w");
	if (fp == NULL) {
		char *utf8_name = g_filename_to_utf8 (file, -1, NULL, NULL, NULL);
		dialog = gtk_message_dialog_new
			(GTK_WINDOW (toplevel),
			 0 /* flags */,
			 GTK_MESSAGE_ERROR,
			 GTK_BUTTONS_OK,
			 _("Unable to create the file:\n"
			   "\"%s\"\n"
			   "Please check your permissions of "
			   "the parent directory"), utf8_name);
		g_free (utf8_name);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		umask(old_mask);
		return NULL;
	}
	umask(old_mask);
	return fp;
}

static gchar *
add_file_to_path (const gchar *path)
{
	char *expanded_path;
	char *retval;
	char *tmp;
	char *file_name;
	int   i = 1;

	expanded_path = gnome_vfs_expand_initial_tilde (path);
	g_strstrip (expanded_path);

	if (window_title) {
		/* translators: this is the name of the file that gets made up
		 * with the screenshot if a specific window is taken */
		file_name = g_strdup_printf (_("Screenshot-%s.png"), window_title);
	} else {
		/* translators: this is the name of the file that gets made up
		 * with the screenshot if the entire screen is taken */
		file_name = g_strdup (_("Screenshot.png"));
	}
	tmp = g_filename_from_utf8 (file_name, -1, NULL, NULL, NULL);
	retval = g_build_filename (expanded_path, tmp, NULL);
	g_free (file_name);
	g_free (tmp);
	
	do {
		struct stat s;

		if (stat (retval, &s) && errno == ENOENT) {
			g_free (expanded_path);
			return retval;
		}
		g_print ("%s, errno: %s\n", retval, g_strerror (errno));
		
		g_free (retval);

		if (window_title) {
			/* translators: this is the name of the file that gets
			 * made up with the screenshot if a specific window is
			 * taken */
			file_name = g_strdup_printf (_("Screenshot-%s-%d.png"),
						     window_title, i);
		}
		else {
			/* translators: this is the name of the file that gets
			 * made up with the screenshot if the entire screen is
			 * taken */
			file_name = g_strdup_printf (_("Screenshot-%d.png"), i);
		}

		tmp = g_filename_from_utf8 (file_name, -1, NULL, NULL, NULL);
		retval = g_build_filename (expanded_path, tmp, NULL);
		g_free (file_name);
		g_free (tmp);
		
		i++;
	} while (TRUE);
}

static void
display_help (void)
{
	GError *error = NULL;

        gnome_help_display_desktop (NULL, "user-guide", 
				    "user-guide.xml", "goseditmainmenu-53", 
				    &error);
	
	if (error) {
		GtkWidget *dialog;

                dialog = gtk_message_dialog_new (GTK_WINDOW (toplevel),
                        GTK_DIALOG_DESTROY_WITH_PARENT,
                        GTK_MESSAGE_ERROR,
                        GTK_BUTTONS_OK,
                        _("There was an error displaying help: \n%s"),
                        error->message);

                g_signal_connect (G_OBJECT (dialog),
                        "response",
                        G_CALLBACK (gtk_widget_destroy), NULL);
                gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
                gtk_widget_show (dialog);
                g_error_free (error);

        }
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
  gdk_draw_pixbuf (drawing_area->window,
		   drawing_area->style->white_gc,
		   preview_image,
		   event->area.x,
		   event->area.y,
		   event->area.x,
		   event->area.y,
		   event->area.width,
		   event->area.height,
		   GDK_RGB_DITHER_NORMAL,
		   0, 0);
}

void
on_preview_configure_event (GtkWidget         *drawing_area,
			    GdkEventConfigure *event,
			    gpointer           data)
{
	if (preview_image)
		g_object_unref (G_OBJECT (preview_image));

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
		gdk_cursor_unref (cursor);
	} else {
		gdk_window_set_cursor (toplevel->window, NULL);
	}

	/* block expose on the, since we don't want to redraw the preview
	 * in the draw. It'd make no sense and would just generate X traffic */
	g_signal_handlers_block_by_func
		(G_OBJECT (preview),
		 G_CALLBACK (on_preview_expose_event),
		 NULL);

	gtk_widget_set_sensitive (toplevel, ! busy);
	gtk_widget_queue_draw (toplevel);

	g_signal_handlers_unblock_by_func
		(G_OBJECT (preview),
		 G_CALLBACK (on_preview_expose_event),
		 NULL);

	gdk_flush ();

}

static gboolean
gimme_file (char *filename)
{
	FILE *fp;

	g_strstrip (filename);
	fp = nibble_on_file (filename);
	if (fp == NULL) {
		return FALSE;
	}

	/* if there is a temporary in the works
	 * gimme it */
	if (temporary_file != NULL)
		;//ensure_temporary ();FIXME

	/* if we actually got a temporary, move or copy it */
	if (temporary_file != NULL) {
		char buf[4096];
		int bytes;
		int infd, outfd;

		/* we'll we're gonna reopen this sucker */
		fclose (fp);

		if (rename (temporary_file, filename) == 0) {
			chmod (filename, 0644);
			return TRUE;
		}
		infd = open (temporary_file, O_RDONLY);
		if (infd < 0) {
			/* Eeeeek! this can never happen, but we're paranoid */
			return FALSE;
		}

		outfd = open (filename, O_CREAT|O_TRUNC|O_WRONLY, 0644);
		if (outfd < 0) {
			GtkWidget *dialog;
			char *utf8_name = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
			dialog = gtk_message_dialog_new
				(GTK_WINDOW (toplevel),
				 0 /* flags */,
				 GTK_MESSAGE_ERROR,
				 GTK_BUTTONS_OK,
				 _("Unable to create the file:\n"
				   "\"%s\"\n"
				   "Please check your permissions of "
				   "the parent directory"), utf8_name);
			g_free (utf8_name);
			gtk_dialog_run (GTK_DIALOG (dialog));
			gtk_widget_destroy (dialog);
			close (infd);
			return FALSE;
		}

		while ((bytes = read (infd, buf, sizeof (buf))) > 0) {
			if (write (outfd, buf, bytes) != bytes) {
				GtkWidget *dialog;
				char *utf8_name = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
				close (infd);
				close (outfd);
				unlink (filename);
				dialog = gtk_message_dialog_new
					(GTK_WINDOW (toplevel),
					 0 /* flags */,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 _("Not enough room to write file %s"),
					 utf8_name);
				g_free (utf8_name);
				gtk_dialog_run (GTK_DIALOG (dialog));
				gtk_widget_destroy (dialog);
				return FALSE;
			}
		}

		close (infd);
		close (outfd);

		return TRUE;
	} else {
		//FIXME: return save_to_file (fp, filename, TRUE);
	}
	return FALSE;
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
		GtkWidget  *entry;
		GtkWidget  *fileentry;
		const char *tmp;
		char       *expanded_filename;

		entry = glade_xml_get_widget (xml, "save_entry");
		fileentry = glade_xml_get_widget (xml, "save_fileentry");
 		tmp = gtk_entry_get_text (GTK_ENTRY (entry));
 		file = g_filename_from_utf8 (tmp, -1, NULL, NULL, NULL);
		g_strstrip (file);
		expanded_filename = gnome_vfs_expand_initial_tilde (file);
		g_free (file);

 		if (gimme_file (expanded_filename)) {
			gnome_entry_prepend_history (GNOME_ENTRY (gnome_file_entry_gnome_entry (GNOME_FILE_ENTRY (fileentry))),
						     TRUE, gtk_entry_get_text (GTK_ENTRY (entry)));

			gtk_main_quit ();
		}
		g_free (expanded_filename);
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

void
on_help_button_clicked (GtkWidget *widget, gpointer data) 
{
	display_help ();
}

int
on_save_entry_key_press_event (GtkWidget    	*widget, 
			       GdkEventKey	*key)
{
	if (key->keyval == GDK_Return)
		on_ok_button_clicked (widget, NULL);
	
	return FALSE;
}
int
on_toplevel_key_press_event (GtkWidget *widget,
			     GdkEventKey *key)
{
	if (key->keyval != GDK_Escape) {
		if (key->keyval == GDK_F1)
			display_help ();

		return FALSE;
	}

	gtk_main_quit ();
	return TRUE;
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

#if 0
	if ( ! ensure_temporary ()) {
		/*FIXME: cancel the drag*/
		return;
	}
#endif
	
	string = g_strdup_printf ("file:%s\r\n", temporary_file);
	gtk_selection_data_set (selection_data,
				selection_data->target,
				8, string, strlen (string)+1);
	g_free (string);
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
		(context, gdk_rgb_get_colormap (), pixmap, mask, 0, 0);
}

static void
save_done_notification (void)
{
  temporary_file = g_strdup (screenshot_save_get_filename ());
}

static char *
escape_underscores (const char *name)
{
	GString *escaped;
	int      i;

	g_return_val_if_fail (name != NULL, NULL);

	escaped = g_string_new (name);

	for (i = 0; escaped->str [i]; i++) {
		if (escaped->str [i] != '_')
			continue;

		escaped = g_string_insert_c (escaped, ++i, '_');
	}

	return g_string_free (escaped, FALSE);
}

static void
do_screenshot (gboolean window)
{
  GtkWidget *save_entry;
  GtkWidget *frame;
  struct stat s;
  gchar *file;
  gint width, height; 
  gchar *utf8_name;
  Window win;

  if (!screenshot_grab_lock ())
    exit (0);

  if (window)
    {
      win = screenshot_find_current_window (FALSE);
      if (win == None)
	{
	  window = FALSE;
	  win = GDK_ROOT_WINDOW ();
	}
      else
	{
	  gchar *tmp;

	  window_title = screenshot_get_window_title (win);
	  tmp = screenshot_sanitize_filename (window_title);
	  g_free (window_title);
	  window_title = tmp;
	}
    }
  else
    {
      win = GDK_ROOT_WINDOW ();
    }

  screenshot = screenshot_get_pixbuf (win);

  if (window && drop_shadow)
    {
      GdkPixbuf *old = screenshot;
    
      screenshot = screenshot_add_shadow (screenshot);
      g_object_unref (old);
    }
  screenshot_release_lock ();

  xml = glade_xml_new (GLADEDIR "/gnome-panel-screenshot.glade", NULL, NULL);
  if (xml == NULL)
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new (NULL,  /* parent */
				       0,  /* flags */
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("Glade file for the screenshot program is missing.\n"
					 "Please check your installation of gnome-panel"));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
      exit (1);
    }
  glade_xml_signal_autoconnect (xml);

  if (screenshot == NULL)
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new (NULL, /* parent */
				       0, /* flags */
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("Unable to take a screenshot of "
					 "the current desktop."));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
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
  gtk_widget_set_size_request (preview, width, height);
  gtk_aspect_frame_set (GTK_ASPECT_FRAME (frame), 0.0, 0.5,
			gdk_pixbuf_get_width (screenshot)/
			(gfloat) gdk_pixbuf_get_height (screenshot),
			FALSE);
  if (window)
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);

  file = add_file_to_path (home_dir);
  utf8_name = g_filename_to_utf8 (file, -1, NULL, NULL, NULL);
  gtk_entry_set_text (GTK_ENTRY (save_entry), utf8_name);
  g_free (file);
  g_free (utf8_name);

  if (!stat (web_dir, &s) && S_ISDIR (s.st_mode))
    {
      GtkWidget *cbutton;
      char      *str;
      char      *escaped;
		
      cbutton = glade_xml_get_widget (xml, "web_rbutton");
      gtk_widget_show (cbutton);

      escaped = escape_underscores (web_dir);
      str = g_strdup_printf (_("Save screenshot to _web page (save in %s)"),
			     escaped);
      gtk_button_set_label (GTK_BUTTON (cbutton), str);
      g_free (str);
      g_free (escaped);
    }

  /* setup dnd */
  /* just in case some wanker like nautilus took our image */
  g_signal_connect (G_OBJECT (preview), "drag_begin",
		    G_CALLBACK (drag_begin), NULL);
  g_signal_connect (G_OBJECT (preview), "drag_data_get",
		    G_CALLBACK (drag_data_get), NULL);
  gtk_drag_source_set (preview,
		       GDK_BUTTON1_MASK|GDK_BUTTON3_MASK,
		       drag_types, 2,
		       GDK_ACTION_COPY);

  gtk_widget_grab_focus (save_entry);
  gtk_editable_select_region (GTK_EDITABLE (save_entry), 0, -1);
  g_signal_connect (G_OBJECT (save_entry), "key_press_event",
		    G_CALLBACK (on_save_entry_key_press_event),
		    NULL);

  gtk_widget_show (toplevel);

  screenshot_save_start (screenshot, save_done_notification);
}

static gboolean
do_screenshot_timeout (gpointer data)
{
  gboolean window = GPOINTER_TO_INT (data);

  do_screenshot (window);

  return FALSE;
}

/* main */
int
main (int argc, char *argv[])
{
	GnomeClient *client;
	GConfClient *gconf_client;
	gboolean window = FALSE;
	gchar *shadow_set = NULL;
	guint delay = 0;
	
	struct poptOption opts[] = {
		{"window", '\0', POPT_ARG_NONE, NULL, 0, N_("Grab a window instead of the entire screen"), NULL},
		{"delay", '\0', POPT_ARG_INT, NULL, 0, N_("Take screenshot after specified delay [in seconds]"), NULL},
		{"shadow", '\0', POPT_ARG_STRING, NULL, 0, N_("Add a drop shadow to window screenshots"), NULL},
		{NULL, '\0', 0, NULL, 0, NULL, NULL}
	};

	opts[0].arg = &window;
	opts[1].arg = &delay;
	opts[2].arg = &shadow_set;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gnome_program_init ("gnome-panel-screenshot", VERSION,
			    LIBGNOMEUI_MODULE,
			    argc, argv,
			    GNOME_PARAM_POPT_TABLE, opts,
			    GNOME_PROGRAM_STANDARD_PROPERTIES,
			    NULL);
	glade_gnome_init();
	client = gnome_master_client ();
	gnome_client_set_restart_style (client, GNOME_RESTART_NEVER);

	/* Load our gconf values before taking the screenshot */
	gconf_client = gconf_client_get_default ();
	home_dir = g_get_home_dir ();	
	web_dir = gconf_client_get_string (gconf_client, "/apps/gnome_panel_screenshot/web_dir", NULL);
	if (!web_dir || !web_dir[0]) {
		g_free (web_dir);
		web_dir = g_strconcat (home_dir, G_DIR_SEPARATOR_S, "public_html", NULL);
	}

	if (gconf_client_get_bool (gconf_client, "/apps/nautilus/preferences/desktop_is_home_dir", NULL))
		desktop_dir = g_strdup (home_dir);
	else
		desktop_dir = g_strconcat (home_dir, G_DIR_SEPARATOR_S,
					   "Desktop", NULL);
	drop_shadow = gconf_client_get_bool (gconf_client, "/apps/gnome_panel_screenshot/drop_shadow_window", NULL);
	/* allow the command line to override it */
	if (shadow_set) {
		if (!strcmp (shadow_set, "yes") ||
		    !strcmp (shadow_set, "on") ||
		    !strcmp (shadow_set, "true"))
			drop_shadow = TRUE;
		else if (!strcmp (shadow_set, "no") ||
			 !strcmp (shadow_set, "off") ||
			 !strcmp (shadow_set, "false"))
			drop_shadow = FALSE;
		else
			/* Should figure out how to get popt to print out an error message */
			exit (1);
	}
		
	g_object_unref (gconf_client);

	gtk_window_set_default_icon_name ("applets-screenshooter");

	if (delay > 0) {
		g_timeout_add (delay * 1000, 
			       do_screenshot_timeout,
			       GINT_TO_POINTER (window));
	} else {
		do_screenshot (window);
	}

	gtk_main ();

	return 0;
}
