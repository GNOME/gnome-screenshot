#include "screenshot-xfer.h"
#include <gnome.h>
#include <time.h>

typedef struct
{
  GnomeVFSAsyncHandle *handle;
  gboolean canceled;
  GtkWidget *parent_dialog;
  GtkWidget *progress_dialog;
  gboolean delete_target;
  guint timeout_id;
} TransferInfo;

G_DEFINE_TYPE (EggVfsXferDialog, egg_vfs_xfer_dialog, GTK_TYPE_DIALOG);

static void
egg_vfs_xfer_dialog_init (EggVfsXferDialog *xfer_dialog)
{
  GtkWidget *vbox;
  GtkWidget *button;
  GtkWidget *table;
  GtkWidget *align;
  GtkWidget *progress_vbox;
  gchar *title;
  gchar *message;

  /* Set up the initial dialog */
  gtk_container_set_border_width (GTK_CONTAINER (xfer_dialog), 6);
  gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (xfer_dialog)->vbox), 24);
  vbox = gtk_vbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (xfer_dialog)->vbox), vbox, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  gtk_dialog_set_has_separator (GTK_DIALOG (xfer_dialog), FALSE);

  /* Set up the title */
  gtk_window_set_title (GTK_WINDOW (xfer_dialog), _("Saving screenshot"));
  title = g_strdup_printf ("<span size=\"larger\" weight=\"bold\">%s</span>", _("Saving screenshot"));
  xfer_dialog->title_label = gtk_label_new (title);
  gtk_label_set_use_markup (GTK_LABEL (xfer_dialog->title_label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (xfer_dialog->title_label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), xfer_dialog->title_label, FALSE, FALSE, 0);

  /* Set up the From: and To: labels */
  align = gtk_alignment_new (0.0, 0.5, 1.0, 0.0);
  table = gtk_table_new (2, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 3);
  gtk_table_set_col_spacings (GTK_TABLE (table), 3);
  gtk_container_add (GTK_CONTAINER (align), table);
  gtk_box_pack_start (GTK_BOX (vbox), align, FALSE, FALSE, 0);

  message = g_strdup_printf ("<b>%s</b>", _("From:"));
  xfer_dialog->from_label = gtk_label_new (message);
  gtk_misc_set_alignment (GTK_MISC (xfer_dialog->from_label), 0.0, 0.5);
  gtk_label_set_use_markup (GTK_LABEL (xfer_dialog->from_label), TRUE);
  gtk_table_attach (GTK_TABLE (table), xfer_dialog->from_label,
		    0, 1, 0, 1,
		    GTK_FILL,
		    GTK_FILL,
		    0, 0);

  xfer_dialog->source_label = gtk_label_new (NULL);
  gtk_misc_set_alignment (GTK_MISC (xfer_dialog->source_label), 0.0, 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (xfer_dialog->source_label), PANGO_ELLIPSIZE_START);
  gtk_table_attach (GTK_TABLE (table), xfer_dialog->source_label,
		    1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);
  
  message = g_strdup_printf ("<b>%s</b>", _("To:"));
  xfer_dialog->to_label = gtk_label_new (message);
  gtk_misc_set_alignment (GTK_MISC (xfer_dialog->to_label), 0.0, 0.5);
  gtk_label_set_use_markup (GTK_LABEL (xfer_dialog->to_label), TRUE);
  gtk_table_attach (GTK_TABLE (table), xfer_dialog->to_label,
		    0, 1, 1, 2,
		    GTK_FILL,
		    GTK_FILL,
		    0, 0);

  xfer_dialog->target_label = gtk_label_new (NULL);
  gtk_misc_set_alignment (GTK_MISC (xfer_dialog->target_label), 0.0, 0.5);
  gtk_label_set_ellipsize (GTK_LABEL (xfer_dialog->target_label), PANGO_ELLIPSIZE_START);
  gtk_table_attach (GTK_TABLE (table), xfer_dialog->target_label,
		    1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);

  /* Progress Bar */
  progress_vbox = gtk_vbox_new (FALSE, 3);
  xfer_dialog->progress_bar = gtk_progress_bar_new ();
  gtk_widget_set_size_request (xfer_dialog->progress_bar, 350, -1);
  xfer_dialog->status_label = gtk_label_new (" ");
  gtk_box_pack_start (GTK_BOX (progress_vbox), xfer_dialog->progress_bar,
		      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (progress_vbox), xfer_dialog->status_label,
		      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), progress_vbox,
		      FALSE, FALSE, 0);
  button = gtk_dialog_add_button (GTK_DIALOG (xfer_dialog),
				  GTK_STOCK_CANCEL,
				  GTK_RESPONSE_CANCEL);
  button = gtk_dialog_add_button (GTK_DIALOG (xfer_dialog),
				  _("_Pause"),
				  GTK_RESPONSE_OK);

  gtk_widget_show_all (vbox);
}


