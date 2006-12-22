#include <config.h>
#include <gnome.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "screenshot-save.h"

static char *parent_dir = NULL;
static char *tmp_filename = NULL;

static SaveFunction save_callback = NULL;
static gpointer save_user_data = NULL;

extern int errno;



/* Strategy for saving:
 *
 * We keep another process around to handle saving the image.  This is
 * done for two reasons.  One, the saving takes a non-zero amount of
 * time (about a quarter of a second on my box.)  This will make it
 * more interactive.  The second reason is to make the child
 * responsible for cleaning up the temp dir.  If the parent process is
 * killed or segfaults, the child process can clean up the temp dir.
 */
static void
clean_up_temporary_dir (gboolean gui_on_error)
{
  char *message;
  gboolean error_occurred = FALSE;
  if (g_file_test (tmp_filename, G_FILE_TEST_EXISTS))
    error_occurred = unlink (tmp_filename);
  if (g_file_test (parent_dir, G_FILE_TEST_EXISTS))
    error_occurred = rmdir (parent_dir) || error_occurred;

  if (error_occurred)
    {
      message = g_strdup_printf (_("Unable to clear the temporary folder:\n%s"),
				 tmp_filename);
      if (gui_on_error)
	{
	  GtkWidget *dialog;

	  dialog = gtk_message_dialog_new (NULL, 0,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_OK,
					   message);
	  gtk_dialog_run (GTK_DIALOG (dialog));
	  gtk_widget_destroy (dialog);
	}
      else
	{
	  g_warning (message);
	}
      g_free (message);
    }
  g_free (tmp_filename);
  g_free (parent_dir);
}

static void
child_done_notification (GPid     pid,
			 gint     status,
			 gpointer data)
{
  /* This should never be called. */

  /* We expect the child to die after the parent.  If the child dies
   * than it either segfaulted, or was randomly killed.  In either
   * case, we can't reasonably continue.  */
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (NULL, 0,
				   GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_OK,
				   _("The child save process unexpectedly exited.  We are unable to write the screenshot to disk."));
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  clean_up_temporary_dir (TRUE);

  exit (1);
}

static gboolean
read_pipe_from_child (GIOChannel   *source,
		      GIOCondition  condition,
		      gpointer      data)
{
  if (condition & G_IO_IN)
    {
      gchar *message = NULL;
      gchar *error_message = NULL;
      GtkWidget *dialog;
      GIOStatus status;

      status = g_io_channel_read_line (source, &error_message, NULL, NULL, NULL);

      if (status == G_IO_STATUS_NORMAL)
	{
	  message = g_strdup_printf ("Unable to save the screenshot to disk:\n\n%s", error_message);
	  dialog = gtk_message_dialog_new (NULL, 0,
					   GTK_MESSAGE_ERROR,
					   GTK_BUTTONS_OK,
					   message);
	  gtk_dialog_run (GTK_DIALOG (dialog));
	  gtk_widget_destroy (dialog);
	  exit (1);
	}
    }

  (*save_callback) (save_user_data);

  return FALSE;
}

static char *
make_temp_directory (void)
{
  gint result;
  char *dir_name;

  do
    {
      /* FIXME: honor $TMPDIR */
      dir_name = g_strdup_printf ("/tmp/gnome-panel-screenshot-%d", rand ());
      result = mkdir (dir_name, 0700);

      if (result == 0)
	return dir_name;
      g_free (dir_name);

      /* If a dir of that name already exists, we try again.  Otherwise, we give up.  */
      if (errno != EEXIST)
	return NULL;
    }
  while (TRUE);
}

static void
signal_handler (int sig)
{
  clean_up_temporary_dir (FALSE);

  signal (sig, SIG_DFL);
  kill (getpid (), sig);
}

void
screenshot_save_start (GdkPixbuf    *pixbuf,
		       SaveFunction  callback,
		       gpointer      user_data)
{
  GPid pid;
  int parent_exit_notification[2];
  int pipe_from_child[2];

  pipe (parent_exit_notification);
  pipe (pipe_from_child);

  parent_dir = make_temp_directory ();
  if (parent_dir == NULL)
    return;

  tmp_filename = g_build_filename (parent_dir,
		  		   _("Screenshot.png"),
				   NULL);
  save_callback = callback;
  save_user_data = user_data;

  pid = fork ();
  if (pid == 0)
    {
      GError *error = NULL;
      char c;

      signal (SIGINT, signal_handler);
      signal (SIGTERM, signal_handler);

      close (parent_exit_notification [1]);
      close (pipe_from_child [0]);

      if (! gdk_pixbuf_save (pixbuf, tmp_filename,
			     "png", &error,
			     "tEXt::CREATOR", "gnome-panel-screenshot",
			     NULL))
	{
	  if (error && error->message)
	    write (pipe_from_child[1],
		   error->message,
		   strlen (error->message));
	  else
#define ERROR_MESSAGE _("Unknown error saving screenshot to disk")
	    write (pipe_from_child[1],
		   ERROR_MESSAGE,
		   strlen (ERROR_MESSAGE));
	}
      /* By closing the pipe, we let the main process know that we're
       * done saving it. */
      close (pipe_from_child[1]);
      read (parent_exit_notification[0], &c, 1);

      clean_up_temporary_dir (FALSE);
      _exit (0);
    }
  else if (pid > 0)
    {
      GIOChannel *channel;

      close (parent_exit_notification[0]);
      close (pipe_from_child[1]);

      channel = g_io_channel_unix_new (pipe_from_child[0]);
      g_io_add_watch (channel,
		      G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
		      read_pipe_from_child,
		      NULL);
      g_io_channel_unref (channel);
      g_child_watch_add (pid, child_done_notification, NULL);
    }
  else
    /* George awesomely wrote code originally to handle the
     * could-not-fork case synchronously.  I'm not copying it, as I'm
     * guessing that the system is pretty hosed if that's the case.
     * However, he gets major kudos for trying. (-:
     */
    g_assert_not_reached ();
}

const char *
screenshot_save_get_filename (void)
{
  return tmp_filename;
}

gchar *
screenshot_sanitize_filename (const char *filename)
{
  char *retval, *p;

  g_assert (filename);
  g_assert (g_utf8_validate (filename, -1, NULL));

  retval = gnome_vfs_escape_host_and_path_string (filename);

  for (p = retval; *p != '\000'; p = g_utf8_next_char (p))
    {
      if (*p == G_DIR_SEPARATOR)
	*p = '-';
    }

  return retval;
}
