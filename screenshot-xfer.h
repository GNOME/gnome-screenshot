#ifndef __SCREENSHOT_XFER_H__
#define __SCREENSHOT_XFER_H__

#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs.h>

#define EGG_TYPE_VFS_XFER_DIALOG      (egg_vfs_xfer_dialog_get_type ())
#define EGG_VFS_XFER_DIALOG(widget)   (G_TYPE_CHECK_INSTANCE_CAST ((widget), EGG_TYPE_VFS_XFER_DIALOG, EggVfsXferDialog))
#define EGG_VFS_XFER_DIALOG_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), EGG_TYPE_VFS_XFER_DIALOG, EggVfsXferDialogClass))
#define EGG_IS_VFS_XFER_DIALOG(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EGG_TYPE_VFS_XFER_DIALOG))
#define EGG_IS_VFS_XFER_DIALOG_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), EGG_TYPE_VFS_XFER_DIALOG))
#define EGG_VFS_XFER_DIALOG_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), EGG_TYPE_VFS_XFER_DIALOG, EggVfsXferDialogClass))

typedef struct
{
  GtkDialog parent;
  GtkWidget *title_label;
  GtkWidget *from_label;
  GtkWidget *to_label;
  GtkWidget *target_label;
  GtkWidget *source_label;
  GtkWidget *status_label;
  GtkWidget *progress_bar;
} EggVfsXferDialog;

typedef struct
{
  GtkDialogClass parent_class;
} EggVfsXferDialogClass;


GType      egg_vfs_xfer_dialog_get_type          (void);
GtkWidget *egg_vfs_xfer_dialog_new               (const char       *title,
						  const char       *operation_string,
						  const char       *from_prefix,
						  const char       *to_prefix,
						  gulong            files_total,
						  GnomeVFSFileSize  bytes_total,
						  gboolean          use_timeout);
void       egg_vfs_xfer_dialog_set_title_string  (EggVfsXferDialog *xfer_dialog,
						  const char       *status);
void       egg_vfs_xfer_dialog_set_status_string (EggVfsXferDialog *xfer_dialog,
						  const char       *status);
void       egg_vfs_xfer_dialog_update_sizes      (EggVfsXferDialog *dialog,
						  GnomeVFSFileSize  bytes_done_in_file,
						  GnomeVFSFileSize  bytes_done);
void       egg_vfs_xfer_dialog_set_target_file   (EggVfsXferDialog *xfer_dialog,
						  const char       *status);
void       egg_vfs_xfer_dialog_set_source_file   (EggVfsXferDialog *xfer_dialog,
						  const char       *status);
gboolean   screenshot_xfer_uri                   (GnomeVFSURI      *source_uri,
						  GnomeVFSURI      *target_uri,
						  GtkWidget        *parent);


#endif /* __SCREENSHOT_XFER_H__ */