void
egg_vfs_xfer_dialog_set_title_string (EggVfsXferDialog *xfer_dialog,
				      const char       *title)
{
  gchar *markup_text = NULL;

  g_return_if_fail (EGG_IS_VFS_XFER_DIALOG (xfer_dialog));

  if (title)
    markup_text = g_strdup_printf ("<span size=\"larger\" weight=\"bold\">%s</span>",
				   title);
  gtk_label_set_markup (GTK_LABEL (xfer_dialog->title_label),
			markup_text);
  g_free (markup_text);
}

void
egg_vfs_xfer_dialog_set_status_string (EggVfsXferDialog *xfer_dialog,
				       const char       *status)
{
  gchar *markup_text = NULL;

  g_return_if_fail (EGG_IS_VFS_XFER_DIALOG (xfer_dialog));

  if (status)
    markup_text = g_strdup_printf ("<span style=\"italic\">%s</span>",
				   status);
  gtk_label_set_markup (GTK_LABEL (xfer_dialog->status_label),
			markup_text);
  g_free (markup_text);
}

static void
egg_vfs_xfer_dialog_class_init (EggVfsXferDialogClass *klass)
{
}

GtkWidget *
egg_vfs_xfer_dialog_new (const char       *title,
			 const char       *operation_string,
			 const char       *from_prefix,
			 const char       *to_prefix,
			 gulong            files_total,
			 GnomeVFSFileSize  bytes_total,
			 gboolean          use_timeout)
{
  GtkWidget *dialog;

  dialog = g_object_new (EGG_TYPE_VFS_XFER_DIALOG,
			 "title", title,
			 NULL);

  return dialog;
}

void
egg_vfs_xfer_dialog_update_sizes      (EggVfsXferDialog *dialog,
				       GnomeVFSFileSize  bytes_done_in_file,
				       GnomeVFSFileSize  bytes_done)
{
  g_return_if_fail (EGG_IS_VFS_XFER_DIALOG (dialog));

}

/* VFS code below here.  I'm planning to split these two sections into seperate
 * files at some point. */

static gboolean show_dialog_timeout (TransferInfo *transfer_info);

static void
remove_timeout (TransferInfo *transfer_info)
{
  g_assert (transfer_info);

  if (transfer_info->timeout_id)
    {
      g_source_remove (transfer_info->timeout_id);
      transfer_info->timeout_id = 0;
    }
}

static void
add_timeout (TransferInfo *transfer_info)
{
  if (transfer_info->timeout_id == 0)
    transfer_info->timeout_id = g_timeout_add (1000, (GSourceFunc) show_dialog_timeout, transfer_info);
}

static GtkWidget *
transfer_info_get_parent (TransferInfo *transfer_info)
{
  g_assert (transfer_info);

  if (transfer_info->progress_dialog)
    return transfer_info->progress_dialog;
  if (transfer_info->parent_dialog)
    return transfer_info->parent_dialog;
  return NULL;
}

