#include "world-store.h"
#include "vendor/tomlc17/tomlc17.h"

#include <glib/gstdio.h>
#include <string.h>

#define WORLD_ATTRIBUTES "standard::name,standard::type,standard::size,standard::is-symlink,time::modified"
#define MAX_WORLD_DEPTH 128
#define MAX_CONFIG_BYTES (2 * 1024 * 1024)

void
pumpkin_world_info_free(gpointer data)
{
  PumpkinWorldInfo *world = data;
  g_free(world->name);
  g_free(world->path);
  g_free(world);
}

static gboolean
within(const char *path, const char *base)
{
  g_autofree char *prefix = g_strconcat(base, G_DIR_SEPARATOR_S, NULL);
  return g_str_has_prefix(path, prefix);
}

static gboolean
managed_path(const char *data_dir, const char *path)
{
  g_autofree char *base = g_canonicalize_filename(data_dir, NULL);
  g_autofree char *canonical = g_canonicalize_filename(path, NULL);
  if (!within(canonical, base)) return FALSE;
  g_autofree char *cursor = g_strdup(canonical);
  for (;;) {
    g_autoptr(GFile) file = g_file_new_for_path(cursor);
    g_autoptr(GFileInfo) info = g_file_query_info(file, "standard::is-symlink,standard::type",
      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
    if (info == NULL || g_file_info_get_is_symlink(info) ||
        g_file_info_get_file_type(info) != G_FILE_TYPE_DIRECTORY) return FALSE;
    if (g_strcmp0(cursor, base) == 0) return TRUE;
    char *parent = g_path_get_dirname(cursor);
    g_free(cursor);
    cursor = parent;
  }
}

static char *
configured_world_path(const char *data_dir, GCancellable *cancellable, GError **error)
{
  /* Current Pumpkin loads from its working directory; older releases use config/. */
  const char *names[] = { "pumpkin.toml", "config" G_DIR_SEPARATOR_S "configuration.toml" };
  for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
    g_autofree char *config = g_build_filename(data_dir, names[i], NULL);
    if (!g_file_test(config, G_FILE_TEST_EXISTS)) continue;
    g_autoptr(GFile) config_file = g_file_new_for_path(config);
    g_autoptr(GFileInfo) info = g_file_query_info(config_file, "standard::type,standard::size",
      G_FILE_QUERY_INFO_NONE, cancellable, error);
    if (info == NULL) return NULL;
    if (g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR ||
        g_file_info_get_size(info) > MAX_CONFIG_BYTES) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid or oversized world configuration");
      return NULL;
    }
    g_autoptr(GFileInputStream) stream = g_file_read(config_file, cancellable, error);
    if (stream == NULL) return NULL;
    g_autofree char *contents = g_malloc(MAX_CONFIG_BYTES + 2);
    gsize length = 0;
    if (!g_input_stream_read_all(G_INPUT_STREAM(stream), contents, MAX_CONFIG_BYTES + 1,
                                 &length, cancellable, error)) return NULL;
    contents[length] = '\0';
    if (length > MAX_CONFIG_BYTES || memchr(contents, '\0', length) != NULL ||
        !g_utf8_validate(contents, (gssize)length, NULL)) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid or oversized world configuration");
      return NULL;
    }
    toml_result_t parsed = toml_parse(contents, (int)length);
    if (!parsed.ok) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                  "Cannot determine the configured world: %s", parsed.errmsg);
      toml_free(parsed);
      return NULL;
    }
    toml_datum_t name = toml_get(parsed.toptab, "default_level_name");
    if (name.type != TOML_UNKNOWN &&
        (name.type != TOML_STRING || name.u.str.len == 0 ||
         memchr(name.u.s, '\0', (gsize)name.u.str.len) != NULL)) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                          "default_level_name must be a non-empty string");
      toml_free(parsed);
      return NULL;
    }
    char *path = g_canonicalize_filename(name.type == TOML_STRING ? name.u.s : "world", data_dir);
    toml_free(parsed);
    return path;
  }
  return g_canonicalize_filename("world", data_dir);
}

static gboolean
recognized_world(const char *path)
{
  g_autofree char *level = g_build_filename(path, "level.dat", NULL);
  return !g_file_test(level, G_FILE_TEST_IS_SYMLINK) && g_file_test(level, G_FILE_TEST_IS_REGULAR);
}

