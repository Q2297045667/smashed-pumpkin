#include "world-store.h"
#include "vendor/tomlc17/tomlc17.h"
#include <glib/gstdio.h>
#include <locale.h>
#include <string.h>

typedef struct { char *root; char *data; } Fixture;

static void remove_tree(const char *path)
{
  if (!g_file_test(path, G_FILE_TEST_IS_SYMLINK) && g_file_test(path, G_FILE_TEST_IS_DIR)) {
    g_autoptr(GDir) dir = g_dir_open(path, 0, NULL);
    const char *name;
    while (dir != NULL && (name = g_dir_read_name(dir)) != NULL) {
      g_autofree char *child = g_build_filename(path, name, NULL);
      remove_tree(child);
    }
    g_assert_cmpint(g_rmdir(path), ==, 0);
  } else g_assert_cmpint(g_remove(path), ==, 0);
}

static void setup(Fixture *fixture, gconstpointer data)
{
  (void)data;
  fixture->root = g_dir_make_tmp("smashed-world-tests-XXXXXX", NULL);
  g_assert_nonnull(fixture->root);
  fixture->data = g_build_filename(fixture->root, "data", NULL);
  g_assert_cmpint(g_mkdir(fixture->data, 0700), ==, 0);
}

static void teardown(Fixture *fixture, gconstpointer data)
{
  (void)data;
  remove_tree(fixture->root);
  g_free(fixture->root);
  g_free(fixture->data);
}

static char *make_world(const char *parent, const char *name)
{
  char *path = g_build_filename(parent, name, NULL);
  g_assert_cmpint(g_mkdir_with_parents(path, 0700), ==, 0);
  g_autofree char *level = g_build_filename(path, "level.dat", NULL);
  g_assert_true(g_file_set_contents(level, "world-fixture", -1, NULL));
  return path;
}

static void config(Fixture *fixture, const char *name, const char *contents)
{
  g_autofree char *directory = g_build_filename(fixture->data, "config", NULL);
  g_assert_cmpint(g_mkdir_with_parents(directory, 0700), ==, 0);
  g_autofree char *path = g_build_filename(g_strcmp0(name, "pumpkin.toml") == 0 ? fixture->data : directory, name, NULL);
  g_assert_true(g_file_set_contents(path, contents, -1, NULL));
}

static void test_inventory(Fixture *fixture, gconstpointer data)
{
  (void)data;
  g_autofree char *world = make_world(fixture->data, "world");
  g_autofree char *storage = g_build_filename(fixture->data, "worlds", NULL);
  g_autofree char *stored = make_world(storage, "Stored");
  g_autofree char *hidden = make_world(storage, ".world-import-fixture");
  g_autofree char *unknown = g_build_filename(storage, "Not a world", NULL);
  g_assert_cmpint(g_mkdir(unknown, 0700), ==, 0);
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) worlds = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(worlds->len, ==, 3);
  PumpkinWorldInfo *first = g_ptr_array_index(worlds, 0);
  g_assert_cmpstr(first->path, ==, world);
  g_assert_true(first->configured);
  g_assert_true(first->managed);
  g_assert_true(first->recognized);
  g_assert_true(first->size_known);
  g_assert_cmpuint(first->size, ==, strlen("world-fixture"));
  g_assert_cmpint(first->modified, >, 0);
  config(fixture, "configuration.toml", "default_level_name = 'worlds/Stored'\n");
  g_clear_pointer(&worlds, g_ptr_array_unref);
  worlds = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(worlds->len, ==, 3); /* Configured folder is not duplicated; the old root world remains visible. */
  first = g_ptr_array_index(worlds, 0);
  g_assert_cmpstr(first->path, ==, stored);
  g_assert_true(first->configured);
  config(fixture, "pumpkin.toml", "default_level_name = 'world'\n");
  g_clear_pointer(&worlds, g_ptr_array_unref);
  worlds = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  g_assert_no_error(error);
  first = g_ptr_array_index(worlds, 0);
  g_assert_cmpstr(first->path, ==, world); /* New config takes precedence. */
}

