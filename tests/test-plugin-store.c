#include "plugin-store.h"
#include "marketplace-auth.h"
#include "vendor/tomlc17/tomlc17.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static char *directory;

static void
setup(void)
{
  directory = g_dir_make_tmp("smashed-pumpkin-plugins-XXXXXX", NULL);
  g_assert_nonnull(directory);
}

static void
teardown(void)
{
  g_autoptr(GDir) dir = g_dir_open(directory, 0, NULL);
  const char *entry;
  while ((entry = g_dir_read_name(dir)) != NULL) {
    g_autofree char *path = g_build_filename(directory, entry, NULL);
    g_assert_cmpint(g_remove(path), ==, 0);
  }
  g_assert_cmpint(g_rmdir(directory), ==, 0);
  g_clear_pointer(&directory, g_free);
}

static void
write_file(const char *file, const char *contents)
{
  g_autofree char *path = g_build_filename(directory, file, NULL);
  g_assert_true(g_file_set_contents(path, contents, -1, NULL));
}

static char *
read_file(const char *file)
{
  g_autofree char *path = g_build_filename(directory, file, NULL);
  char *contents = NULL;
  g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
  return contents;
}

static PumpkinPluginMetadata
plugin(gint64 id, char *public_id, char *file)
{
  return (PumpkinPluginMetadata){ .id = id, .public_id = public_id, .file = file,
    .name = "Same Name", .author = "alice", .version = "1.2.3", .updated_at = "2026-10-06T12:00:00Z" };
}

static void
test_roundtrip(void)
{
  setup();
  PumpkinPluginMetadata metadata = plugin(7, "aBc12", "name.wasm");
  metadata.name = "Quoted \"Name\"\nNext line";
  g_assert_true(pumpkin_plugin_store_record(directory, &metadata, NULL));
  g_autoptr(PumpkinPluginMetadata) loaded = pumpkin_plugin_store_lookup(directory, "name.wasm");
  g_assert_nonnull(loaded);
  g_assert_cmpstr(loaded->name, ==, metadata.name);
  g_assert_cmpstr(loaded->version, ==, "1.2.3");
  g_assert_cmpint(loaded->id, ==, 7);
  teardown();
}

static void
test_identity(void)
{
  setup();
  PumpkinPluginMetadata a = plugin(1, "first", "a.wasm");
  PumpkinPluginMetadata b = plugin(2, "second", "b.wasm");
  b.author = "bob";
  write_file("a.wasm", "fake WASM");
  write_file("b.wasm", "fake WASM");
  write_file("Same Name.wasm", "manual plugin must not match by name");
  g_assert_true(pumpkin_plugin_store_record(directory, &a, NULL));
  g_assert_true(pumpkin_plugin_store_record(directory, &b, NULL));
  g_autoptr(PumpkinPluginMetadata) found = pumpkin_plugin_store_find(directory, 2, "second", NULL);
  g_assert_nonnull(found);
  g_assert_cmpstr(found->file, ==, "b.wasm");
  g_autoptr(PumpkinPluginMetadata) absent = pumpkin_plugin_store_find(directory, 3, "third", NULL);
  g_assert_null(absent);
  g_autoptr(PumpkinPluginMetadata) wrong_public = pumpkin_plugin_store_find(directory, 2, "wrong", NULL);
  g_assert_null(wrong_public);
  teardown();
}

static void
test_update(void)
{
  setup();
  PumpkinPluginMetadata metadata = plugin(7, "stable", "custom.wasm.deactivated");
  write_file(metadata.file, "fake WASM");
  g_assert_true(pumpkin_plugin_store_record(directory, &metadata, NULL));
  metadata.name = "Renamed Plugin";
  metadata.version = "2.0.0";
  g_assert_true(pumpkin_plugin_store_record(directory, &metadata, NULL));
  g_autoptr(PumpkinPluginMetadata) loaded = pumpkin_plugin_store_find(directory, 7, "stable", NULL);
  g_assert_nonnull(loaded);
  g_assert_cmpstr(loaded->file, ==, "custom.wasm.deactivated");
  g_assert_cmpstr(loaded->name, ==, "Renamed Plugin");
  g_assert_cmpstr(loaded->version, ==, "2.0.0");
  teardown();
}