static gboolean
measure_world(GFile *directory, guint depth, guint64 *size, gint64 *modified, GCancellable *cancellable)
{
  if (depth > MAX_WORLD_DEPTH || (cancellable != NULL && g_cancellable_is_cancelled(cancellable))) return FALSE;
  g_autoptr(GFileEnumerator) entries = g_file_enumerate_children(directory, WORLD_ATTRIBUTES,
    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, NULL);
  if (entries == NULL) return FALSE;
  gboolean complete = TRUE;
  for (;;) {
    g_autoptr(GError) error = NULL;
    g_autoptr(GFileInfo) info = g_file_enumerator_next_file(entries, cancellable, &error);
    if (error != NULL) return FALSE;
    if (info == NULL) break;
    if (g_file_info_get_is_symlink(info)) continue;
    guint64 changed = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    if (changed <= G_MAXINT64) *modified = MAX(*modified, (gint64)changed);
    GFileType type = g_file_info_get_file_type(info);
    if (type == G_FILE_TYPE_DIRECTORY) {
      g_autoptr(GFile) child = g_file_get_child(directory, g_file_info_get_name(info));
      if (!measure_world(child, depth + 1, size, modified, cancellable)) complete = FALSE;
    } else if (type == G_FILE_TYPE_REGULAR) {
      goffset bytes = g_file_info_get_size(info);
      if (bytes < 0 || (guint64)bytes > G_MAXUINT64 - *size) return FALSE;
      *size += (guint64)bytes;
    }
  }
  return complete;
}

static void
add_world(GPtrArray *worlds, const char *data_dir, const char *path,
          const char *configured, GCancellable *cancellable)
{
  for (guint i = 0; i < worlds->len; i++) {
    PumpkinWorldInfo *existing = g_ptr_array_index(worlds, i);
    if (g_strcmp0(existing->path, path) == 0) return;
  }
  g_autoptr(GFile) file = g_file_new_for_path(path);
  g_autoptr(GFileInfo) info = g_file_query_info(file, WORLD_ATTRIBUTES,
    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, NULL);
  if (info == NULL) return;
  gboolean directory = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
  gboolean configured_link = g_strcmp0(path, configured) == 0 &&
    g_file_info_get_is_symlink(info) && g_file_test(path, G_FILE_TEST_IS_DIR);
  if (!directory && !configured_link) return;
  PumpkinWorldInfo *world = g_new0(PumpkinWorldInfo, 1);
  world->name = g_filename_display_basename(path);
  world->path = g_strdup(path);
  world->configured = g_strcmp0(path, configured) == 0;
  world->recognized = recognized_world(path);
  world->managed = managed_path(data_dir, path);
  guint64 changed = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
  world->modified = changed <= G_MAXINT64 ? (gint64)changed : 0;
  if (world->managed) world->size_known = measure_world(file, 0, &world->size, &world->modified, cancellable);
  g_ptr_array_add(worlds, world);
}

static gint
compare_worlds(gconstpointer first, gconstpointer second)
{
  const PumpkinWorldInfo *a = *(PumpkinWorldInfo * const *)first;
  const PumpkinWorldInfo *b = *(PumpkinWorldInfo * const *)second;
  if (a->configured != b->configured) return a->configured ? -1 : 1;
  return g_utf8_collate(a->name, b->name);
}

GPtrArray *
pumpkin_world_inventory_load(const char *data_dir, GCancellable *cancellable, GError **error)
{
  g_autofree char *base = g_canonicalize_filename(data_dir, NULL);
  g_autofree char *configured = configured_world_path(base, cancellable, error);
  if (configured == NULL) return NULL;
  g_autoptr(GPtrArray) worlds = g_ptr_array_new_with_free_func(pumpkin_world_info_free);
  add_world(worlds, base, configured, configured, cancellable);
  if (g_file_test(base, G_FILE_TEST_IS_DIR)) {
    g_autoptr(GDir) dir = g_dir_open(base, 0, error);
    if (dir == NULL) return NULL;
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
      if (name[0] == '.') continue;
      g_autofree char *path = g_build_filename(base, name, NULL);
      if (recognized_world(path)) add_world(worlds, base, path, configured, cancellable);
    }
  }
  g_autofree char *storage = g_build_filename(base, "worlds", NULL);
  if (g_file_test(storage, G_FILE_TEST_IS_DIR)) {
    if (!managed_path(base, storage)) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                          "The world storage directory must not be a symbolic link");
      return NULL;
    }
    g_autoptr(GDir) dir = g_dir_open(storage, 0, error);
    if (dir == NULL) return NULL;
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
      if (name[0] == '.') continue;
      g_autofree char *path = g_build_filename(storage, name, NULL);
      add_world(worlds, base, path, configured, cancellable);
    }
  }
  if (cancellable != NULL && g_cancellable_set_error_if_cancelled(cancellable, error)) return NULL;
  g_ptr_array_sort(worlds, compare_worlds);
  return g_steal_pointer(&worlds);
}