static int
handle_transfer_ok (const GnomeVFSXferProgressInfo *progress_info,
		    TransferInfo                   *transfer_info)
{
  if (transfer_info->canceled
      && progress_info->phase != GNOME_VFS_XFER_PHASE_COMPLETED)
    {
      /* If cancelled, delete any partially copied files that are laying
       * around and return. Don't delete the source though..
       */
      if (progress_info->target_name != NULL
	  && progress_info->source_name != NULL
	  && strcmp (progress_info->source_name, progress_info->target_name) != 0
	  && progress_info->bytes_total != progress_info->bytes_copied)
	{
	  transfer_info->delete_target = TRUE;
	}

      gtk_main_quit ();
      return 0;
    }

  if (progress_info->phase == GNOME_VFS_XFER_PHASE_COMPLETED)
    {
      remove_timeout (transfer_info);
      if (transfer_info->progress_dialog)
	{
	  gtk_widget_destroy (transfer_info->progress_dialog);
	  transfer_info->progress_dialog = NULL;
	}
      gtk_main_quit ();
      return 0;
    }

  /* Update the dialog, if need be */
  if (transfer_info->progress_dialog == NULL)
    return 1;

  switch (progress_info->phase)
    {
      /* Initial phase */
    case GNOME_VFS_XFER_PHASE_INITIAL:
      /* Checking if destination can handle move/copy */
      return 1;
    case GNOME_VFS_XFER_CHECKING_DESTINATION:
    case GNOME_VFS_XFER_PHASE_COLLECTING:
    case GNOME_VFS_XFER_PHASE_READYTOGO:
      egg_vfs_xfer_dialog_set_status_string (EGG_VFS_XFER_DIALOG (transfer_info->progress_dialog),
					     _("Preparing to copy"));
      return 1;
    case GNOME_VFS_XFER_PHASE_OPENSOURCE:
    case GNOME_VFS_XFER_PHASE_OPENTARGET:
    case GNOME_VFS_XFER_PHASE_COPYING:
    case GNOME_VFS_XFER_PHASE_WRITETARGET:
    case GNOME_VFS_XFER_PHASE_CLOSETARGET:
      if (progress_info->bytes_copied == 0)
	{
	}
      else
	{
	  egg_vfs_xfer_dialog_update_sizes
	    (EGG_VFS_XFER_DIALOG (transfer_info->progress_dialog),
	     MIN (progress_info->bytes_copied, 
		  progress_info->bytes_total),
	     MIN (progress_info->total_bytes_copied,
		  progress_info->bytes_total));
	}
      return 1;
    case GNOME_VFS_XFER_PHASE_FILECOMPLETED:
    case GNOME_VFS_XFER_PHASE_CLEANUP:
      egg_vfs_xfer_dialog_set_status_string (EGG_VFS_XFER_DIALOG (transfer_info->progress_dialog),
					     _("Done copying file"));
      return 1;
      /* Phases we don't expect to see */
    case GNOME_VFS_XFER_PHASE_COMPLETED:
    case GNOME_VFS_XFER_PHASE_SETATTRIBUTES:
    case GNOME_VFS_XFER_PHASE_CLOSESOURCE:
    case GNOME_VFS_XFER_PHASE_MOVING:
    case GNOME_VFS_XFER_PHASE_DELETESOURCE:
    case GNOME_VFS_XFER_PHASE_READSOURCE:
    default:
      g_print ("Unexpected phase (%d) hit\n", progress_info->phase);
      g_assert_not_reached ();
      return 0;
    }
  return 1;
}

static gint
handle_transfer_vfs_error (GnomeVFSXferProgressInfo *progress_info,
			   TransferInfo             *transfer_info)
{
  g_print ("handle_transfer_vfs_error!\n");
  remove_timeout (transfer_info);
  if (transfer_info->progress_dialog)
    {
      gtk_widget_destroy (transfer_info->progress_dialog);
      transfer_info->progress_dialog = NULL;
    }
  gtk_main_quit ();
  return 1;
}

