/* screenshot-utils.c - common functions for GNOME Screenshot
 *
 * Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu>
 * Copyright (C) 2008 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 */

#include "config.h"

#include "screenshot-utils.h"

#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gsound.h>

#include "screenshot-backend-shell.h"

void
screenshot_play_sound_effect (const gchar *event_id,
                              const gchar *event_desc)
{
  GSoundContext *context;
  GError *error = NULL;

  context = gsound_context_new (NULL, &error);

  if (G_UNLIKELY (error)) {
    g_critical ("Couldn't initialize GSound: %s", error->message);
    g_clear_error (&error);

    return;
  }

  gsound_context_play_simple (context,
                              NULL,
                              &error,
                              GSOUND_ATTR_EVENT_ID, event_id,
                              GSOUND_ATTR_EVENT_DESCRIPTION, event_desc,
                              GSOUND_ATTR_CANBERRA_CACHE_CONTROL, "permanent",
                              NULL);

  if (G_UNLIKELY (error)) {
    g_critical ("Couldn't play sound: %s", error->message);
    g_clear_error (&error);
  }

  g_object_unref (context);
}

GdkPixbuf *
screenshot_get_pixbuf (GdkRectangle *rectangle)
{
  GdkPixbuf *screenshot = NULL;
  g_autoptr (ScreenshotBackend) backend = NULL;

  backend = screenshot_backend_shell_new ();
  screenshot = screenshot_backend_get_pixbuf (backend, rectangle);

  if (!screenshot)
    g_message ("Unable to use GNOME Shell's builtin screenshot interface.");

  return screenshot;
}

gint
screenshot_show_dialog (GtkWindow   *parent,
                        GtkMessageType message_type,
                        GtkButtonsType buttons_type,
                        const gchar *message,
                        const gchar *detail)
{
  GtkWidget *dialog;
  GtkWindowGroup *group;
  gint response;

  g_return_val_if_fail ((parent == NULL) || (GTK_IS_WINDOW (parent)),
                        GTK_RESPONSE_NONE);
  g_return_val_if_fail (message != NULL, GTK_RESPONSE_NONE);

  dialog = gtk_message_dialog_new (parent,
                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                   message_type,
                                   buttons_type,
                                   "%s", message);
  gtk_window_set_title (GTK_WINDOW (dialog), "");

  if (detail)
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                              "%s", detail);

  if (parent)
    {
      group = gtk_window_get_group (parent);
      if (group != NULL)
        gtk_window_group_add_window (group, GTK_WINDOW (dialog));
    }

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return response;
}

void
screenshot_display_help (GtkWindow *parent)
{
  g_autoptr(GError) error = NULL;

  gtk_show_uri_on_window (parent,
                          "help:gnome-help/screen-shot-record",
                          gtk_get_current_event_time (),
                          &error);

  if (error)
    screenshot_show_dialog (parent,
                            GTK_MESSAGE_ERROR,
                            GTK_BUTTONS_OK,
                            _("Error loading the help page"),
                            error->message);
}
