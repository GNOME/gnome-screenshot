/*
 * Copyright © 2008 Alexander “weej” Jones <alex@weej.com>
 * Copyright © 2008 Thomas Perl <thp@thpinfo.com>
 * Copyright © 2009 daniel g. siegel <dgsiegel@gnome.org>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This is a "flash" object that you can create and invoke a method "flash" on to
 * flood the screen with white temporarily */

#include <gtk/gtk.h>

#include "cheese-flash.h"

#ifdef GDK_WINDOWING_X11
#include <X11/Xproto.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#endif /* GDK_WINDOWING_X11 */

/* How long to hold the flash for */
#define FLASH_DURATION 250

/* The factor which defines how much the flash fades per frame */
#define FLASH_FADE_FACTOR 0.95

/* How many frames per second */
#define FLASH_ANIMATION_RATE 50

/* When to consider the flash finished so we can stop fading */
#define FLASH_LOW_THRESHOLD 0.01

G_DEFINE_TYPE (CheeseFlash, cheese_flash, G_TYPE_OBJECT);

#define CHEESE_FLASH_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CHEESE_TYPE_FLASH, CheeseFlashPrivate))

typedef struct
{
  GtkWindow *window;
  guint flash_timeout_tag;
  guint fade_timeout_tag;
} CheeseFlashPrivate;

/* Copy-pasted from totem/src/backend/video-utils.c
 * Waiting on GTK+ bug:
 * https://bugzilla.gnome.org/show_bug.cgi?id=523574 */
#ifdef GDK_WINDOWING_X11
static int
get_current_desktop (GdkScreen *screen)
{
        Display *display;
        Window win;
        Atom current_desktop, type;
        int format;
        unsigned long n_items, bytes_after;
        unsigned char *data_return = NULL;
        int workspace = 0;

        display = GDK_DISPLAY_XDISPLAY (gdk_screen_get_display (screen));
        win = XRootWindow (display, GDK_SCREEN_XNUMBER (screen));

        current_desktop = XInternAtom (display, "_NET_CURRENT_DESKTOP", True);

        XGetWindowProperty (display,
                            win,
                            current_desktop,
                            0, G_MAXLONG,
                            False, XA_CARDINAL,
                            &type, &format, &n_items, &bytes_after,
                            &data_return);

        if (type == XA_CARDINAL && format == 32 && n_items > 0)
                workspace = (int) data_return[0];
        if (data_return)
                XFree (data_return);

        return workspace;
}

static gboolean
get_work_area (GdkScreen      *screen,
	       GdkRectangle   *rect)
{
	Atom            workarea;
	Atom            type;
	Window          win;
	int             format;
	gulong          num;
	gulong          leftovers;
	gulong          max_len = 4 * 32;
	guchar         *ret_workarea;
	long           *workareas;
	int             result;
	int             disp_screen;
	int             desktop;
	Display        *display;

	display = GDK_DISPLAY_XDISPLAY (gdk_screen_get_display (screen));
	workarea = XInternAtom (display, "_NET_WORKAREA", True);

	disp_screen = GDK_SCREEN_XNUMBER (screen);

	/* Defaults in case of error */
	rect->x = 0;
	rect->y = 0;
	rect->width = gdk_screen_get_width (screen);
	rect->height = gdk_screen_get_height (screen);

	if (workarea == None)
		return FALSE;

	win = XRootWindow (display, disp_screen);
	result = XGetWindowProperty (display,
				     win,
				     workarea,
				     0,
				     max_len,
				     False,
				     AnyPropertyType,
				     &type,
				     &format,
				     &num,
				     &leftovers,
				     &ret_workarea);

	if (result != Success
	    || type == None
	    || format == 0
	    || leftovers
	    || num % 4) {
		return FALSE;
	}

	desktop = get_current_desktop (screen);

	workareas = (long *) ret_workarea;
	rect->x = workareas[desktop * 4];
	rect->y = workareas[desktop * 4 + 1];
	rect->width = workareas[desktop * 4 + 2];
	rect->height = workareas[desktop * 4 + 3];

	XFree (ret_workarea);

	return TRUE;
}
#endif /* GDK_WINDOWING_X11 */

static gboolean
cheese_flash_window_draw_event_cb (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
  cairo_fill (cr);
  return TRUE;
}