static void test_float_config(Fixture *fixture, gconstpointer data)
{
  const char *locale = data;
  g_autofree char *previous = g_strdup(setlocale(LC_NUMERIC, NULL));
  if (setlocale(LC_NUMERIC, locale) == NULL) {
    g_test_skip("Requested numeric locale is not installed");
    return;
  }
  const char *contents = "tps = 20.0\nnegative = -1.5\nfraction = 0.0625\n"
                         "exponent = 2.5e-2\nsubnormal = 5e-324\n"
                         "default_level_name = 'worlds/Lobby'\n";
  g_autofree char *storage = g_build_filename(fixture->data, "worlds", NULL);
  g_autofree char *world = make_world(storage, "Lobby");
  config(fixture, "pumpkin.toml", contents);
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) worlds = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  toml_result_t parsed = toml_parse(contents, (int)strlen(contents));
  g_assert_cmpstr(setlocale(LC_NUMERIC, NULL), ==, locale);
  g_assert_nonnull(setlocale(LC_NUMERIC, previous));
  g_assert_no_error(error);
  g_assert_cmpuint(worlds->len, ==, 1);
  PumpkinWorldInfo *first = g_ptr_array_index(worlds, 0);
  g_assert_true(first->configured);
  g_assert_cmpstr(first->path, ==, world);
  g_assert_true(parsed.ok);
  g_assert_cmpfloat(toml_get(parsed.toptab, "tps").u.fp64, ==, 20.0);
  g_assert_cmpfloat(toml_get(parsed.toptab, "negative").u.fp64, ==, -1.5);
  g_assert_cmpfloat(toml_get(parsed.toptab, "fraction").u.fp64, ==, 0.0625);
  g_assert_cmpfloat(toml_get(parsed.toptab, "exponent").u.fp64, ==, 0.025);
  g_assert_cmpfloat(toml_get(parsed.toptab, "subnormal").u.fp64, >, 0.0);
  toml_free(parsed);
}

static void test_invalid_config(Fixture *fixture, gconstpointer data)
{
  (void)data;
  const char *invalid[] = { "default_level_name = ", "default_level_name = 42", "default_level_name = ''",
                           "default_level_name = \"world\\u0000other\"" };
  for (guint i = 0; i < G_N_ELEMENTS(invalid); i++) {
    config(fixture, "pumpkin.toml", invalid[i]);
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) worlds = pumpkin_world_inventory_load(fixture->data, NULL, &error);
    g_assert_null(worlds);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  }
  g_autofree char *path = g_build_filename(fixture->data, "pumpkin.toml", NULL);
  const char binary[] = "default_level_name = 'world'\0invalid";
  g_assert_true(g_file_set_contents(path, binary, sizeof(binary) - 1, NULL));
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) worlds = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  g_assert_null(worlds);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error(&error);
  g_autofree char *oversized = g_malloc0(2 * 1024 * 1024 + 1);
  g_assert_true(g_file_set_contents(path, oversized, 2 * 1024 * 1024 + 1, NULL));
  worlds = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  g_assert_null(worlds);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error(&error);
  g_assert_cmpint(g_remove(path), ==, 0);
  g_assert_cmpint(g_mkdir(path, 0700), ==, 0);
  worlds = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  g_assert_null(worlds);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void test_external(Fixture *fixture, gconstpointer data)
{
  (void)data;
  g_autofree char *outside = make_world(fixture->root, "External");
  config(fixture, "pumpkin.toml", "default_level_name = '../External'\n");
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) worlds = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(worlds->len, ==, 1);
  PumpkinWorldInfo *world = g_ptr_array_index(worlds, 0);
  g_assert_cmpstr(world->path, ==, outside);
  g_assert_true(world->configured);
  g_assert_false(world->managed);
  g_assert_false(world->size_known);
  g_autoptr(GCancellable) cancel = g_cancellable_new();
  g_cancellable_cancel(cancel);
  g_clear_pointer(&worlds, g_ptr_array_unref);
  worlds = pumpkin_world_inventory_load(fixture->data, cancel, &error);
  g_assert_null(worlds);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

