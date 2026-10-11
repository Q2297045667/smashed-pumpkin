#include "minimap-store.h"
#include "minimap-cache.h"
#include "minimap-terrain.h"
#include "minimap-motion.h"
#include "minimap-render.h"
#include "vendor/cjson/cJSON.h"
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

static const char *nonce = "8ea2a883-4761-48d6-8a22-3ca5afc182d0";
static const char *player_id = "05c49c2f-e216-4216-8665-e7cb33009b51";
static const gint64 now = 1791540000000;

static char *document(const char *worlds, const char *players)
{
  return g_strdup_printf("{\"version\":1,\"request\":\"%s\",\"updated_at_ms\":%" G_GINT64_FORMAT
    ",\"worlds\":%s,\"players\":%s}", nonce, now, worlds, players);
}
static char *players(void)
{
  return g_strdup_printf("[{\"id\":\"%s\",\"name\":\"Alex\",\"world\":\"Lobby\","
    "\"dimension\":\"minecraft:overworld\",\"x\":-15.5,\"y\":65,\"z\":32,\"ping\":51}]", player_id);
}
static const char *worlds = "[{\"name\":\"Lobby\",\"dimension\":\"minecraft:overworld\","
  "\"loaded_chunks\":[{\"x\":-1,\"z\":2}],\"border\":{\"x\":0,\"z\":0,\"diameter\":200,"
  "\"transition_known\":true,\"target\":100,\"at_ms\":1791540000000,\"duration_ms\":2000}}]";

static void test_snapshot(void)
{
  g_autofree char *roster = players(), *json = document(worlds, roster);
  g_autoptr(GError) error = NULL;
  g_autoptr(PumpkinMapSnapshot) snapshot = pumpkin_map_snapshot_parse(json, nonce, now, &error);
  g_assert_no_error(error); g_assert_nonnull(snapshot);
  g_assert_cmpuint(snapshot->players->len, ==, 1);
  PumpkinMapPlayer *player = pumpkin_map_find_player(snapshot, player_id);
  PumpkinMapWorld *world = g_ptr_array_index(snapshot->worlds, 0);
  g_assert_cmpstr(player->name, ==, "Alex"); g_assert_cmpuint(player->ping, ==, 51);
  g_assert_false(player->yaw_known);
  g_assert_true(pumpkin_map_player_matches(player, world));
  g_assert_cmpuint(world->chunks->len, ==, 1);
  g_assert_cmpfloat(pumpkin_map_border_size(world, now + 1000), ==, 150);
  g_assert_true(pumpkin_map_snapshot_fresh(snapshot, now + 5000));
  g_assert_false(pumpkin_map_snapshot_fresh(snapshot, now + 5001));
}
static void test_locator_and_yaw(void)
{
  const char *rotstein = "faf39d18-9aae-4294-9289-ba3836ccb271";
  g_assert_cmphex(pumpkin_map_locator_color(rotstein), ==, 0x18d7c5);
  g_assert_cmphex(pumpkin_map_locator_color("FAF39D18-9AAE-4294-9289-BA3836CCB271"), ==, 0x18d7c5);
  g_assert_cmphex(pumpkin_map_locator_color("invalid"), ==, 0x7ebde8);
  g_autofree char *roster = players(), *json = document(worlds, roster);
  cJSON *root = cJSON_Parse(json);
  cJSON *player = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "players"), 0);
  cJSON_AddNumberToObject(player, "yaw", 450);
  char *text = cJSON_PrintUnformatted(root);
  g_autoptr(GError) error = NULL;
  g_autoptr(PumpkinMapSnapshot) snapshot = pumpkin_map_snapshot_parse(text, nonce, now, &error);
  g_assert_no_error(error); g_assert_nonnull(snapshot);
  PumpkinMapPlayer *parsed = pumpkin_map_find_player(snapshot, player_id);
  g_assert_true(parsed->yaw_known); g_assert_cmpfloat(parsed->yaw, ==, 90);
  cJSON_free(text);
  cJSON_SetNumberValue(cJSON_GetObjectItem(player, "yaw"), 1e20);
  text = cJSON_PrintUnformatted(root);
  g_clear_pointer(&snapshot, pumpkin_map_snapshot_free);
  snapshot = pumpkin_map_snapshot_parse(text, nonce, now, &error);
  g_assert_null(snapshot); g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  cJSON_free(text); cJSON_Delete(root);
}
static void reject(const char *json, const char *request, gint64 timestamp)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(PumpkinMapSnapshot) snapshot = pumpkin_map_snapshot_parse(json, request, timestamp, &error);
  g_assert_null(snapshot); g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}
