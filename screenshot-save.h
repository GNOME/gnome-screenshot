#ifndef __SCREENSHOT_SAVE_H__
#define __SCREENSHOT_SAVE_H__

#include <gtk/gtk.h>

typedef void (*SaveNotifyFunc) (void);

void        screenshot_save_start        (GdkPixbuf      *pixbuf,
					  SaveNotifyFunc  func);
const char *screenshot_save_get_filename (void);
gchar      *screenshot_sanitize_filename (const char     *filename);

#endif /* __SCREENSHOT_SAVE_H__ */