static void
cheese_flash_init (CheeseFlash *self)
{
  CheeseFlashPrivate *priv = CHEESE_FLASH_GET_PRIVATE (self);
  cairo_region_t *input_region;
  GtkWindow *window;
  GdkScreen *screen;
  GdkVisual *visual;

  priv->flash_timeout_tag = 0;
  priv->fade_timeout_tag  = 0;

  window = GTK_WINDOW (gtk_window_new (GTK_WINDOW_POPUP));

  /* make it so it doesn't look like a window on the desktop (+fullscreen) */
  gtk_window_set_decorated (window, FALSE);
  gtk_window_set_skip_taskbar_hint (window, TRUE);
  gtk_window_set_skip_pager_hint (window, TRUE);
  gtk_window_set_keep_above (window, TRUE);
  gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_NOTIFICATION);

  /* Don't take focus */
  gtk_window_set_accept_focus (window, FALSE);
  gtk_window_set_focus_on_map (window, FALSE);

  /* no shadow */
  screen = gtk_widget_get_screen (GTK_WIDGET (window));
  visual = gdk_screen_get_rgba_visual (screen);
  if (visual == NULL)
    visual = gdk_screen_get_system_visual (screen);

  gtk_widget_set_visual (GTK_WIDGET (window), visual);

  /* Don't consume input */
  gtk_widget_realize (GTK_WIDGET (window));

  input_region = cairo_region_create ();
  gdk_window_input_shape_combine_region (gtk_widget_get_window (GTK_WIDGET (window)), input_region, 0, 0);
  cairo_region_destroy (input_region);

  g_signal_connect (G_OBJECT (window), "draw", G_CALLBACK (cheese_flash_window_draw_event_cb), NULL);
  priv->window = window;
}

static void
cheese_flash_dispose (GObject *object)
{
  CheeseFlashPrivate *priv = CHEESE_FLASH_GET_PRIVATE (object);

  if (priv->window != NULL)
  {
    gtk_widget_destroy (GTK_WIDGET (priv->window));
    priv->window = NULL;
  }

  if (G_OBJECT_CLASS (cheese_flash_parent_class)->dispose)
    G_OBJECT_CLASS (cheese_flash_parent_class)->dispose (object);
}

static void
cheese_flash_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (cheese_flash_parent_class)->finalize)
    G_OBJECT_CLASS (cheese_flash_parent_class)->finalize (object);
}

static void
cheese_flash_class_init (CheeseFlashClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (CheeseFlashPrivate));

  object_class->dispose      = cheese_flash_dispose;
  object_class->finalize     = cheese_flash_finalize;
}

static gboolean
cheese_flash_opacity_fade (gpointer data)
{
  CheeseFlash        *flash        = data;
  CheeseFlashPrivate *flash_priv   = CHEESE_FLASH_GET_PRIVATE (flash);
  GtkWindow          *flash_window = flash_priv->window;
  double              opacity      = gtk_window_get_opacity (flash_window);

  /* exponentially decrease */
  gtk_window_set_opacity (flash_window, opacity * FLASH_FADE_FACTOR);

  if (opacity <= FLASH_LOW_THRESHOLD)
  {
    /* the flasher has finished when we reach the quit value */
    gtk_widget_hide (GTK_WIDGET (flash_window));
    return FALSE;
  }

  return TRUE;
}

static gboolean
cheese_flash_start_fade (gpointer data)
{
  CheeseFlashPrivate *flash_priv = CHEESE_FLASH_GET_PRIVATE (CHEESE_FLASH (data));

  GtkWindow *flash_window = flash_priv->window;

  /* If the screen is non-composited, just hide and finish up */
  if (!gdk_screen_is_composited (gtk_window_get_screen (flash_window)))
  {
    gtk_widget_hide (GTK_WIDGET (flash_window));
    return FALSE;
  }

  flash_priv->fade_timeout_tag = g_timeout_add (1000.0 / FLASH_ANIMATION_RATE, cheese_flash_opacity_fade, data);
  return FALSE;
}

void
cheese_flash_fire (CheeseFlash  *flash,
                   GdkRectangle *rect)
{
  CheeseFlashPrivate *flash_priv = CHEESE_FLASH_GET_PRIVATE (flash);
  GdkScreen          *screen;
  int                 monitor;
  GtkWindow *flash_window = flash_priv->window;

  if (flash_priv->flash_timeout_tag > 0)
    g_source_remove (flash_priv->flash_timeout_tag);
  if (flash_priv->fade_timeout_tag > 0)
    g_source_remove (flash_priv->fade_timeout_tag);

  gtk_window_resize (flash_window, rect->width, rect->height);
  gtk_window_move (flash_window, rect->x, rect->y);

  gtk_window_set_opacity (flash_window, 1);
  gtk_widget_show_all (GTK_WIDGET (flash_window));
  flash_priv->flash_timeout_tag = g_timeout_add (FLASH_DURATION, cheese_flash_start_fade, (gpointer) flash);
}

CheeseFlash *
cheese_flash_new (void)
{
  return g_object_new (CHEESE_TYPE_FLASH, NULL);
}
