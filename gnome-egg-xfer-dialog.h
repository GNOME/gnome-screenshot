/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* gnome-egg-xfer-dialog.h - Progress dialog for transfer 
   operations in the GNOME Desktop File Operation Service.

   Copyright (C) 1999, 2000 Free Software Foundation
   Copyright (C) 2000, 2001 Eazel Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; see the file COPYING.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: 
        Ettore Perazzoli <ettore@gnu.org> 
        Pavel Cisler <pavel@eazel.com>
*/

#ifndef GNOME_EGG_XFER_DIALOG_H
#define GNOME_EGG_XFER_DIALOG_H

#include <gtk/gtkdialog.h>
#include <libgnomevfs/gnome-vfs-file-size.h>

#define GNOME_TYPE_EGG_XFER_DIALOG              (gnome_egg_xfer_dialog_get_type ())
#define GNOME_EGG_XFER_DIALOG(widget)           (G_TYPE_CHECK_INSTANCE_CAST ((widget), GNOME_TYPE_EGG_XFER_DIALOG, GnomeEggXferDialog))
#define GNOME_EGG_XFER_DIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GNOME_TYPE_EGG_XFER_DIALOG, GnomeEggXferDialogClass))
#define GNOME_IS_EGG_XFER_DIALOG(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNOME_TYPE_EGG_XFER_DIALOG))
#define GNOME_IS_EGG_XFER_DIALOG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GNOME_TYPE_EGG_XFER_DIALOG))
#define GNOME_EGG_XFER_DIALOG_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GNOME_TYPE_EGG_XFER_DIALOG, GnomeEggXferDialogClass))

typedef struct GnomeEggXferDialogPrivate GnomeEggXferDialogPrivate;

typedef struct
{
  GtkDialog dialog;
  GnomeEggXferDialogPrivate *priv;
} GnomeEggXferDialog;

typedef struct
{
  GtkDialogClass parent_class;
} GnomeEggXferDialogClass;

GType      gnome_egg_xfer_dialog_get_type             (void);
GtkWidget *gnome_egg_xfer_dialog_new                  (const char         *title,
                                                       const char         *operation_string,
                                                       const char         *from_prefix,
                                                       const char         *to_prefix,
                                                       gulong              files_total,
                                                       GnomeVFSFileSize    bytes_total,
                                                       gboolean            use_timeout);
void       gnome_egg_xfer_dialog_done                 (GnomeEggXferDialog *dialog);
void       gnome_egg_xfer_dialog_set_progress_title   (GnomeEggXferDialog *dialog,
                                                       const char         *progress_title);
void       gnome_egg_xfer_dialog_set_total            (GnomeEggXferDialog *dialog,
                                                       gulong              files_total,
                                                       GnomeVFSFileSize    bytes_total);
void       gnome_egg_xfer_dialog_set_operation_string (GnomeEggXferDialog *dialog,
                                                       const char         *operation_string);
void       gnome_egg_xfer_dialog_clear                (GnomeEggXferDialog *dialog);
void       gnome_egg_xfer_dialog_new_file             (GnomeEggXferDialog *dialog,
                                                       const char         *progress_verb,
                                                       const char         *item_name,
                                                       const char         *from_path,
                                                       const char         *to_path,
                                                       const char         *from_prefix,
                                                       const char         *to_prefix,
                                                       gulong              file_index,
                                                       GnomeVFSFileSize    size);
void       gnome_egg_xfer_dialog_update_sizes         (GnomeEggXferDialog *dialog,
                                                       GnomeVFSFileSize    bytes_done_in_file,
                                                       GnomeVFSFileSize    bytes_done);
void       gnome_egg_xfer_dialog_pause_timeout        (GnomeEggXferDialog *progress);
void       gnome_egg_xfer_dialog_resume_timeout       (GnomeEggXferDialog *progress);



#endif /* GNOME_EGG_XFER_DIALOG_H */
