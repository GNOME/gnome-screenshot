/* screenshot-filename-builder.c - Builds a filename suitable for a screenshot
 *
 * Copyright (C) 2008, 2011 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include <config.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <pwd.h>
#include <string.h>

#include "screenshot-filename-builder.h"
#include "screenshot-config.h"

typedef enum
{
  TEST_SAVED_DIR = 0,
  TEST_DEFAULT,
  TEST_FALLBACK,
  NUM_TESTS
} TestType;

typedef struct 
{
  char *base_paths[NUM_TESTS];
  char *screenshot_origin;
  int iteration;
  TestType type;
} AsyncExistenceJob;

/* Taken from gnome-vfs-utils.c */
static char *
expand_initial_tilde (const char *path)
{
  char *slash_after_user_name, *user_name;
  struct passwd *passwd_file_entry;

  if (path[1] == '/' || path[1] == '\0') {
    return g_build_filename (g_get_home_dir (), &path[1], NULL);
  }
  
  slash_after_user_name = strchr (&path[1], '/');
  if (slash_after_user_name == NULL) {
    user_name = g_strdup (&path[1]);
  } else {
    user_name = g_strndup (&path[1],
                           slash_after_user_name - &path[1]);
  }
  passwd_file_entry = getpwnam (user_name);
  g_free (user_name);
  
  if (passwd_file_entry == NULL || passwd_file_entry->pw_dir == NULL) {
    return g_strdup (path);
  }
  
  return g_strconcat (passwd_file_entry->pw_dir,
                      slash_after_user_name,
                      NULL);
}

static gchar *
get_fallback_screenshot_dir (void)
{
  return g_strdup (g_get_home_dir ());
}

static gchar *
get_default_screenshot_dir (void)
{
  return g_strdup (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES));
}

static gchar *
sanitize_save_directory (const gchar *save_dir)
{
  gchar *retval = g_strdup (save_dir);

  if (save_dir == NULL)
    return NULL;

  if (save_dir[0] == '~')
    {
      char *tmp = expand_initial_tilde (save_dir);
      g_free (retval);
      retval = tmp;
    }
  else if (strstr (save_dir, "://") != NULL)
    {
      GFile *file;

      g_free (retval);
      file = g_file_new_for_uri (save_dir);
      retval = g_file_get_path (file);
      g_object_unref (file);
    }

  return retval;
}

static char *
build_path (AsyncExistenceJob *job)
{
  const gchar *base_path, *file_type;
  char *retval, *file_name, *origin;

  base_path = job->base_paths[job->type];
  file_type = screenshot_config->file_type;

  if (base_path == NULL ||
      base_path[0] == '\0')
    return NULL;

  if (job->screenshot_origin == NULL)
    {
      GDateTime *d;

      d = g_date_time_new_now_local ();
      origin = g_date_time_format (d, "%Y-%m-%d %H-%M-%S");
      g_date_time_unref (d);
    }
  else
    origin = g_strdup (job->screenshot_origin);

  if (job->iteration == 0)
    {
      /* translators: this is the name of the file that gets made up
       * with the screenshot if the entire screen is taken */
      file_name = g_strdup_printf (_("Screenshot from %s.%s"), origin, file_type);
    }
  else
    {
      /* translators: this is the name of the file that gets
       * made up with the screenshot if the entire screen is
       * taken */
      file_name = g_strdup_printf (_("Screenshot from %s - %d.%s"), origin, job->iteration, file_type);
    }

  retval = g_build_filename (base_path, file_name, NULL);
  g_free (file_name);
  g_free (origin);

  return retval;
}

static void
async_existence_job_free (AsyncExistenceJob *job)
{
  gint idx;

  for (idx = 0; idx < NUM_TESTS; idx++)
    g_free (job->base_paths[idx]);

  g_free (job->screenshot_origin);

  g_slice_free (AsyncExistenceJob, job);
}

static gboolean
prepare_next_cycle (AsyncExistenceJob *job)
{
  gboolean res = FALSE;

  if (job->type != (NUM_TESTS - 1))
    {
      (job->type)++;
      job->iteration = 0;

      res = TRUE;
    }

  return res;
}

static void
try_check_file (GTask *task,
                gpointer source_object,
                gpointer data,
                GCancellable *cancellable)
{
  AsyncExistenceJob *job = data;
  GFile *file;
  GFileInfo *info;
  GError *error;
  char *path, *retval;

retry:
  error = NULL;
  path = build_path (job);

  if (path == NULL)
    {
      (job->type)++;
      goto retry;
    }

  file = g_file_new_for_path (path);
  info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
			    G_FILE_QUERY_INFO_NONE, cancellable, &error);
  if (info != NULL)
    {
      /* file already exists, iterate again */
      g_object_unref (info);
      g_object_unref (file);
      g_free (path);

      (job->iteration)++;

      goto retry;
    }
  else
    {
      /* see the error to check whether the location is not accessible
       * or the file does not exist.
       */
      if (error->code == G_IO_ERROR_NOT_FOUND)
        {
          GFile *parent;

          /* if the parent directory doesn't exist as well, forget the saved
           * directory and treat this as a generic error.
           */

          parent = g_file_get_parent (file);

          if (!g_file_query_exists (parent, NULL))
            {
              if (!prepare_next_cycle (job))
                {
                  retval = NULL;

                  g_object_unref (parent);
                  goto out;
                }

              g_object_unref (file);
              g_object_unref (parent);
              goto retry;
            }
          else
            {
              retval = path;

              g_object_unref (parent);
              goto out;
            }
        }
      else
        {
          /* another kind of error, assume this location is not
           * accessible.
           */
          g_free (path);

          if (prepare_next_cycle (job))
            {
              g_error_free (error);
              g_object_unref (file);
              goto retry;
            }
          else
            {
              retval = NULL;
              goto out;
            }
        }
    }

out:
  g_error_free (error);
  g_object_unref (file);

  if (retval == NULL)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_FAILED,
                             "%s", "Failed to find a valid place to save");

  g_task_return_pointer (task, retval, NULL);
}

void
screenshot_build_filename_async (const char *save_dir,
				 const char *screenshot_origin,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
  AsyncExistenceJob *job;
  GTask *task;

  job = g_slice_new0 (AsyncExistenceJob);

  job->base_paths[TEST_SAVED_DIR] = sanitize_save_directory (save_dir);
  job->base_paths[TEST_DEFAULT] = get_default_screenshot_dir ();
  job->base_paths[TEST_FALLBACK] = get_fallback_screenshot_dir ();
  job->iteration = 0;
  job->type = TEST_SAVED_DIR;

  job->screenshot_origin = g_strdup (screenshot_origin);

  task = g_task_new (NULL, NULL, callback, user_data);
  g_task_set_task_data (task, job, (GDestroyNotify) async_existence_job_free);

  g_task_run_in_thread (task, try_check_file);
  g_object_unref (task);
}

gchar *
screenshot_build_filename_finish (GAsyncResult *result,
                                  GError **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}