static void test_validation(void)
{
  g_autofree char *roster = players(), *json = document(worlds, roster);
  reject(json, "42f77a85-0a5c-4311-9670-075a10c1f2b8", now);
  reject(json, nonce, now + 5001); reject(json, nonce, now - 1001);
  g_autofree char *trailing = g_strconcat(json, " garbage", NULL);
  reject(trailing, nonce, now);
  g_autofree char *unknown = document("[]", roster);
  reject(unknown, nonce, now);
  cJSON *root = cJSON_Parse(json);
  cJSON_AddBoolToObject(root, "complete", 0);
  char *incomplete = cJSON_PrintUnformatted(root); reject(incomplete, nonce, now);
  cJSON_free(incomplete); cJSON_DeleteItemFromObject(root, "complete");
  cJSON *player = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "players"), 0);
  cJSON_SetNumberValue(cJSON_GetObjectItem(player, "ping"), -1);
  char *negative = cJSON_PrintUnformatted(root); reject(negative, nonce, now);
  cJSON_free(negative); cJSON_Delete(root);
  g_autofree char *nul = document("[{\"name\":\"Lobby\\u0000bad\",\"dimension\":\"minecraft:overworld\"}]", "[]");
  reject(nul, nonce, now);
}
static void test_border(void)
{
  PumpkinMapWorld world = {.diameter = 100, .target = 300, .transition_known = TRUE,
    .transition_at_ms = now, .transition_ms = 2000};
  g_assert_cmpfloat(pumpkin_map_border_size(&world, now - 500), ==, 100);
  g_assert_cmpfloat(pumpkin_map_border_size(&world, now + 500), ==, 150);
  g_assert_cmpfloat(pumpkin_map_border_size(&world, now + 2000), ==, 300);
  g_assert_cmpfloat(pumpkin_map_border_size(&world, now + 9000), ==, 300);
  world.diameter = 300; world.target = 50;
  g_assert_cmpfloat(pumpkin_map_border_size(&world, now + 1000), ==, 175);
  world.diameter = 175; world.target = 400; world.transition_at_ms = now + 1000;
  g_assert_cmpfloat(pumpkin_map_border_size(&world, now + 1000), ==, 175);
  g_assert_cmpfloat(pumpkin_map_border_size(&world, now + 2000), ==, 287.5);
  world.transition_ms = 0;
  g_assert_cmpfloat(pumpkin_map_border_size(&world, now + 1000), ==, 400);
  world.transition_known = FALSE;
  g_assert_cmpfloat(pumpkin_map_border_size(&world, now + 1000), ==, 175);
}
static void remove_tree(const char *path)
{
  if (!g_file_test(path, G_FILE_TEST_IS_SYMLINK) && g_file_test(path, G_FILE_TEST_IS_DIR)) {
    g_autoptr(GDir) directory = g_dir_open(path, 0, NULL);
    const char *name;
    while ((name = g_dir_read_name(directory)) != NULL) {
      g_autofree char *child = g_build_filename(path, name, NULL); remove_tree(child);
    }
    g_rmdir(path);
  } else g_remove(path);
}
static char *region(const char *root, const char *relative, int x, int z, gboolean filled)
{
  g_autofree char *folder = g_build_filename(root, relative, NULL);
  g_assert_cmpint(g_mkdir_with_parents(folder, 0700), ==, 0);
  char *path = g_strdup_printf("%s/r.%d.%d.mca", folder, x, z);
  guint8 contents[12288] = {0};
  if (filled) { contents[2] = 2; contents[3] = 1; }
  /* A location outside the file must not become a generated chunk. */
  contents[6] = 255; contents[7] = 255;
  g_assert_true(g_file_set_contents(path, (const char *)contents, sizeof(contents), NULL));
  return path;
}
static void test_inventory(void)
{
  g_autofree char *root = g_dir_make_tmp("smashed-map-regions-XXXXXX", NULL);
  g_autofree char *overworld = region(root, "Lobby/dimensions/minecraft/overworld/region", -1, 2, TRUE);
  g_autofree char *nether = region(root, "Lobby/DIM-1/region", 0, 0, TRUE);
  g_autofree char *end = region(root, "Lobby/DIM1/region", 0, 0, FALSE);
  g_autofree char *custom = region(root, "BedWars 1/dimensions/myserver/arena/region", 1, -1, TRUE);
  g_autofree char *legacy = region(root, "Lobby/region", 0, 0, TRUE);
  g_autofree char *before = NULL; gsize size = 0;
  g_assert_true(g_file_get_contents(overworld, &before, &size, NULL));
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) inventory = pumpkin_map_scan_regions(root, NULL, &error);
  g_assert_no_error(error); g_assert_cmpuint(inventory->len, ==, 3);
  gboolean found = FALSE;
  for (guint i = 0; i < inventory->len; i++) {
    PumpkinMapWorld *world = g_ptr_array_index(inventory, i);
    g_assert_cmpuint(world->chunks->len, ==, 1); g_assert_cmpuint(world->tiles->len, ==, 1);
    if (g_strcmp0(world->dimension, "minecraft:overworld") == 0) {
      PumpkinMapChunk chunk = g_array_index(world->chunks, PumpkinMapChunk, 0);
      g_assert_cmpint(chunk.x, ==, -32); g_assert_cmpint(chunk.z, ==, 64); found = TRUE;
      PumpkinMapTile *tile = g_ptr_array_index(world->tiles, 0);
      g_assert_cmpuint(cairo_image_surface_get_data(tile->mask)[0], ==, 255);
      g_assert_cmpuint(cairo_image_surface_get_data(tile->mask)[1], ==, 0);
    }
    g_assert_cmpstr(world->dimension, !=, "minecraft:the_end");
  }
  g_assert_true(found);
  g_autofree char *after = NULL; gsize after_size = 0;
  g_assert_true(g_file_get_contents(overworld, &after, &after_size, NULL));
  g_assert_cmpmem(before, size, after, after_size);
  remove_tree(root);
}
static void test_empty_and_cancelled(void)
{
  g_autofree char *root = g_dir_make_tmp("smashed-map-empty-XXXXXX", NULL);
  g_autofree char *empty = region(root, "World/region", 0, 0, FALSE);
  g_autoptr(GPtrArray) inventory = pumpkin_map_scan_regions(root, NULL, NULL);
  g_assert_nonnull(inventory); g_assert_cmpuint(inventory->len, ==, 0);
  g_autoptr(GCancellable) cancel = g_cancellable_new(); g_cancellable_cancel(cancel);
  g_autoptr(GError) error = NULL;
  g_clear_pointer(&inventory, g_ptr_array_unref);
  inventory = pumpkin_map_scan_regions(root, cancel, &error);
  g_assert_null(inventory); g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  remove_tree(root);
}
static void test_bounded_discovery(void)
{
  g_autofree char *root = g_dir_make_tmp("smashed-map-depth-XXXXXX", NULL);
  g_autofree char *deep = region(root, "a/b/c/d/e/f/g/h/i/World/region", 0, 0, TRUE);
  g_autoptr(GPtrArray) inventory = pumpkin_map_scan_regions(root, NULL, NULL);
  g_assert_null(inventory);
  g_autoptr(GError) error = NULL;
  inventory = pumpkin_map_scan_regions(root, NULL, &error);
  g_assert_null(inventory); g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
  remove_tree(root);
}
static void test_ipc(void)
{
  g_autofree char *root = g_dir_make_tmp("smashed-map-ipc-XXXXXX", NULL);
  g_autofree char *folder = g_build_filename(root, "plugins", "data", "smashed-map-bridge", NULL);
  g_assert_cmpint(g_mkdir_with_parents(folder, 0700), ==, 0);
  g_autofree char *json = document(worlds, "[]");
  g_autoptr(GError) error = NULL;
  g_assert_true(pumpkin_map_write_request(root, "{\"version\":1}", &error));
  g_assert_no_error(error);
  g_autofree char *live = g_build_filename(folder, "live.json", NULL);
  g_assert_true(g_file_set_contents(live, json, -1, NULL));
  g_autoptr(PumpkinMapSnapshot) snapshot = pumpkin_map_read_live(root, nonce, now, &error);
  g_assert_no_error(error); g_assert_nonnull(snapshot);
  g_clear_pointer(&snapshot, pumpkin_map_snapshot_free);
  g_assert_true(g_file_set_contents(live, "{\0bad}", 7, NULL));
  snapshot = pumpkin_map_read_live(root, nonce, now, &error);
  g_assert_null(snapshot); g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  remove_tree(root);
}
static void test_stop_request(void)
{
  g_autofree char *root = g_dir_make_tmp("smashed-map-stop-XXXXXX", NULL);
  g_autofree char *folder = g_build_filename(root, "plugins", "data", "smashed-map-bridge", NULL);
  g_assert_cmpint(g_mkdir_with_parents(folder, 0700), ==, 0);
  g_autofree char *path = g_build_filename(folder, "request.json", NULL);
  g_autoptr(GError) error = NULL;
  g_assert_true(pumpkin_map_stop_request(root, "old-session", &error));
  const char *request = "{\"request\":\"new-session\"}";
  g_assert_true(pumpkin_map_write_request(root, request, &error));
  g_assert_no_error(error);
  g_assert_true(pumpkin_map_stop_request(root, "old-session", &error));
  g_autofree char *contents = NULL; gsize size = 0;
  g_assert_true(g_file_get_contents(path, &contents, &size, NULL));
  g_assert_cmpstr(contents, ==, request);
  g_autoptr(GCancellable) cancel = g_cancellable_new();
  g_cancellable_cancel(cancel);
  g_assert_false(pumpkin_map_write_request_cancellable(root, "{}", cancel, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED); g_clear_error(&error);
  g_assert_true(pumpkin_map_stop_request(root, "new-session", &error));
  g_assert_no_error(error);
  g_clear_pointer(&contents, g_free);
  g_assert_true(g_file_get_contents(path, &contents, &size, NULL));
  g_assert_cmpuint(size, ==, 0);
  g_assert_true(pumpkin_map_stop_request(root, "new-session", &error));
  g_assert_no_error(error);
  remove_tree(root);
}
static void test_bridge_enable_disable(void)
{
  const char *sample = g_getenv("SMASHED_BUNDLED_BRIDGE_SAMPLE");
  if (sample == NULL) { g_test_skip("Bundled bridge sample required"); return; }
  g_autofree char *root = g_dir_make_tmp("smashed-map-toggle-XXXXXX", NULL);
  g_autofree char *folder = g_build_filename(root, "plugins", NULL);
  g_assert_cmpint(g_mkdir(folder, 0700), ==, 0);
  g_autofree char *binary = g_build_filename(folder, PUMPKIN_MAP_BRIDGE_FILENAME, NULL);
  g_autofree char *disabled = g_strconcat(binary, ".disabled", NULL);
  g_autofree char *contents = NULL, *name = NULL; gsize size = 0;
  g_assert_true(g_file_get_contents(sample, &contents, &size, NULL));
  g_autoptr(GError) error = NULL;
  gboolean changed = TRUE;
  g_assert_true(pumpkin_map_bridge_set_enabled(root, FALSE, &name, &changed, &error));
  g_assert_no_error(error); g_assert_null(name); g_assert_false(changed);
  g_assert_true(g_file_set_contents(binary, contents, (gssize)size, NULL));
  g_assert_true(pumpkin_map_bridge_set_enabled(root, FALSE, &name, &changed, &error));
  g_assert_no_error(error); g_assert_true(changed); g_assert_cmpstr(name, ==, "map-bridge");
  g_assert_false(g_file_test(binary, G_FILE_TEST_EXISTS));
  g_assert_true(g_file_test(disabled, G_FILE_TEST_IS_REGULAR));
  g_clear_pointer(&name, g_free);
  g_assert_true(pumpkin_map_bridge_set_enabled(root, FALSE, &name, &changed, &error));
  g_assert_no_error(error); g_assert_false(changed); g_clear_pointer(&name, g_free);
  g_assert_true(pumpkin_map_bridge_set_enabled(root, TRUE, &name, &changed, &error));
  g_assert_no_error(error); g_assert_true(changed); g_clear_pointer(&name, g_free);
  g_assert_false(g_file_test(disabled, G_FILE_TEST_EXISTS));
  g_autofree char *restored = NULL; gsize restored_size = 0;
  g_assert_true(g_file_get_contents(binary, &restored, &restored_size, NULL));
  g_assert_cmpmem(restored, restored_size, contents, size);
  /* Never overwrite a conflicting file or modify an unverified local plugin. */
  g_assert_true(g_file_set_contents(disabled, "another plugin", -1, NULL));
  g_assert_false(pumpkin_map_bridge_set_enabled(root, FALSE, &name, &changed, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_assert_false(changed); g_assert_null(name); g_clear_error(&error);
  g_assert_cmpint(g_remove(disabled), ==, 0);
  g_assert_true(g_file_set_contents(binary, "custom plugin", -1, NULL));
  g_assert_false(pumpkin_map_bridge_set_enabled(root, FALSE, &name, &changed, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_assert_false(changed); g_assert_null(name);
  g_assert_true(g_file_test(binary, G_FILE_TEST_EXISTS));
  g_assert_false(g_file_test(disabled, G_FILE_TEST_EXISTS));
  remove_tree(root);
}
static void test_chunk_revisions(void)
{
  g_autofree char *root = g_dir_make_tmp("smashed-map-revisions-XXXXXX", NULL);
  g_autofree char *path = region(root, "World/region", 0, 0, TRUE);
  guint8 contents[12288] = {0}; contents[2] = 2; contents[3] = 1; contents[4099] = 10;
  g_assert_true(g_file_set_contents(path, (const char *)contents, sizeof(contents), NULL));
  g_autoptr(GPtrArray) first = pumpkin_map_scan_regions(root, NULL, NULL);
  PumpkinMapWorld *world = g_ptr_array_index(first, 0);
  PumpkinMapTile *tile = g_ptr_array_index(world->tiles, 0);
  guint64 revision = tile->revisions[0];
  g_assert_cmpuint(revision, !=, 0);
  g_autoptr(GPtrArray) second = pumpkin_map_scan_regions(root, NULL, NULL);
  world = g_ptr_array_index(second, 0); tile = g_ptr_array_index(world->tiles, 0);
  g_assert_cmpuint(tile->revisions[0], ==, revision);
  contents[4099] = 11; contents[6] = 2; contents[7] = 1; contents[4103] = 11;
  g_assert_true(g_file_set_contents(path, (const char *)contents, sizeof(contents), NULL));
  g_autoptr(GPtrArray) third = pumpkin_map_scan_regions(root, NULL, NULL);
  world = g_ptr_array_index(third, 0); tile = g_ptr_array_index(world->tiles, 0);
  g_assert_cmpuint(world->chunks->len, ==, 2);
  g_assert_cmpuint(tile->revisions[0], !=, revision);
  g_assert_cmpuint(tile->revisions[1], !=, 0);
  remove_tree(root);
}
static void test_incomplete_region(void)
{
  g_autofree char *root = g_dir_make_tmp("smashed-map-saving-XXXXXX", NULL);
  g_autofree char *path = region(root, "World/region", 0, 0, TRUE);
  g_autoptr(GPtrArray) saved = pumpkin_map_scan_regions(root, NULL, NULL);
  PumpkinMapWorld *world = g_ptr_array_index(saved, 0);
  g_assert_false(world->inventory_incomplete);
  g_assert_true(g_file_set_contents(path, "", 0, NULL));
  g_autoptr(GPtrArray) writing = pumpkin_map_scan_regions(root, NULL, NULL);
  g_assert_cmpuint(writing->len, ==, 1);
  world = g_ptr_array_index(writing, 0);
  g_assert_true(world->inventory_incomplete);
  g_assert_cmpuint(world->chunks->len, ==, 0);
  g_assert_cmpint(g_remove(path), ==, 0);
  g_autoptr(GPtrArray) removed = pumpkin_map_scan_regions(root, NULL, NULL);
  g_assert_cmpuint(removed->len, ==, 0);
  remove_tree(root);
}
static void test_disk_cache(void)
{
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
  guint32 color = 0xff123456;
  memcpy(cairo_image_surface_get_data(surface), &color, 4); cairo_surface_mark_dirty(surface);
  pumpkin_map_cache_write("/fixture/world/region", -32, 61, 99, surface);
  cairo_surface_destroy(surface);
  /* No source world exists: this must be a persistent cache hit, not a decode. */
  surface = pumpkin_map_cache_read("/fixture/world/region", -32, 61, 99);
  g_assert_nonnull(surface);
  guint32 actual; memcpy(&actual, cairo_image_surface_get_data(surface), 4);
  g_assert_cmpuint(actual, ==, color); cairo_surface_destroy(surface);
  g_assert_null(pumpkin_map_cache_read("/fixture/world/region", -32, 61, 100));
  guint64 previous = 0;
  cairo_surface_t *old = pumpkin_map_cache_read_previous("/fixture/world/region", -32, 61, &previous);
  g_assert_nonnull(old); g_assert_cmpuint(previous, ==, 99); cairo_surface_destroy(old);
  g_assert_null(pumpkin_map_cache_read("/other/world/region", -32, 61, 99));
  g_autofree char *directory = g_build_filename(g_get_user_cache_dir(), "smashed-pumpkin", "minimap-v1", NULL);
  g_autoptr(GDir) entries = g_dir_open(directory, 0, NULL);
  g_autofree char *path = g_build_filename(directory, g_dir_read_name(entries), NULL);
  g_autofree char *bytes = NULL; gsize size;
  g_assert_true(g_file_get_contents(path, &bytes, &size, NULL)); bytes[32] ^= 1;
  g_assert_true(g_file_set_contents(path, bytes, (gssize)size, NULL));
  g_assert_null(pumpkin_map_cache_read("/fixture/world/region", -32, 61, 99));
}
static void test_saved_terrain_cache_speed(void)
{
  const char *region = g_getenv("SMASHED_TERRAIN_SAMPLE");
  if (region == NULL) { g_test_skip("No read-only terrain benchmark requested"); return; }
  g_autofree char *path = g_build_filename(region, "r.0.0.mca", NULL);
  g_autoptr(GFile) file = g_file_new_for_path(path);
  g_autoptr(GFileInputStream) stream = g_file_read(file, NULL, NULL);
  guint8 header[4096]; gsize size;
  g_assert_nonnull(stream);
  g_assert_true(g_input_stream_read_all(G_INPUT_STREAM(stream), header, sizeof(header), &size, NULL, NULL));
  g_assert_cmpuint(size, ==, sizeof(header));
  g_autoptr(PumpkinTerrainRegion) reader = pumpkin_terrain_region_open(region, 0, 0, NULL, NULL);
  g_assert_nonnull(reader);
  g_autoptr(GArray) slots = g_array_new(FALSE, FALSE, sizeof(guint));
  gint64 decode_us = 0;
  for (guint slot = 0; slot < 1024 && slots->len < 128; slot++) {
    if (header[slot * 4 + 3] == 0) continue;
    gint64 begin = g_get_monotonic_time();
    cairo_surface_t *tile = pumpkin_terrain_region_read(reader, (int)(slot % 32), (int)(slot / 32), NULL, NULL);
    decode_us += g_get_monotonic_time() - begin;
    if (tile == NULL) continue;
    pumpkin_map_cache_write(region, (int)(slot % 32), (int)(slot / 32), 99, tile);
    cairo_surface_destroy(tile); g_array_append_val(slots, slot);
  }
  g_assert_cmpuint(slots->len, >, 0);
  gint64 begin = g_get_monotonic_time();
  for (guint i = 0; i < slots->len; i++) {
    guint slot = g_array_index(slots, guint, i); guint64 revision = 0;
    cairo_surface_t *tile = pumpkin_map_cache_read_previous(region, (int)(slot % 32), (int)(slot / 32), &revision);
    g_assert_nonnull(tile); g_assert_cmpuint(revision, ==, 99); cairo_surface_destroy(tile);
  }
  g_test_message("%u real saved chunks: decode %.1f ms; cached restore %.1f ms (private cache, world read-only)",
    slots->len, decode_us / 1000.0, (g_get_monotonic_time() - begin) / 1000.0);
}
static void test_previous_bridge_upgrade(void)
{
  const char *sample = g_getenv("SMASHED_PREVIOUS_BRIDGE_SAMPLE");
  if (sample == NULL) { g_test_skip("No previous shipped bridge sample requested"); return; }
  g_autofree char *root = g_dir_make_tmp("smashed-map-upgrade-XXXXXX", NULL);
  g_autofree char *folder = g_build_filename(root, "plugins", NULL);
  g_assert_cmpint(g_mkdir(folder, 0700), ==, 0);
  g_autofree char *binary = g_build_filename(folder, "smashed-map-bridge.wasm", NULL);
  g_autofree char *old = NULL; gsize size;
  g_assert_true(g_file_get_contents(sample, &old, &size, NULL));
  g_assert_true(g_file_set_contents(binary, old, (gssize)size, NULL));
  g_autoptr(GBytes) bundled = g_bytes_new_static("test-wasm", 9);
  g_assert_cmpint(pumpkin_map_bridge_access(root, bundled, NULL), ==, PUMPKIN_MAP_BRIDGE_OUTDATED);
  g_autoptr(GError) error = NULL;
  g_assert_true(pumpkin_map_bridge_upgrade(root, bundled, &error)); g_assert_no_error(error);
  g_assert_cmpint(pumpkin_map_bridge_access(root, bundled, NULL), ==, PUMPKIN_MAP_BRIDGE_UNKNOWN);
  g_assert_false(g_file_test(binary, G_FILE_TEST_EXISTS));
  g_autofree char *canonical = g_build_filename(folder, PUMPKIN_MAP_BRIDGE_FILENAME, NULL);
  g_clear_pointer(&old, g_free); g_assert_true(g_file_get_contents(canonical, &old, NULL, NULL));
  g_assert_cmpstr(old, ==, "test-wasm"); remove_tree(root);
}
static void test_bridge_permissions(void)
{
  g_autofree char *root = g_dir_make_tmp("smashed-map-permissions-XXXXXX", NULL);
  g_autofree char *folder = g_build_filename(root, "plugins", NULL);
  g_assert_cmpint(g_mkdir(folder, 0700), ==, 0);
  g_autofree char *binary = g_build_filename(folder, PUMPKIN_MAP_BRIDGE_FILENAME, NULL);
  g_autoptr(GBytes) bundled = g_bytes_new_static("test-wasm", 9);
  g_assert_true(g_file_set_contents(binary, "test-wasm", 9, NULL));
  g_assert_cmpint(pumpkin_map_bridge_access(root, bundled, NULL), ==, PUMPKIN_MAP_BRIDGE_UNKNOWN);
  g_autofree char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, "test-wasm", 9);
  g_autofree char *path = g_build_filename(folder, "permission_cache.json", NULL);
  g_autofree char *json = g_strdup_printf("{\"entries\":{\"other\":{\"approved\":false,\"permissions_requested\":[\"network.outbound\"]},"
    "\"%s\":{\"approved\":false,\"permissions_requested\":[\"fs.write.data\"]}}}", hash);
  g_assert_true(g_file_set_contents(path, json, -1, NULL));
  g_assert_cmpint(pumpkin_map_bridge_access(root, bundled, NULL), ==, PUMPKIN_MAP_BRIDGE_DENIED);
  g_autoptr(GError) error = NULL;
  g_assert_true(pumpkin_map_bridge_approve(root, bundled, &error)); g_assert_no_error(error);
  g_assert_cmpint(pumpkin_map_bridge_access(root, bundled, NULL), ==, PUMPKIN_MAP_BRIDGE_APPROVED);
  g_autofree char *saved = NULL;
  g_assert_true(g_file_get_contents(path, &saved, NULL, NULL));
  cJSON *doc = cJSON_Parse(saved);
  cJSON *other = cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "entries"), "other");
  g_assert_true(cJSON_IsFalse(cJSON_GetObjectItem(other, "approved")));
  g_assert_cmpstr(cJSON_GetArrayItem(cJSON_GetObjectItem(other, "permissions_requested"), 0)->valuestring, ==, "network.outbound");
  cJSON_Delete(doc);
  g_assert_true(g_file_set_contents(binary, "other-wasm", -1, NULL));
  g_assert_false(pumpkin_map_bridge_approve(root, bundled, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED); g_clear_error(&error);
  g_assert_cmpint(pumpkin_map_bridge_access(root, bundled, NULL), ==, PUMPKIN_MAP_BRIDGE_UNTRUSTED);
  g_assert_false(pumpkin_map_bridge_upgrade(root, bundled, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED); g_clear_error(&error);
  g_autofree char *unchanged = NULL;
  g_assert_true(g_file_get_contents(binary, &unchanged, NULL, NULL));
  g_assert_cmpstr(unchanged, ==, "other-wasm");
  g_assert_true(g_file_set_contents(binary, "test-wasm", 9, NULL));
  g_assert_true(g_file_set_contents(path, "broken cache", -1, NULL));
  g_assert_false(pumpkin_map_bridge_approve(root, bundled, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA); g_clear_error(&error);
  g_clear_pointer(&saved, g_free); g_assert_true(g_file_get_contents(path, &saved, NULL, NULL));
  g_assert_cmpstr(saved, ==, "broken cache");
  g_assert_cmpint(g_remove(path), ==, 0);
  g_assert_true(pumpkin_map_bridge_approve(root, bundled, &error)); g_assert_no_error(error);
  g_assert_cmpint(pumpkin_map_bridge_access(root, bundled, NULL), ==, PUMPKIN_MAP_BRIDGE_APPROVED);
  remove_tree(root);
}
static void test_upgrade_permission_decisions(void)
{
  const char *sample = g_getenv("SMASHED_PREVIOUS_BRIDGE_SAMPLE");
  const char *current = g_getenv("SMASHED_BUNDLED_BRIDGE_SAMPLE");
  if (sample == NULL || current == NULL) { g_test_skip("Previous and bundled bridge samples required"); return; }
  g_autofree char *old_bytes = NULL, *new_bytes = NULL;
  gsize old_size, new_size;
  g_assert_true(g_file_get_contents(sample, &old_bytes, &old_size, NULL));
  g_assert_true(g_file_get_contents(current, &new_bytes, &new_size, NULL));
  g_autoptr(GBytes) bundled = g_bytes_new(new_bytes, new_size);
  g_autofree char *old_hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guint8 *)old_bytes, old_size);
  g_autofree char *new_hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guint8 *)new_bytes, new_size);
  g_assert_cmpstr(old_hash, !=, new_hash);
  for (guint variant = 0; variant < 8; ++variant) {
    g_autofree char *root = g_dir_make_tmp("smashed-map-update-decision-XXXXXX", NULL);
    g_autofree char *folder = g_build_filename(root, "plugins", NULL);
    g_assert_cmpint(g_mkdir(folder, 0700), ==, 0);
    g_autofree char *binary = g_build_filename(folder, "smashed-map-bridge.wasm", NULL);
    g_autofree char *canonical = g_build_filename(folder, PUMPKIN_MAP_BRIDGE_FILENAME, NULL);
    g_autofree char *cache = g_build_filename(folder, "permission_cache.json", NULL);
    g_assert_true(g_file_set_contents(binary, old_bytes, (gssize)old_size, NULL));
    if (variant == 6) g_assert_true(g_file_set_contents(canonical, "unknown-local-plugin", -1, NULL));
    if (variant == 7) {
      g_autofree char *config = g_build_filename(root, "pumpkin.toml", NULL);
      g_assert_true(g_file_set_contents(config, "[plugins.overrides.smashed-map-bridge]\nenabled = false\n", -1, NULL));
    }
    cJSON *root_doc = cJSON_CreateObject(), *entries = cJSON_AddObjectToObject(root_doc, "entries");
    cJSON *unrelated = cJSON_AddObjectToObject(entries, "other-plugin");
    cJSON_AddBoolToObject(unrelated, "approved", FALSE);
    cJSON_AddItemToArray(cJSON_AddArrayToObject(unrelated, "permissions_requested"), cJSON_CreateString("network.outbound"));
    cJSON *old = cJSON_AddObjectToObject(entries, old_hash);
    cJSON_AddBoolToObject(old, "approved", variant != 1);
    cJSON_AddItemToArray(cJSON_AddArrayToObject(old, "permissions_requested"),
      cJSON_CreateString(variant == 2 ? "network.outbound" : "fs.write.data"));
    if (variant == 3) {
      cJSON *existing = cJSON_AddObjectToObject(entries, new_hash);
      cJSON_AddBoolToObject(existing, "approved", FALSE);
      cJSON_AddItemToArray(cJSON_AddArrayToObject(existing, "permissions_requested"), cJSON_CreateString("fs.write.data"));
    }
    char *json = cJSON_PrintUnformatted(root_doc); cJSON_Delete(root_doc);
    g_assert_true(g_file_set_contents(cache, variant == 4 ? "invalid cache" : json, -1, NULL));
    cJSON_free(json);
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) future = g_bytes_new_static("new-scopes-not-verified", 23);
    gboolean ok = pumpkin_map_bridge_upgrade(root, variant == 5 ? future : bundled, &error);
    if (variant == 4 || variant >= 6) {
      g_assert_false(ok);
      int expected_code = variant == 4 ? G_IO_ERROR_INVALID_DATA : G_IO_ERROR_PERMISSION_DENIED;
      g_assert_error(error, G_IO_ERROR, expected_code);
      g_autofree char *unchanged = NULL; gsize size;
      g_assert_true(g_file_get_contents(binary, &unchanged, &size, NULL));
      g_assert_cmpmem(unchanged, size, old_bytes, old_size);
    } else {
      g_assert_true(ok); g_assert_no_error(error);
      g_assert_false(g_file_test(binary, G_FILE_TEST_EXISTS));
      g_assert_true(g_file_test(canonical, G_FILE_TEST_IS_REGULAR));
      PumpkinMapBridgeAccess expected = variant == 0 ? PUMPKIN_MAP_BRIDGE_APPROVED :
        variant == 1 || variant == 3 ? PUMPKIN_MAP_BRIDGE_DENIED : PUMPKIN_MAP_BRIDGE_UNKNOWN;
      g_assert_cmpint(pumpkin_map_bridge_access(root, variant == 5 ? future : bundled, NULL), ==, expected);
      g_autofree char *saved = NULL;
      g_assert_true(g_file_get_contents(cache, &saved, NULL, NULL));
      root_doc = cJSON_Parse(saved); entries = cJSON_GetObjectItemCaseSensitive(root_doc, "entries");
      g_assert_true(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(entries, "other-plugin"), "approved")));
      g_assert_nonnull(cJSON_GetObjectItemCaseSensitive(entries, old_hash));
      cJSON_Delete(root_doc);
    }
    remove_tree(root);
  }
}
static void test_player_motion(void)
{
  g_autoptr(PumpkinMapMotion) motion = pumpkin_map_motion_new();
  g_autoptr(GPtrArray) players = g_ptr_array_new();
  PumpkinMapPlayer player = {.id = "Alex", .world = "Lobby", .dimension = "minecraft:overworld", .yaw = 359, .yaw_known = TRUE};
  g_ptr_array_add(players, &player);
  PumpkinMapSnapshot snapshot = {.players = players, .updated_at_ms = 1000};
  pumpkin_map_motion_update(motion, &snapshot, 0, 2);
  g_assert_false(pumpkin_map_motion_active(motion, 0));
  player.x = 10; player.yaw = 1; snapshot.updated_at_ms = 1500;
  pumpkin_map_motion_update(motion, &snapshot, 500000, 2);
  PumpkinMapPose pose = pumpkin_map_motion_pose(motion, &player, 750000);
  g_assert_cmpfloat_with_epsilon(pose.x, 5, .0001);
  g_assert_cmpfloat_with_epsilon(remainder(pose.yaw, 360), 0, .0001);
  pumpkin_map_motion_update(motion, &snapshot, 750000, 20);
  g_assert_cmpfloat_with_epsilon(pumpkin_map_motion_pose(motion, &player, 750000).x, 5, .0001);
  player.x = 20; snapshot.updated_at_ms = 1800;
  pumpkin_map_motion_update(motion, &snapshot, 750000, 20);
  g_assert_cmpfloat_with_epsilon(pumpkin_map_motion_pose(motion, &player, 750000).x, 5, .0001);
  g_assert_cmpfloat_with_epsilon(pumpkin_map_motion_pose(motion, &player, 900000).x, 12.5, .0001);
  g_assert_cmpfloat_with_epsilon(pumpkin_map_motion_pose(motion, &player, 1050000).x, 20, .0001);
  g_assert_false(pumpkin_map_motion_active(motion, 1050000));
  player.x = 500; snapshot.updated_at_ms = 2300;
  pumpkin_map_motion_update(motion, &snapshot, 1250000, 2);
  g_assert_cmpfloat(pumpkin_map_motion_pose(motion, &player, 1250000).x, ==, 500);
  player.x = 501; player.dimension = "minecraft:the_nether"; snapshot.updated_at_ms = 2350;
  pumpkin_map_motion_update(motion, &snapshot, 1300000, 20);
  g_assert_cmpfloat(pumpkin_map_motion_pose(motion, &player, 1300000).x, ==, 501);
  g_assert_false(pumpkin_map_motion_active(motion, 1300000));
  g_ptr_array_set_size(players, 0); snapshot.updated_at_ms = 2400;
  pumpkin_map_motion_update(motion, &snapshot, 1350000, 20);
  g_assert_false(pumpkin_map_motion_active(motion, 1350000));
  pumpkin_map_motion_clear(motion);
}
static void test_chunk_render_seams(void)
{
  cairo_surface_t *chunk = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
  cairo_t *paint = cairo_create(chunk);
  cairo_set_source_rgb(paint, .2, .6, .4); cairo_paint(paint); cairo_destroy(paint);
  cairo_surface_flush(chunk);
  guint32 expected = *(guint32 *)cairo_image_surface_get_data(chunk);
  const double densities[] = {1, 1.25, 1.5, 2};
  for (guint d = 0; d < G_N_ELEMENTS(densities); ++d) for (guint zoom = 0; zoom < 36; ++zoom) {
    double scale = MIN(.05 * pow(1.16, zoom), 8), density = densities[d];
    cairo_surface_t *target = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 256, 256);
    cairo_t *cr = cairo_create(target);
    cairo_set_source_rgb(cr, 0, 0, 0); cairo_paint(cr); cairo_scale(cr, density, density);
    double origin_x = 10.37, origin_y = 12.61;
    for (int x = 0; x < 8; ++x) for (int z = 0; z < 8; ++z)
      pumpkin_map_paint_chunk(cr, chunk, origin_x + x * 16 * scale, origin_y + z * 16 * scale, scale);
    cairo_destroy(cr); cairo_surface_flush(target);
    int stride = cairo_image_surface_get_stride(target) / 4;
    const guint32 *pixels = (guint32 *)cairo_image_surface_get_data(target);
    int left = (int)round(origin_x * density), right = MIN(256, (int)round((origin_x + 128 * scale) * density));
    int top = (int)round(origin_y * density), bottom = MIN(256, (int)round((origin_y + 128 * scale) * density));
    for (int z = top; z < bottom; ++z) for (int x = left; x < right; ++x)
      g_assert_cmphex(pixels[z * stride + x], ==, expected);
    cairo_surface_destroy(target);
  }
  cairo_surface_t *gap = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 32);
  cairo_t *cr = cairo_create(gap); cairo_set_source_rgb(cr, 0, 0, 0); cairo_paint(cr);
  pumpkin_map_paint_chunk(cr, chunk, 10.3, 5, .7);
  pumpkin_map_paint_chunk(cr, chunk, 10.3 + 32 * .7, 5, .7);
  cairo_destroy(cr); cairo_surface_flush(gap);
  guint32 *pixels = (guint32 *)cairo_image_surface_get_data(gap);
  int stride = cairo_image_surface_get_stride(gap) / 4;
  for (int x = 22; x < 32; ++x) g_assert_cmphex(pixels[10 * stride + x], ==, 0xff000000);
  cairo_surface_destroy(gap); cairo_surface_destroy(chunk);
}
static void test_internal_plugins(void)
{
  g_assert_true(pumpkin_map_is_internal_plugin("map-bridge.wasm"));
  g_assert_true(pumpkin_map_is_internal_plugin("map-bridge.wasm.disabled"));
  g_assert_true(pumpkin_map_is_internal_plugin("smashed-map-bridge.wasm.deactivated"));
  g_assert_false(pumpkin_map_is_internal_plugin("my-map-bridge.wasm"));
  g_assert_false(pumpkin_map_is_internal_plugin("map-bridge-tools.wasm"));
}
static void test_player_marker(void)
{
  for (guint direction = 0; direction < 8; ++direction) {
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 48, 48);
    cairo_t *cr = cairo_create(surface);
    pumpkin_map_paint_player(cr, 24, 24, direction * 45, TRUE, 0x18d7c5, FALSE, FALSE);
    cairo_destroy(cr); cairo_surface_flush(surface);
    const guint32 *pixels = (guint32 *)cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface) / 4;
    g_assert_cmphex(pixels[24 * stride + 24], ==, 0xff18d7c5);
    for (int y = 0; y < 48; ++y) for (int x = 0; x < 48; ++x)
      if (pixels[y * stride + x] != 0) {
        g_assert_cmpint(ABS(x - 24), <=, 10); g_assert_cmpint(ABS(y - 24), <=, 10);
      }
    cairo_surface_destroy(surface);
  }
}
#ifndef G_OS_WIN32
static void test_symlinks(void)
{
  g_autofree char *root = g_dir_make_tmp("smashed-map-links-XXXXXX", NULL);
  g_autofree char *target = g_dir_make_tmp("smashed-map-link-target-XXXXXX", NULL);
  g_autofree char *saved = region(target, "World/region", 0, 0, TRUE);
  g_autofree char *linked = g_build_filename(root, "linked-world", NULL);
  g_autoptr(GFile) link = g_file_new_for_path(linked);
  g_assert_true(g_file_make_symbolic_link(link, target, NULL, NULL));
  g_autoptr(GPtrArray) inventory = pumpkin_map_scan_regions(root, NULL, NULL);
  g_assert_nonnull(inventory); g_assert_cmpuint(inventory->len, ==, 0);
  g_clear_object(&link);
  g_autofree char *plugins = g_build_filename(root, "plugins", NULL);
  link = g_file_new_for_path(plugins);
  g_assert_true(g_file_make_symbolic_link(link, target, NULL, NULL));
  g_autoptr(GError) error = NULL;
  g_assert_false(pumpkin_map_write_request(root, "{}", &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  remove_tree(root); remove_tree(target);
}
#endif
int main(int argc, char **argv)
{
  g_autofree char *cache = g_dir_make_tmp("smashed-map-cache-XXXXXX", NULL);
  g_setenv("XDG_CACHE_HOME", cache, TRUE);
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/minimap/snapshot", test_snapshot);
  g_test_add_func("/minimap/locator-yaw", test_locator_and_yaw);
  g_test_add_func("/minimap/validation", test_validation);
  g_test_add_func("/minimap/border", test_border);
  g_test_add_func("/minimap/inventory", test_inventory);
  g_test_add_func("/minimap/empty-cancelled", test_empty_and_cancelled);
  g_test_add_func("/minimap/discovery-bound", test_bounded_discovery);
  g_test_add_func("/minimap/local-ipc", test_ipc);
  g_test_add_func("/minimap/stop-request", test_stop_request);
  g_test_add_func("/minimap/bridge-enable-disable", test_bridge_enable_disable);
  g_test_add_func("/minimap/chunk-revisions", test_chunk_revisions);
  g_test_add_func("/minimap/concurrent-save", test_incomplete_region);
  g_test_add_func("/minimap/disk-cache", test_disk_cache);
  g_test_add_func("/minimap/saved-terrain-cache-speed", test_saved_terrain_cache_speed);
  g_test_add_func("/minimap/previous-bridge-upgrade", test_previous_bridge_upgrade);
  g_test_add_func("/minimap/bridge-permissions", test_bridge_permissions);
  g_test_add_func("/minimap/upgrade-permission-decisions", test_upgrade_permission_decisions);
  g_test_add_func("/minimap/player-motion", test_player_motion);
  g_test_add_func("/minimap/chunk-render-seams", test_chunk_render_seams);
  g_test_add_func("/minimap/internal-plugins", test_internal_plugins);
  g_test_add_func("/minimap/player-marker", test_player_marker);
#ifndef G_OS_WIN32
  g_test_add_func("/minimap/symlinks", test_symlinks);
#endif
  int result = g_test_run(); remove_tree(cache); return result;
}