static void
test_toggle_remove(void)
{
  setup();
  PumpkinPluginMetadata metadata = plugin(7, "stable", "name.wasm");
  g_assert_true(pumpkin_plugin_store_record(directory, &metadata, NULL));
  g_assert_true(pumpkin_plugin_store_rename(directory, "name.wasm", "name.wasm.deactivated", NULL));
  g_autoptr(PumpkinPluginMetadata) loaded = pumpkin_plugin_store_lookup(directory, "name.wasm.deactivated");
  g_assert_nonnull(loaded);
  g_assert_cmpstr(loaded->file, ==, "name.wasm.deactivated");
  g_assert_true(pumpkin_plugin_store_rename(directory, "name.wasm.deactivated", "name.wasm", NULL));
  g_assert_true(pumpkin_plugin_store_remove(directory, "name.wasm", NULL));
  g_autoptr(PumpkinPluginMetadata) removed = pumpkin_plugin_store_lookup(directory, "name.wasm");
  g_assert_null(removed);
  teardown();
}

static void
test_legacy(void)
{
  setup();
  write_file("market-7.wasm", "fake WASM");
  write_file("market-7.wasm.marketplace", "[marketplace]\nid=7\nname=Legacy Plugin\nversion=1.0.0\nauthor=alice\n");
  g_autoptr(PumpkinPluginMetadata) found = pumpkin_plugin_store_find(directory, 7, "stable", NULL);
  g_assert_nonnull(found);
  g_assert_cmpstr(found->name, ==, "Legacy Plugin");
  g_assert_cmpstr(found->version, ==, "1.0.0");
  g_assert_true(pumpkin_plugin_store_rename(directory, "market-7.wasm", "market-7.wasm.deactivated", NULL));
  g_autofree char *lock_path = g_build_filename(directory, ".ppm.lock", NULL);
  g_assert_false(g_file_test(lock_path, G_FILE_TEST_EXISTS));
  PumpkinPluginMetadata metadata = plugin(7, "stable", "market-7.wasm");
  metadata.version = "2.0.0";
  g_assert_true(pumpkin_plugin_store_record(directory, &metadata, NULL));
  g_autoptr(PumpkinPluginMetadata) newer = pumpkin_plugin_store_lookup(directory, "market-7.wasm");
  g_assert_cmpstr(newer->version, ==, "2.0.0");
  teardown();
}

static void
test_duplicate(void)
{
  setup();
  write_file("one.wasm", "fake WASM");
  write_file("two.wasm", "fake WASM");
  write_file("one.wasm.marketplace", "[marketplace]\nid=7\npublic_id=stable\n");
  write_file("two.wasm.marketplace", "[marketplace]\nid=7\npublic_id=stable\n");
  g_autoptr(GError) error = NULL;
  g_autoptr(PumpkinPluginMetadata) found = pumpkin_plugin_store_find(directory, 7, "stable", &error);
  g_assert_null(found);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  teardown();
}

static void
test_invalid_lock(void)
{
  setup();
  const char *invalid[] = { "invalid !!!", "version=2\n[plugins]", "version=1\nplugins=5",
    "version=1\n[plugins.bad]\nfile='../escape.wasm'\n",
    "version=1\nextra=\"\\u0000\"\n[plugins]\n", NULL };
  PumpkinPluginMetadata metadata = plugin(7, "stable", "name.wasm");
  for (int i = 0; invalid[i] != NULL; i++) {
    write_file(".ppm.lock", invalid[i]);
    g_autoptr(GError) error = NULL;
    g_assert_false(pumpkin_plugin_store_record(directory, &metadata, &error));
    g_assert_nonnull(error);
    g_autofree char *unchanged = read_file(".ppm.lock");
    g_assert_cmpstr(unchanged, ==, invalid[i]);
  }
  teardown();
}

static void
test_preserve_unknown(void)
{
  setup();
  write_file("ppm.lock", "version=1\nextra={flag=true, date=2026-10-06, utc=2026-10-06T12:34:56Z, time=12:34:56, float=1.0, list=[1,2]}\n"
    "[plugins.stable]\nid=7\npublic_id='stable'\nfile='name.wasm'\nname='Plugin'\nauthor='alice'\nversion='1.0.0'\ncustom={nested='keep'}\n");
  PumpkinPluginMetadata metadata = plugin(7, "stable", "name.wasm");
  g_assert_true(pumpkin_plugin_store_record(directory, &metadata, NULL));
  g_autofree char *contents = read_file("ppm.lock");
  toml_result_t parsed = toml_parse(contents, (int)strlen(contents));
  g_assert_true(parsed.ok);
  g_assert_cmpstr(toml_seek(parsed.toptab, "plugins.stable.custom.nested").u.s, ==, "keep");
  g_assert_cmpint(toml_seek(parsed.toptab, "extra.date").type, ==, TOML_DATE);
  g_assert_cmpint(toml_seek(parsed.toptab, "extra.utc").type, ==, TOML_DATETIMETZ);
  g_assert_cmpint(toml_seek(parsed.toptab, "extra.list").u.arr.size, ==, 2);
  toml_free(parsed);
  g_autofree char *hidden = g_build_filename(directory, ".ppm.lock", NULL);
  g_assert_false(g_file_test(hidden, G_FILE_TEST_EXISTS));
  teardown();
}

