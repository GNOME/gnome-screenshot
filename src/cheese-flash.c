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

#include "config.h"

#ifdef HAVE_X11

#include <gtk/gtk.h>

#include "cheese-flash.h"

/**
 * SECTION:cheese-flash
 * @short_description: Flash the screen, like a real camera flash
 * @stability: Unstable
 * @include: cheese/cheese-flash.h
 *
 * #CheeseFlash is a window that you can create and invoke a method "flash" on
 * to temporarily flood the screen with white.
 */

/* How long to hold the flash for, in milliseconds. */
static const guint FLASH_DURATION = 150;

/* The factor which defines how much the flash fades per frame */
static const gdouble FLASH_FADE_FACTOR = 0.95;

/* How many frames per second */
static const guint FLASH_ANIMATION_RATE = 120;

/* When to consider the flash finished so we can stop fading */
static const gdouble FLASH_LOW_THRESHOLD = 0.01;

/*
 * CheeseFlashPrivate:
 * @flash_timeout_tag: signal ID of the timeout to start fading in the flash
 * @fade_timeout_tag: signal ID of the timeout to start fading out the flash
 *
 * Private data for #CheeseFlash.
 */
typedef struct
{
  /*< private >*/
  guint flash_timeout_tag;
  guint fade_timeout_tag;
  gdouble opacity;
} CheeseFlashPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (CheeseFlash, cheese_flash, GTK_TYPE_WINDOW)

static void
cheese_flash_init (CheeseFlash *self)
{
  CheeseFlashPrivate *priv = cheese_flash_get_instance_private (self);
  cairo_region_t *input_region;
  GtkWindow *window = GTK_WINDOW (self);
  const GdkRGBA white = { 1.0, 1.0, 1.0, 1.0 };

  priv->flash_timeout_tag = 0;
  priv->fade_timeout_tag  = 0;
  priv->opacity = 1.0;

  /* make it so it doesn't look like a window on the desktop (+fullscreen) */
  gtk_window_set_decorated (window, FALSE);
  gtk_window_set_skip_taskbar_hint (window, TRUE);
  gtk_window_set_skip_pager_hint (window, TRUE);
  gtk_window_set_keep_above (window, TRUE);

  /* Don't take focus */
  gtk_window_set_accept_focus (window, FALSE);
  gtk_window_set_focus_on_map (window, FALSE);

  /* Make it white */
  gtk_widget_override_background_color (GTK_WIDGET (window), GTK_STATE_NORMAL,
                                        &white);

  /* Don't consume input */
  gtk_widget_realize (GTK_WIDGET (window));
  input_region = cairo_region_create ();
  gdk_window_input_shape_combine_region (gtk_widget_get_window (GTK_WIDGET (window)), input_region, 0, 0);
  cairo_region_destroy (input_region);
}

static void
cheese_flash_class_init (CheeseFlashClass *klass)
{
}

/*
 * cheese_flash_opacity_fade:
 * @data: the #CheeseFlash
 *
 * Fade the flash out.
 *
 * Returns: %TRUE if the fade was completed, %FALSE if the flash must continue
 * to fade
 */
static gboolean
cheese_flash_opacity_fade (gpointer data)
{
  CheeseFlashPrivate *priv;
  GtkWidget *flash_window;

  flash_window = GTK_WIDGET (data);
  priv = cheese_flash_get_instance_private (CHEESE_FLASH (data));

  /* exponentially decrease */
  priv->opacity *= FLASH_FADE_FACTOR;

  if (priv->opacity <= FLASH_LOW_THRESHOLD)
    {
      /* the flasher has finished when we reach the quit value */
      priv->fade_timeout_tag = 0;
      gtk_widget_destroy (flash_window);
      return G_SOURCE_REMOVE;
    }
  else
    {
      gtk_widget_set_opacity (flash_window, priv->opacity);
    }

  return G_SOURCE_CONTINUE;
}

/*
 * cheese_flash_start_fade:
 * @data: the #CheeseFlash
 *
 * Add a timeout to start the fade animation.
 *
 * Returns: %FALSE
 */
static gboolean
cheese_flash_start_fade (gpointer data)
{
  CheeseFlashPrivate *priv = cheese_flash_get_instance_private (CHEESE_FLASH (data));
  GtkWindow *flash_window = GTK_WINDOW (data);

  /* If the screen is non-composited, just destroy and finish up */
  if (!gdk_screen_is_composited (gtk_window_get_screen (flash_window)))
    {
      gtk_widget_destroy (GTK_WIDGET (flash_window));
      return G_SOURCE_REMOVE;
    }

  priv->fade_timeout_tag = g_timeout_add (1000.0 / FLASH_ANIMATION_RATE, cheese_flash_opacity_fade, data);
  priv->flash_timeout_tag = 0;
  return G_SOURCE_REMOVE;
}

/**
 * cheese_flash_fire:
 * @flash: a #CheeseFlash
 * @rect: a #GdkRectangle
 *
 * Fire the flash.
 */
void
cheese_flash_fire (CheeseFlash  *flash,
                   GdkRectangle *rect)
{
  CheeseFlashPrivate *priv;
  GtkWindow *flash_window;

  g_return_if_fail (CHEESE_IS_FLASH (flash));
  g_return_if_fail (rect != NULL);

  priv = cheese_flash_get_instance_private (flash);
  flash_window = GTK_WINDOW (flash);

  if (priv->flash_timeout_tag > 0)
    {
      g_source_remove (priv->flash_timeout_tag);
      priv->flash_timeout_tag = 0;
    }

  if (priv->fade_timeout_tag > 0)
    {
      g_source_remove (priv->fade_timeout_tag);
      priv->fade_timeout_tag = 0;
    }

  priv->opacity = 1.0;

  gtk_window_resize (flash_window, rect->width, rect->height);
  gtk_window_move (flash_window, rect->x, rect->y);

  gtk_widget_set_opacity (GTK_WIDGET (flash_window), 1);
  gtk_widget_show_all (GTK_WIDGET (flash_window));
  priv->flash_timeout_tag = g_timeout_add (FLASH_DURATION, cheese_flash_start_fade, (gpointer) flash);
}

/**
 * cheese_flash_new:
 *
 * Create a new #CheeseFlash.
 *
 * Returns: a new #CheeseFlash
 */
CheeseFlash *
cheese_flash_new (void)
{
  return g_object_new (CHEESE_TYPE_FLASH,
		       "type", GTK_WINDOW_POPUP,
                       NULL);
}

#endif
