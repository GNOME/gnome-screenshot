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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <pwd.h>
#include <string.h>

typedef enum
{
  TEST_LAST_DIR = 0,
  TEST_DESKTOP = 1,
  TEST_TMP = 2,
} TestType;

typedef struct 
{
  char *base_uris[3];
  int iteration;
  TestType type;

  GSimpleAsyncResult *async_result;
} AsyncExistenceJob;

/* Taken from gnome-vfs-utils.c */
static char *
expand_initial_tilde (const char *path)
{
  char *slash_after_user_name, *user_name;
  struct passwd *passwd_file_entry;

  if (path[1] == '/' || path[1] == '\0') {
    return g_strconcat (g_get_home_dir (), &path[1], NULL);
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
get_default_screenshot_dir (void)
{
  gchar *shot_dir;

  shot_dir = g_strconcat ("file://", g_get_user_special_dir (G_USER_DIRECTORY_PICTURES), NULL);

  return shot_dir;
}

static gchar *
sanitize_save_directory (const gchar *save_dir)
{
  gchar *retval = g_strdup (save_dir);

  if (save_dir[0] == '~')
    {
      char *tmp = expand_initial_tilde (save_dir);
      g_free (retval);
      retval = tmp;
    }

  return retval;
}

static char *
build_uri (AsyncExistenceJob *job)
{
  const gchar *base_uri;
  char *retval, *file_name;
  char *timestamp;
  GDateTime *d;

  base_uri = job->base_uris[job->type];

  if (base_uri == NULL ||
      base_uri[0] == '\0')
    return NULL;

  d = g_date_time_new_now_local ();
  timestamp = g_date_time_format (d, "%Y-%m-%d %H:%M:%S");
  g_date_time_unref (d);

  if (job->iteration == 0)
    {
      /* translators: this is the name of the file that gets made up
       * with the screenshot if the entire screen is taken */
      file_name = g_strdup_printf (_("Screenshot at %s.png"), timestamp);
    }
  else
    {
      /* translators: this is the name of the file that gets
       * made up with the screenshot if the entire screen is
       * taken */
      file_name = g_strdup_printf (_("Screenshot at %s - %d.png"), timestamp, job->iteration);
    }

  retval = g_build_filename (base_uri, file_name, NULL);
  g_free (file_name);
  g_free (timestamp);

  return retval;
}

static void
async_existence_job_free (AsyncExistenceJob *job)
{
  gint idx;

  for (idx = 0; idx < 3; idx++)
    g_free (job->base_uris[idx]);

  g_clear_object (&job->async_result);

  g_slice_free (AsyncExistenceJob, job);
}

static gboolean
try_check_file (GIOSchedulerJob *io_job,
                GCancellable *cancellable,
                gpointer data)
{
  AsyncExistenceJob *job = data;
  GFile *file;
  GFileInfo *info;
  GError *error;
  char *uri, *retval;

retry:
  error = NULL;
  uri = build_uri (job);

  if (uri == NULL)
    {
      (job->type)++;
      goto retry;
    }

  file = g_file_new_for_uri (uri);
  info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
			    G_FILE_QUERY_INFO_NONE, cancellable, &error);
  if (info != NULL)
    {
      /* file already exists, iterate again */
      g_object_unref (info);
      g_object_unref (file);
      g_free (uri);

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
              (job->type)++;
              job->iteration = 0;

              g_object_unref (file);
              g_object_unref (parent);
              goto retry;
            }
          else
            {
              retval = uri;

              g_object_unref (parent);
              goto out;
            }
        }
      else
        {
          /* another kind of error, assume this location is not
           * accessible.
           */
          g_free (uri);
          if (job->type == TEST_TMP)
            {
              retval = NULL;
              goto out;
            }
          else
            {
              (job->type)++;
              job->iteration = 0;

              g_error_free (error);
              g_object_unref (file);
              goto retry;
            }
        }
    }

out:
  g_error_free (error);
  g_object_unref (file);

  g_simple_async_result_set_op_res_gpointer (job->async_result,
                                             retval, NULL);
  if (retval == NULL)
    g_simple_async_result_set_error (job->async_result,
                                     G_IO_ERROR,
                                     G_IO_ERROR_FAILED,
                                     "%s", "Failed to find a valid place to save");

  g_simple_async_result_complete_in_idle (job->async_result);
  async_existence_job_free (job);

  return FALSE;
}

void
screenshot_build_filename_async (const gchar *save_dir,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
  AsyncExistenceJob *job;

  job = g_slice_new0 (AsyncExistenceJob);

  job->base_uris[0] = sanitize_save_directory (save_dir);
  job->base_uris[1] = get_default_screenshot_dir ();
  job->base_uris[2] = g_strconcat ("file://", g_get_tmp_dir (), NULL);
  job->iteration = 0;
  job->type = TEST_LAST_DIR;

  job->async_result = g_simple_async_result_new (NULL,
                                                 callback, user_data,
                                                 screenshot_build_filename_async);

  g_io_scheduler_push_job (try_check_file,
                           job, NULL,
                           G_PRIORITY_DEFAULT, NULL);
}

gchar *
screenshot_build_filename_finish (GAsyncResult *result,
                                  GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
    return NULL;

  return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
}
