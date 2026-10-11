#include "minimap-cache.h"
#include <glib/gstdio.h>
#include <string.h>

#define TILE_BYTES (16 + 1024 + 32)
#define CACHE_LIMIT 16384

static char *cache_directory(void)
{ return g_build_filename(g_get_user_cache_dir(), "smashed-pumpkin", "minimap-v1", NULL); }
static char *cache_path(const char *region, int x, int z)
{
  g_autofree char *directory = cache_directory();
  g_autofree char *key = g_strdup_printf("%s/%d,%d", region, x, z);
  g_autofree char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, key, -1);
  g_autofree char *name = g_strconcat(hash, ".tile", NULL);
  return g_build_filename(directory, name, NULL);
}
static void digest(const guint8 *bytes, guint8 *result)
{
  g_autoptr(GChecksum) checksum = g_checksum_new(G_CHECKSUM_SHA256);
  g_checksum_update(checksum, bytes, TILE_BYTES - 32);
  gsize size = 32;
  g_checksum_get_digest(checksum, result, &size);
}
cairo_surface_t *pumpkin_map_cache_read_previous(const char *region, int x, int z, guint64 *revision)
{
  g_autofree char *path = cache_path(region, x, z);
  g_autoptr(GFile) file = g_file_new_for_path(path);
  g_autoptr(GFileInfo) info = g_file_query_info(file, "standard::size,standard::type,standard::is-symlink",
    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
  if (info == NULL || g_file_info_get_size(info) != TILE_BYTES || g_file_info_get_is_symlink(info) ||
      g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR) return NULL;
  g_autoptr(GFileInputStream) stream = g_file_read(file, NULL, NULL);
  guint8 bytes[TILE_BYTES], sum[32]; gsize count = 0;
  if (stream == NULL || !g_input_stream_read_all(G_INPUT_STREAM(stream), bytes, sizeof(bytes), &count, NULL, NULL) ||
      count != sizeof(bytes) || memcmp(bytes, "SMPKMAP1", 8) != 0) return NULL;
  guint64 saved; memcpy(&saved, bytes + 8, 8);
  digest(bytes, sum);
  if (memcmp(sum, bytes + TILE_BYTES - 32, 32) != 0) return NULL;
  *revision = GUINT64_FROM_BE(saved);
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(surface); return NULL; }
  guint8 *pixels = cairo_image_surface_get_data(surface);
  int stride = cairo_image_surface_get_stride(surface);
  for (guint i = 0; i < 256; i++) {
    guint32 color; memcpy(&color, bytes + 16 + i * 4, 4); color = GUINT32_FROM_BE(color);
    memcpy(pixels + (i / 16) * (guint)stride + (i % 16) * 4, &color, 4);
  }
  cairo_surface_mark_dirty(surface);
  return surface;
}
cairo_surface_t *pumpkin_map_cache_read(const char *region, int x, int z, guint64 revision)
{
  guint64 saved = 0;
  cairo_surface_t *surface = pumpkin_map_cache_read_previous(region, x, z, &saved);
  if (surface != NULL && saved != revision) { cairo_surface_destroy(surface); return NULL; }
  return surface;
}
typedef struct { char *name; guint64 modified; } CacheEntry;
static void entry_free(gpointer data)
{ CacheEntry *entry = data; g_free(entry->name); g_free(entry); }
static gint oldest_first(gconstpointer a, gconstpointer b)
{
  const CacheEntry *left = *(CacheEntry *const *)a, *right = *(CacheEntry *const *)b;
  return left->modified < right->modified ? -1 : left->modified > right->modified ? 1 : 0;
}
static void trim_cache(const char *directory)
{
  g_autoptr(GFile) folder = g_file_new_for_path(directory);
  g_autoptr(GFileEnumerator) files = g_file_enumerate_children(folder, "standard::name,time::modified",
    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
  if (files == NULL) return;
  g_autoptr(GPtrArray) entries = g_ptr_array_new_with_free_func(entry_free);
  for (;;) {
    g_autoptr(GFileInfo) info = g_file_enumerator_next_file(files, NULL, NULL);
    if (info == NULL) break;
    const char *name = g_file_info_get_name(info);
    if (strlen(name) != 69 || !g_str_has_suffix(name, ".tile")) continue;
    gboolean valid = TRUE;
    for (guint i = 0; i < 64; i++) if (!g_ascii_isxdigit(name[i])) { valid = FALSE; break; }
    if (!valid) continue;
    CacheEntry *entry = g_new0(CacheEntry, 1); entry->name = g_strdup(name);
    entry->modified = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    g_ptr_array_add(entries, entry);
  }
  if (entries->len <= CACHE_LIMIT) return;
  g_ptr_array_sort(entries, oldest_first);
  guint excess = entries->len - CACHE_LIMIT;
  for (guint i = 0; i < excess; i++) {
    CacheEntry *entry = g_ptr_array_index(entries, i);
    g_autoptr(GFile) file = g_file_get_child(folder, entry->name);
    g_file_delete(file, NULL, NULL);
  }
}
void pumpkin_map_cache_write(const char *region, int x, int z, guint64 revision, cairo_surface_t *surface)
{
  if (surface == NULL || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE ||
      cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32 ||
      cairo_image_surface_get_width(surface) != 16 || cairo_image_surface_get_height(surface) != 16) return;
  g_autofree char *directory = cache_directory();
  if (g_mkdir_with_parents(directory, 0700) != 0 || g_file_test(directory, G_FILE_TEST_IS_SYMLINK)) return;
  guint8 bytes[TILE_BYTES]; memcpy(bytes, "SMPKMAP1", 8);
  guint64 saved = GUINT64_TO_BE(revision); memcpy(bytes + 8, &saved, 8);
  cairo_surface_flush(surface);
  const guint8 *pixels = cairo_image_surface_get_data(surface);
  int stride = cairo_image_surface_get_stride(surface);
  for (guint i = 0; i < 256; i++) {
    guint32 color; memcpy(&color, pixels + (i / 16) * (guint)stride + (i % 16) * 4, 4);
    color = GUINT32_TO_BE(color); memcpy(bytes + 16 + i * 4, &color, 4);
  }
  digest(bytes, bytes + TILE_BYTES - 32);
  g_autofree char *path = cache_path(region, x, z);
  if (!g_file_set_contents_full(path, (const char *)bytes, sizeof(bytes), G_FILE_SET_CONTENTS_CONSISTENT, 0600, NULL)) return;
  /* Amortized background cleanup; at most 511 extra small files between sweeps. */
  static gint writes;
  if ((g_atomic_int_add(&writes, 1) & 511) == 511) trim_cache(directory);
}
