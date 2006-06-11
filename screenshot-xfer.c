#include "config.h"

#include "screenshot-xfer.h"
#include "gnome-egg-xfer-dialog.h"

#include <time.h>
#include <glib/gi18n.h>

typedef struct
{
  GnomeVFSAsyncHandle *handle;
  const char *operation_title;	/* "Copying files" */
  const char *action_label;	/* "Files copied:" */
  const char *progress_verb;	/* "Copying" */
  const char *preparation_name;	/* "Preparing To Copy..." */
  const char *cleanup_name;	/* "Finishing Move..." */
  GtkWidget *parent_dialog;
  GtkWidget *progress_dialog;
  gboolean delete_target;
  guint timeout_id;
  gboolean canceled;
} TransferInfo;

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
    case GNOME_VFS_XFER_PHASE_COLLECTING:
    case GNOME_VFS_XFER_CHECKING_DESTINATION:
      gnome_egg_xfer_dialog_set_operation_string (GNOME_EGG_XFER_DIALOG (transfer_info->progress_dialog),
						  _("Preparing to copy"));
      return 1;
    case GNOME_VFS_XFER_PHASE_READYTOGO:
      gnome_egg_xfer_dialog_set_operation_string
	(GNOME_EGG_XFER_DIALOG (transfer_info->progress_dialog),
	 transfer_info->action_label);
      gnome_egg_xfer_dialog_set_total
	(GNOME_EGG_XFER_DIALOG (transfer_info->progress_dialog),
	 progress_info->files_total,
	 progress_info->bytes_total);
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
	  gnome_egg_xfer_dialog_update_sizes
	    (GNOME_EGG_XFER_DIALOG (transfer_info->progress_dialog),
	     MIN (progress_info->bytes_copied, 
		  progress_info->bytes_total),
	     MIN (progress_info->total_bytes_copied,
		  progress_info->bytes_total));
	}
      return 1;
    case GNOME_VFS_XFER_PHASE_FILECOMPLETED:
    case GNOME_VFS_XFER_PHASE_CLEANUP:
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
	  gnome_egg_xfer_dialog_new (NULL, NULL, NULL, NULL, 0, 0, FALSE);
  gtk_widget_show (transfer_info->progress_dialog);
  transfer_info->timeout_id = 0;

  return FALSE;
}

GnomeVFSResult
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

  return GNOME_VFS_OK;
}

