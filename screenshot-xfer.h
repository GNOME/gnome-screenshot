#ifndef __SCREENSHOT_XFER_H__
#define __SCREENSHOT_XFER_H__

#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs.h>

GnomeVFSResult screenshot_xfer_uri (GnomeVFSURI *source_uri,
				    GnomeVFSURI *target_uri,
				    GtkWidget   *parent);

#endif /* __SCREENSHOT_XFER_H__ */
