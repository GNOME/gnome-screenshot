#ifndef __SCREENSHOT_DIALOG_H__
#define __SCREENSHOT_DIALOG_H__

#include <gtk/gtk.h>
#include <glade/glade.h>

typedef struct ScreenshotDialog ScreenshotDialog;

ScreenshotDialog *screenshot_dialog_new          (GdkPixbuf        *screenshot,
						  char             *initial_uri,
						  gboolean          take_window_shot);
void              screenshot_dialog_enable_dnd   (ScreenshotDialog *dialog);
GtkWidget        *screenshot_dialog_get_toplevel (ScreenshotDialog *dialog);
char             *screenshot_dialog_get_uri      (ScreenshotDialog *dialog);
void              screenshot_dialog_set_busy     (ScreenshotDialog *dialog,
						  gboolean          busy);
#endif /* __SCREENSHOT_DIALOG_H__ */