static void
test_filename_collision(void)
{
  setup();
  PumpkinPluginMetadata a = plugin(1, "first", "same.wasm");
  PumpkinPluginMetadata b = plugin(2, "second", "same.wasm");
  g_assert_true(pumpkin_plugin_store_record(directory, &a, NULL));
  g_autoptr(GError) error = NULL;
  g_assert_false(pumpkin_plugin_store_record(directory, &b, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_autoptr(PumpkinPluginMetadata) loaded = pumpkin_plugin_store_lookup(directory, "same.wasm");
  g_assert_cmpint(loaded->id, ==, 1);
  teardown();
}

static void
test_unsafe_filename(void)
{
  setup();
  const char *unsafe[] = { "../escape.wasm", "sub/escape.wasm", "C:\\escape.wasm", ".", "", NULL };
  for (int i = 0; unsafe[i] != NULL; i++) {
    PumpkinPluginMetadata metadata = plugin(7, "stable", (char *)unsafe[i]);
    g_autoptr(GError) error = NULL;
    g_assert_false(pumpkin_plugin_store_record(directory, &metadata, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  }
  teardown();
}

static void
test_auth(void)
{
  g_assert_true(pumpkin_marketplace_auth_token_is_safe("pmk_a_test_key"));
  g_assert_false(pumpkin_marketplace_auth_token_is_safe("key\r\nInjected-Header: value"));
  g_assert_false(pumpkin_marketplace_auth_token_is_safe(""));
  g_assert_false(pumpkin_marketplace_auth_token_is_safe("key with spaces"));
  g_assert_nonnull(pumpkin_marketplace_auth_error(401, "Invalid token"));
  g_assert_nonnull(pumpkin_marketplace_auth_error(403, "Forbidden"));
  g_assert_nonnull(pumpkin_marketplace_auth_error(404, ""));
  g_assert_nonnull(pumpkin_marketplace_auth_error(302, ""));
  g_assert_nonnull(pumpkin_marketplace_auth_error(500, ""));
  g_assert_nonnull(pumpkin_marketplace_auth_error(200, "{}"));
  g_assert_nonnull(pumpkin_marketplace_auth_error(200, "<html>Login</html>"));
  g_assert_nonnull(pumpkin_marketplace_auth_error(200, "{\"token\": null}"));
  g_assert_nonnull(pumpkin_marketplace_auth_error(200, "{\"token\": \"not-a-jwt\"}"));
  g_assert_nonnull(pumpkin_marketplace_auth_error(200, "{\"token\":\"header.payload.signature\"}"));
  g_assert_nonnull(pumpkin_marketplace_auth_error(200, "{garbage \"token\":\"header.payload.signature\",\"user\":{}}"));
  g_assert_nonnull(pumpkin_marketplace_auth_error(200, "{\"token\":\"header.payload.signature\",\"token\":\"bad\",\"user\":{}}"));
  g_assert_null(pumpkin_marketplace_auth_error(200, "{\"token\":\"header.payload.signature\",\"user\":{\"username\":\"test\"}}"));
}

int
main(int argc, char **argv)
{
  if (argc == 2 && strcmp(argv[1], "--check-account-response") == 0) {
    g_autoptr(GString) body = g_string_new(NULL);
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), stdin) != NULL && body->len <= 64 * 1024) {
      g_string_append(body, buffer);
    }
    gboolean confirmed = pumpkin_marketplace_auth_error(200, body->str) == NULL;
    g_print("Account response %s\n", confirmed ? "confirmed" : "rejected");
    return confirmed ? 0 : 1;
  }
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/plugins/roundtrip", test_roundtrip);
  g_test_add_func("/plugins/identity", test_identity);
  g_test_add_func("/plugins/update", test_update);
  g_test_add_func("/plugins/toggle-remove", test_toggle_remove);
  g_test_add_func("/plugins/legacy", test_legacy);
  g_test_add_func("/plugins/duplicate", test_duplicate);
  g_test_add_func("/plugins/invalid-lock", test_invalid_lock);
  g_test_add_func("/plugins/preserve-unknown", test_preserve_unknown);
  g_test_add_func("/plugins/filename-collision", test_filename_collision);
  g_test_add_func("/plugins/unsafe-filename", test_unsafe_filename);
  g_test_add_func("/marketplace/auth", test_auth);
  return g_test_run();
}
