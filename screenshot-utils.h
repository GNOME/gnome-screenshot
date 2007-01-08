#ifndef __SCREENSHOT_UTILS_H__
#define __SCREENSHOT_UTILS_H__

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

G_BEGIN_DECLS

gboolean   screenshot_grab_lock           (void);
void       screenshot_release_lock        (void);
gchar     *screenshot_get_window_title    (Window   w);
Window     screenshot_find_current_window (gboolean include_decoration);
GdkPixbuf *screenshot_get_pixbuf          (Window   w);

void       screenshot_show_error_dialog   (GtkWindow   *parent,
                                           const gchar *message,
                                           const gchar *detail);
void       screenshot_show_gerror_dialog  (GtkWindow   *parent,
                                           const gchar *message,
                                           GError      *error);

G_END_DECLS

#endif /* __SCREENSHOT_UTILS_H__ */