typedef struct { char *data_dir; char *path; } WorldTask;

static void
world_task_free(gpointer data)
{
  WorldTask *task = data;
  g_free(task->data_dir);
  g_free(task->path);
  g_free(task);
}

static void
start_world_task(const char *data_dir, const char *path, GCancellable *cancellable,
                 GAsyncReadyCallback callback, gpointer data, GTaskThreadFunc worker, gpointer tag)
{
  g_autoptr(GTask) task = g_task_new(NULL, cancellable, callback, data);
  g_task_set_source_tag(task, tag);
  WorldTask *parameters = g_new0(WorldTask, 1);
  parameters->data_dir = g_strdup(data_dir);
  parameters->path = g_strdup(path);
  g_task_set_task_data(task, parameters, world_task_free);
  g_task_run_in_thread(task, worker);
}

static void
inventory_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
  (void)source;
  WorldTask *parameters = data;
  g_autoptr(GError) error = NULL;
  GPtrArray *worlds = pumpkin_world_inventory_load(parameters->data_dir, cancellable, &error);
  if (worlds == NULL) g_task_return_error(task, g_steal_pointer(&error));
  else g_task_return_pointer(task, worlds, (GDestroyNotify)g_ptr_array_unref);
}

void
pumpkin_world_inventory_async(const char *data_dir, GCancellable *cancellable,
                              GAsyncReadyCallback callback, gpointer data)
{
  start_world_task(data_dir, NULL, cancellable, callback, data, inventory_worker, pumpkin_world_inventory_async);
}

GPtrArray *
pumpkin_world_inventory_finish(GAsyncResult *result, GError **error)
{
  g_return_val_if_fail(g_async_result_is_tagged(result, pumpkin_world_inventory_async), NULL);
  return g_task_propagate_pointer(G_TASK(result), error);
}

static gboolean
remove_folder(GFile *file, guint depth, GError **error)
{
  g_autoptr(GFileInfo) info = g_file_query_info(file, WORLD_ATTRIBUTES,
    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
  if (info == NULL) return FALSE;
  if (!g_file_info_get_is_symlink(info) && g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY) {
    if (depth > MAX_WORLD_DEPTH) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "World folder nesting is too deep");
      return FALSE;
    }
    g_autoptr(GFileEnumerator) entries = g_file_enumerate_children(file, WORLD_ATTRIBUTES,
      G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
    if (entries == NULL) return FALSE;
    for (;;) {
      g_autoptr(GFileInfo) child_info = g_file_enumerator_next_file(entries, NULL, error);
      if (child_info == NULL) { if (error != NULL && *error != NULL) return FALSE; break; }
      g_autoptr(GFile) child = g_file_get_child(file, g_file_info_get_name(child_info));
      if (!remove_folder(child, depth + 1, error)) return FALSE;
    }
  }
  return g_file_delete(file, NULL, error);
}

static void
remove_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancellable)
{
  (void)source;
  (void)cancellable;
  WorldTask *parameters = data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) worlds = pumpkin_world_inventory_load(parameters->data_dir, NULL, &error);
  gboolean allowed = FALSE;
  if (worlds != NULL) {
    for (guint i = 0; i < worlds->len; i++) {
      const PumpkinWorldInfo *world = g_ptr_array_index(worlds, i);
      if (g_strcmp0(world->path, parameters->path) == 0 && world->managed && world->recognized) allowed = TRUE;
    }
  }
  if (!allowed) {
    if (error == NULL) g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
      "Only recognized world folders inside this server's data directory can be removed");
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }
  g_autoptr(GFile) file = g_file_new_for_path(parameters->path);
  if (!remove_folder(file, 0, &error)) g_task_return_error(task, g_steal_pointer(&error));
  else g_task_return_boolean(task, TRUE);
}

void
pumpkin_world_remove_async(const char *data_dir, const char *path,
                           GAsyncReadyCallback callback, gpointer data)
{
  start_world_task(data_dir, path, NULL, callback, data, remove_worker, pumpkin_world_remove_async);
}

gboolean
pumpkin_world_remove_finish(GAsyncResult *result, GError **error)
{
  g_return_val_if_fail(g_async_result_is_tagged(result, pumpkin_world_remove_async), FALSE);
  return g_task_propagate_boolean(G_TASK(result), error);
}

