#ifndef __SCREENSHOT_UTILS_H__
#define __SCREENSHOT_UTILS_H__

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

gboolean   screenshot_grab_lock           (void);
void       screenshot_release_lock        (void);
gchar     *screenshot_get_window_title    (Window   w);
Window     screenshot_find_current_window (gboolean include_decoration);
GdkPixbuf *screenshot_get_pixbuf          (Window   w);

#endif /* __SCREENSHOT_UTILS_H__ */