typedef struct { gboolean done; gboolean removing; char *path; GError *error; gboolean success; } Completion;

static void completed(GObject *source, GAsyncResult *result, gpointer data)
{
  (void)source;
  Completion *completion = data;
  if (completion->removing) completion->success = pumpkin_world_remove_finish(result, &completion->error);
  else {
    completion->path = pumpkin_world_import_finish(result, &completion->error);
    completion->success = completion->path != NULL;
  }
  completion->done = TRUE;
}

static void wait_for(Completion *completion)
{
  gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
  while (!completion->done && g_get_monotonic_time() < deadline) {
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_usleep(1000);
  }
  g_assert_true(completion->done);
}

static void test_import_remove(Fixture *fixture, gconstpointer data)
{
  (void)data;
  g_autofree char *source = make_world(fixture->root, "Imported World");
  Completion imported = {0};
  pumpkin_world_import_async(fixture->data, source, completed, &imported);
  wait_for(&imported);
  g_assert_no_error(imported.error);
  g_assert_true(imported.success);
  g_assert_true(g_file_test(source, G_FILE_TEST_IS_DIR));
  g_autofree char *expected = g_build_filename(fixture->data, "worlds", "Imported World", NULL);
  g_assert_cmpstr(imported.path, ==, expected);
  Completion collision = {0};
  pumpkin_world_import_async(fixture->data, source, completed, &collision);
  wait_for(&collision);
  g_assert_no_error(collision.error);
  g_assert_true(collision.success);
  g_assert_cmpstr(collision.path, !=, imported.path);
  Completion rejected = {.removing = TRUE};
  pumpkin_world_remove_async(fixture->data, source, completed, &rejected);
  wait_for(&rejected);
  g_assert_error(rejected.error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_assert_true(g_file_test(source, G_FILE_TEST_IS_DIR));
  g_clear_error(&rejected.error);
  Completion removed = {.removing = TRUE};
  pumpkin_world_remove_async(fixture->data, imported.path, completed, &removed);
  wait_for(&removed);
  g_assert_no_error(removed.error);
  g_assert_true(removed.success);
  g_assert_false(g_file_test(imported.path, G_FILE_TEST_EXISTS));
  g_assert_true(g_file_test(collision.path, G_FILE_TEST_IS_DIR));
  g_free(imported.path);
  g_free(collision.path);
}

static void test_links(Fixture *fixture, gconstpointer data)
{
  (void)data;
  g_autofree char *source = make_world(fixture->root, "Linked World");
  g_autofree char *outside = make_world(fixture->root, "Outside");
  g_autofree char *link_path = g_build_filename(source, "linked", NULL);
  g_autoptr(GFile) link = g_file_new_for_path(link_path);
  g_autoptr(GError) error = NULL;
  if (!g_file_make_symbolic_link(link, outside, NULL, &error)) {
    g_test_skip("This platform does not permit symbolic links");
    return;
  }
  g_autofree char *configured = g_build_filename(fixture->data, "world", NULL);
  g_autoptr(GFile) configured_link = g_file_new_for_path(configured);
  g_assert_true(g_file_make_symbolic_link(configured_link, outside, NULL, NULL));
  g_autoptr(GPtrArray) linked_inventory = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  g_assert_no_error(error);
  g_assert_cmpuint(linked_inventory->len, ==, 1);
  PumpkinWorldInfo *configured_world = g_ptr_array_index(linked_inventory, 0);
  g_assert_true(configured_world->configured);
  g_assert_true(configured_world->recognized);
  g_assert_false(configured_world->managed);
  g_assert_false(configured_world->size_known);
  Completion linked_remove = {.removing = TRUE};
  pumpkin_world_remove_async(fixture->data, configured, completed, &linked_remove);
  wait_for(&linked_remove);
  g_assert_error(linked_remove.error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_clear_error(&linked_remove.error);
  Completion failed = {0};
  pumpkin_world_import_async(fixture->data, source, completed, &failed);
  wait_for(&failed);
  g_assert_error(failed.error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error(&failed.error);
  g_autofree char *storage = g_build_filename(fixture->data, "worlds", NULL);
  g_autoptr(GDir) dir = g_dir_open(storage, 0, NULL);
  g_assert_nonnull(dir);
  g_assert_null(g_dir_read_name(dir)); /* Failed staging directories are cleaned. */
  g_autofree char *world = make_world(storage, "Safe World");
  g_autofree char *nested_link = g_build_filename(world, "linked", NULL);
  g_autoptr(GFile) nested = g_file_new_for_path(nested_link);
  g_assert_true(g_file_make_symbolic_link(nested, outside, NULL, NULL));
  Completion removed = {.removing = TRUE};
  pumpkin_world_remove_async(fixture->data, world, completed, &removed);
  wait_for(&removed);
  g_assert_no_error(removed.error);
  g_assert_true(removed.success);
  g_autofree char *outside_level = g_build_filename(outside, "level.dat", NULL);
  g_assert_true(g_file_test(outside_level, G_FILE_TEST_IS_REGULAR));
  g_assert_cmpint(g_rmdir(storage), ==, 0);
  g_autoptr(GFile) storage_file = g_file_new_for_path(storage);
  g_assert_true(g_file_make_symbolic_link(storage_file, outside, NULL, NULL));
  g_autoptr(GPtrArray) inventory = pumpkin_world_inventory_load(fixture->data, NULL, &error);
  g_assert_null(inventory);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
}

static void test_unsafe_import(Fixture *fixture, gconstpointer data)
{
  (void)data;
  g_autofree char *root_level = g_build_filename(fixture->data, "level.dat", NULL);
  g_assert_true(g_file_set_contents(root_level, "world-fixture", -1, NULL));
  g_autofree char *storage = make_world(fixture->data, "worlds");
  g_autofree char *hidden = make_world(fixture->root, ".Hidden");
  g_autofree char *unknown = g_build_filename(fixture->root, "Unknown", NULL);
  g_assert_cmpint(g_mkdir(unknown, 0700), ==, 0);
  const char *sources[] = {fixture->data, storage, hidden, unknown};
  for (guint i = 0; i < G_N_ELEMENTS(sources); i++) {
    Completion imported = {0};
    pumpkin_world_import_async(fixture->data, sources[i], completed, &imported);
    wait_for(&imported);
    g_assert_false(imported.success);
    g_assert_error(imported.error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&imported.error);
  }
  Completion removed = {.removing = TRUE};
  pumpkin_world_remove_async(fixture->data, unknown, completed, &removed);
  wait_for(&removed);
  g_assert_error(removed.error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_clear_error(&removed.error);
  g_assert_true(g_file_test(unknown, G_FILE_TEST_IS_DIR));
  g_autoptr(GDir) directory = g_dir_open(storage, 0, NULL);
  g_assert_cmpstr(g_dir_read_name(directory), ==, "level.dat");
  g_assert_null(g_dir_read_name(directory));
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add("/worlds/inventory", Fixture, NULL, setup, test_inventory, teardown);
  g_test_add("/worlds/configuration", Fixture, NULL, setup, test_invalid_config, teardown);
  g_test_add("/worlds/configuration-floats-c", Fixture, "C", setup, test_float_config, teardown);
  g_test_add("/worlds/configuration-floats-german", Fixture, "de_DE.UTF-8", setup, test_float_config, teardown);
  g_test_add("/worlds/external-and-cancel", Fixture, NULL, setup, test_external, teardown);
  g_test_add("/worlds/import-and-remove", Fixture, NULL, setup, test_import_remove, teardown);
  g_test_add("/worlds/symbolic-links", Fixture, NULL, setup, test_links, teardown);
  g_test_add("/worlds/unsafe-import", Fixture, NULL, setup, test_unsafe_import, teardown);
  return g_test_run();
}