static gboolean
copy_folder(GFile *source, GFile *destination, guint depth, GError **error)
{
  if (depth > MAX_WORLD_DEPTH) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "World folder nesting is too deep");
    return FALSE;
  }
  if (!g_file_make_directory(destination, NULL, error)) return FALSE;
  g_autoptr(GFileEnumerator) entries = g_file_enumerate_children(source, WORLD_ATTRIBUTES,
    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
  if (entries == NULL) return FALSE;
  for (;;) {
    g_autoptr(GFileInfo) info = g_file_enumerator_next_file(entries, NULL, error);
    if (info == NULL) return error == NULL || *error == NULL;
    g_autoptr(GFile) src = g_file_get_child(source, g_file_info_get_name(info));
    g_autoptr(GFile) dst = g_file_get_child(destination, g_file_info_get_name(info));
    GFileType type = g_file_info_get_file_type(info);
    if (g_file_info_get_is_symlink(info) || (type != G_FILE_TYPE_DIRECTORY && type != G_FILE_TYPE_REGULAR)) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                          "World import does not accept symbolic links or special files");
      return FALSE;
    }
    if (type == G_FILE_TYPE_DIRECTORY) {
      if (!copy_folder(src, dst, depth + 1, error)) return FALSE;
    } else if (!g_file_copy(src, dst, G_FILE_COPY_NOFOLLOW_SYMLINKS, NULL, NULL, NULL, error)) return FALSE;
  }
}

static void
import_worker(GTask *task, gpointer source_object, gpointer data, GCancellable *cancellable)
{
  (void)source_object;
  (void)cancellable;
  WorldTask *parameters = data;
  g_autoptr(GError) error = NULL;
  g_autofree char *base = g_canonicalize_filename(parameters->data_dir, NULL);
  g_autofree char *source_path = g_canonicalize_filename(parameters->path, NULL);
  if (!recognized_world(source_path) || g_file_test(source_path, G_FILE_TEST_IS_SYMLINK) ||
      g_strcmp0(base, source_path) == 0 || within(base, source_path)) {
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "Choose a world folder containing level.dat, not a server or storage folder");
    return;
  }
  g_autofree char *name = g_path_get_basename(source_path);
  if (name[0] == '.') {
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Choose a world folder with a non-hidden name");
    return;
  }
  g_autofree char *storage = g_build_filename(base, "worlds", NULL);
  if (g_strcmp0(storage, source_path) == 0 || within(storage, source_path)) {
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "The import destination cannot be inside the source world");
    return;
  }
  g_autoptr(GFile) base_file = g_file_new_for_path(base);
  g_autoptr(GFileInfo) base_info = g_file_query_info(base_file, "standard::type,standard::is-symlink",
    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
  if (base_info == NULL || g_file_info_get_is_symlink(base_info) ||
      g_file_info_get_file_type(base_info) != G_FILE_TYPE_DIRECTORY ||
      g_mkdir_with_parents(storage, 0700) != 0 || !managed_path(base, storage)) {
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "World storage is not a safe local directory");
    return;
  }
  g_autofree char *uuid = g_uuid_string_random();
  g_autofree char *staging_name = g_strconcat(".world-import-", uuid, NULL);
  g_autofree char *staging_path = g_build_filename(storage, staging_name, NULL);
  g_autoptr(GFile) source = g_file_new_for_path(source_path);
  g_autoptr(GFile) staging = g_file_new_for_path(staging_path);
  gboolean copied = copy_folder(source, staging, 0, &error);
  g_autofree char *target_path = g_build_filename(storage, name, NULL);
  if (g_file_test(target_path, G_FILE_TEST_EXISTS)) {
    g_autofree char *unique = g_strdup_printf("%s-%s", name, uuid);
    g_free(target_path);
    target_path = g_build_filename(storage, unique, NULL);
  }
  g_autoptr(GFile) target = g_file_new_for_path(target_path);
  if (!copied || !g_file_move(staging, target, G_FILE_COPY_NONE, NULL, NULL, NULL, &error)) {
    remove_folder(staging, 0, NULL);
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }
  g_task_return_pointer(task, g_steal_pointer(&target_path), g_free);
}

void
pumpkin_world_import_async(const char *data_dir, const char *source,
                           GAsyncReadyCallback callback, gpointer data)
{
  start_world_task(data_dir, source, NULL, callback, data, import_worker, pumpkin_world_import_async);
}

char *
pumpkin_world_import_finish(GAsyncResult *result, GError **error)
{
  g_return_val_if_fail(g_async_result_is_tagged(result, pumpkin_world_import_async), NULL);
  return g_task_propagate_pointer(G_TASK(result), error);
}
