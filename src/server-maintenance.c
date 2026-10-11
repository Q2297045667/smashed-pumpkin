#include "server-maintenance.h"

#include <glib/gstdio.h>
#include <errno.h>

static gboolean
wait_for_stop(gpointer data)
{
  GTask *task = G_TASK(data);
  PumpkinServer *server = g_task_get_source_object(task);
  if (g_task_return_error_if_cancelled(task)) {
    return G_SOURCE_REMOVE;
  }
  if (!pumpkin_server_get_running(server)) {
    g_task_return_boolean(task, TRUE);
    return G_SOURCE_REMOVE;
  }
  if (pumpkin_server_get_stop_timed_out(server)) {
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
      "Server could not be stopped safely. Wait for it to finish saving or explicitly force stop, then retry.");
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

void
pumpkin_server_stop_async(PumpkinServer *server, GCancellable *cancellable,
                          GAsyncReadyCallback callback, gpointer user_data)
{
  g_return_if_fail(PUMPKIN_IS_SERVER(server));
  GTask *task = g_task_new(server, cancellable, callback, user_data);
  g_task_set_source_tag(task, pumpkin_server_stop_async);
  if (g_task_return_error_if_cancelled(task)) {
    g_object_unref(task);
    return;
  }
  pumpkin_server_stop(server);
  if (!pumpkin_server_get_running(server)) {
    g_task_return_boolean(task, TRUE);
    g_object_unref(task);
    return;
  }
  g_timeout_add_full(G_PRIORITY_DEFAULT, 50, wait_for_stop, task, g_object_unref);
}

gboolean
pumpkin_server_stop_finish(PumpkinServer *server, GAsyncResult *result, GError **error)
{
  g_return_val_if_fail(g_task_is_valid(result, server), FALSE);
  g_return_val_if_fail(g_async_result_is_tagged(result, pumpkin_server_stop_async), FALSE);
  return g_task_propagate_boolean(G_TASK(result), error);
}

typedef struct {
  char *source_path;
  char *destination_path;
} BinaryInstall;

static void
binary_install_free(gpointer data)
{
  BinaryInstall *install = data;
  g_free(install->source_path);
  g_free(install->destination_path);
  g_free(install);
}

static char *
stage_copy(const char *source, const char *destination,
           GCancellable *cancellable, GError **error)
{
  g_autofree char *staging = g_strconcat(destination, ".stage-XXXXXX", NULL);
  int fd = g_mkstemp(staging);
  if (fd < 0) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                "Cannot stage installation: %s", g_strerror(errno));
    return NULL;
  }
  g_close(fd, NULL);
  g_autoptr(GFile) src = g_file_new_for_path(source);
  g_autoptr(GFile) dst = g_file_new_for_path(staging);
  if (!g_file_copy(src, dst, G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS,
                   cancellable, NULL, NULL, error)) {
    g_remove(staging);
    return NULL;
  }
  if (g_file_test(staging, G_FILE_TEST_IS_SYMLINK) ||
      !g_file_test(staging, G_FILE_TEST_IS_REGULAR)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Installation source must be a regular file, not a symbolic link");
    g_remove(staging);
    return NULL;
  }
#ifndef G_OS_WIN32
  if (g_chmod(staging, 0755) != 0) {
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                "Cannot make staged binary executable: %s", g_strerror(errno));
    g_remove(staging);
    return NULL;
  }
#endif
  return g_steal_pointer(&staging);
}

static gboolean
replace_staged_file(const char *stage, const char *destination, GError **error)
{
  if (g_rename(stage, destination) == 0) {
    return TRUE;
  }
  g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
              "Cannot replace installation: %s", g_strerror(errno));
  return FALSE;
}

static void
binary_install_worker(GTask *task, gpointer source_object, gpointer task_data,
                      GCancellable *cancellable)
{
  (void)source_object;
  BinaryInstall *install = task_data;
  g_autoptr(GError) error = NULL;
  g_autofree char *stage = stage_copy(install->source_path, install->destination_path,
                                     cancellable, &error);
  if (stage == NULL) {
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }

  if (g_file_test(install->destination_path, G_FILE_TEST_EXISTS)) {
    g_autofree char *backup = g_strconcat(install->destination_path, ".previous", NULL);
    g_autofree char *backup_stage = stage_copy(install->destination_path, backup,
                                              cancellable, &error);
    if (backup_stage == NULL || !replace_staged_file(backup_stage, backup, &error)) {
      if (backup_stage != NULL) {
        g_remove(backup_stage);
      }
      g_remove(stage);
      g_task_return_error(task, g_steal_pointer(&error));
      return;
    }
  }
  /* Cancellation is honored before commit, never reported after replacement. */
  if (cancellable != NULL && g_cancellable_set_error_if_cancelled(cancellable, &error)) {
    g_remove(stage);
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }
  if (!replace_staged_file(stage, install->destination_path, &error)) {
    g_remove(stage);
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }
  g_task_return_boolean(task, TRUE);
}

void
pumpkin_binary_install_async(const char *source_path, const char *destination_path,
                             GCancellable *cancellable, GAsyncReadyCallback callback,
                             gpointer user_data)
{
  g_return_if_fail(source_path != NULL && destination_path != NULL);
  g_autoptr(GTask) task = g_task_new(NULL, cancellable, callback, user_data);
  g_task_set_source_tag(task, pumpkin_binary_install_async);
  g_task_set_check_cancellable(task, FALSE);
  BinaryInstall *install = g_new0(BinaryInstall, 1);
  install->source_path = g_strdup(source_path);
  install->destination_path = g_strdup(destination_path);
  g_task_set_task_data(task, install, binary_install_free);
  g_task_run_in_thread(task, binary_install_worker);
}

gboolean
pumpkin_binary_install_finish(GAsyncResult *result, GError **error)
{
  g_return_val_if_fail(g_task_is_valid(result, NULL), FALSE);
  g_return_val_if_fail(g_async_result_is_tagged(result, pumpkin_binary_install_async), FALSE);
  return g_task_propagate_boolean(G_TASK(result), error);
}