static int
handle_transfer_overwrite (const GnomeVFSXferProgressInfo *progress_info,
			   TransferInfo                   *transfer_info)
{
  GtkWidget *dialog;
  gint response;
  gint need_timeout;

  need_timeout = (transfer_info->timeout_id > 0);
  remove_timeout (transfer_info);

  /* We need to ask the user if they want to overrwrite this file */
  dialog = gtk_message_dialog_new (GTK_WINDOW (transfer_info_get_parent (transfer_info)),
				   GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				   GTK_MESSAGE_QUESTION,
				   GTK_BUTTONS_NONE,
				   _("File already exists"));

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					    _("The file \"%s\" already exists.  Would you like to replace it?"), 
					    progress_info->target_name);
  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_CANCEL,
			 GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 _("_Replace"),
			 GTK_RESPONSE_OK);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  if (response == GTK_RESPONSE_OK)
    return GNOME_VFS_XFER_OVERWRITE_ACTION_REPLACE;
  else
    return GNOME_VFS_XFER_OVERWRITE_ACTION_SKIP;

  if (need_timeout)
    add_timeout (transfer_info);
}

static gint
handle_transfer_duplicate (GnomeVFSXferProgressInfo *progress_info,
			   TransferInfo             *transfer_info)
{
  g_print ("handle_transfer_duplicate!\n");
  return 1;
}

static gint
screenshot_async_xfer_progress (GnomeVFSAsyncHandle      *handle,
				GnomeVFSXferProgressInfo *progress_info,
				gpointer                  data)
{
  TransferInfo *transfer_info = data;

  switch (progress_info->status)
    {
    case GNOME_VFS_XFER_PROGRESS_STATUS_OK:
      return handle_transfer_ok (progress_info, transfer_info);
    case GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR:
      return handle_transfer_vfs_error (progress_info, transfer_info);
    case GNOME_VFS_XFER_PROGRESS_STATUS_OVERWRITE:
      return handle_transfer_overwrite (progress_info, transfer_info);
    case GNOME_VFS_XFER_PROGRESS_STATUS_DUPLICATE:
      return handle_transfer_duplicate (progress_info, transfer_info);
    default:
      g_warning (_("Unknown GnomeVFSXferProgressStatus %d"),
		 progress_info->status);
      return 0;
    }
}

static gboolean
show_dialog_timeout (TransferInfo *transfer_info)
{
  transfer_info->progress_dialog =
    egg_vfs_xfer_dialog_new (NULL, NULL, NULL, NULL, 0, 0, FALSE);
  gtk_widget_show (transfer_info->progress_dialog);
  transfer_info->timeout_id = 0;

  return FALSE;
}

gboolean
screenshot_xfer_uri (GnomeVFSURI *source_uri,
		     GnomeVFSURI *target_uri,
		     GtkWidget   *parent)
{
  GnomeVFSResult result;
  GList *source_uri_list = NULL;
  GList *target_uri_list = NULL;
  TransferInfo *transfer_info;

  source_uri_list = g_list_prepend (source_uri_list, source_uri);
  target_uri_list = g_list_prepend (target_uri_list, target_uri);

  transfer_info = g_new0 (TransferInfo, 1);
  transfer_info->parent_dialog = parent;
  add_timeout (transfer_info);
  result = gnome_vfs_async_xfer (&transfer_info->handle,
				 source_uri_list, target_uri_list,
				 GNOME_VFS_XFER_DEFAULT,
				 GNOME_VFS_XFER_ERROR_MODE_QUERY,
				 GNOME_VFS_XFER_OVERWRITE_MODE_QUERY,
				 GNOME_VFS_PRIORITY_DEFAULT,
				 screenshot_async_xfer_progress, transfer_info,
				 NULL, NULL);
  gtk_main ();
  remove_timeout (transfer_info);
  g_list_free (source_uri_list);
  g_list_free (target_uri_list);

  if (transfer_info->delete_target)
    ;/* try to delete the target, iff it started writing */
  return TRUE;
}

