#ifndef __SCREENSHOT_SAVE_H__
#define __SCREENSHOT_SAVE_H__

#include <gtk/gtk.h>

typedef void (*SaveFunction) (gpointer data);

void        screenshot_save_start        (GdkPixbuf    *pixbuf,
					  SaveFunction  callback,
					  gpointer      user_data);
const char *screenshot_save_get_filename (void);
gchar      *screenshot_sanitize_filename (const char   *filename);


#endif /* __SCREENSHOT_SAVE_H__ */
