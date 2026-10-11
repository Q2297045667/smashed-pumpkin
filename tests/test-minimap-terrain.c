#include "minimap-terrain.h"
#include <glib/gstdio.h>
#include <string.h>

static void number(GByteArray *bytes, guint32 n)
{
  guint8 value[] = {(guint8)(n >> 24), (guint8)(n >> 16), (guint8)(n >> 8), (guint8)n};
  g_byte_array_append(bytes, value, 4);
}
static void string(GByteArray *bytes, const char *s)
{
  guint length = (guint)strlen(s); guint8 prefix[] = {(guint8)(length >> 8), (guint8)length};
  g_byte_array_append(bytes, prefix, 2); g_byte_array_append(bytes, (const guint8 *)s, length);
}
static void little_number(GByteArray *bytes, guint32 n)
{
  guint8 value[] = {(guint8)n, (guint8)(n >> 8), (guint8)(n >> 16), (guint8)(n >> 24)};
  g_byte_array_append(bytes, value, 4);
}
static void named(GByteArray *bytes, guint8 type, const char *name)
{ g_byte_array_append(bytes, &type, 1); string(bytes, name); }
static void byte(GByteArray *bytes, guint8 n) { g_byte_array_append(bytes, &n, 1); }
static GByteArray *chunk(void)
{
  GByteArray *bytes = g_byte_array_new(); named(bytes, 10, "");
  named(bytes, 8, "Status"); string(bytes, "minecraft:full");
  named(bytes, 9, "sections"); byte(bytes, 10); number(bytes, 1);
  named(bytes, 1, "Y"); byte(bytes, 0);
  named(bytes, 10, "block_states");
  named(bytes, 9, "palette"); byte(bytes, 10); number(bytes, 3);
  const char *blocks[] = {"minecraft:air", "minecraft:grass_block", "minecraft:water"};
  for (guint i = 0; i < 3; i++) { named(bytes, 8, "Name"); string(bytes, blocks[i]); byte(bytes, 0); }
  named(bytes, 12, "data"); number(bytes, 256);
  for (guint i = 0; i < 256; i++) {
    guint32 value = i >= 240 ? (i < 248 ? 0x11111111 : 0x22222222) : 0;
    number(bytes, value); number(bytes, value);
  }
  byte(bytes, 0); byte(bytes, 0); byte(bytes, 0); return bytes;
}
static void check_image(cairo_surface_t *surface)
{
  g_assert_nonnull(surface);
  guint8 *data = cairo_image_surface_get_data(surface); int stride = cairo_image_surface_get_stride(surface);
  guint32 grass, water; memcpy(&grass, data, 4); memcpy(&water, data + 15 * stride, 4);
  g_assert_cmpuint(grass, ==, 0xff79a956); g_assert_cmpuint(water, ==, 0xff397fc1);
  cairo_surface_destroy(surface);
}
static void test_decode(void)
{
  g_autoptr(GByteArray) bytes = chunk(); g_autoptr(GError) error = NULL;
  check_image(pumpkin_map_decode_terrain(bytes->data, bytes->len, &error)); g_assert_no_error(error);
  for (guint size = 0; size < bytes->len; size++) {
    cairo_surface_t *image = pumpkin_map_decode_terrain(bytes->data, size, &error);
    g_assert_null(image); g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA); g_clear_error(&error);
  }
  g_autoptr(GRand) random = g_rand_new_with_seed(8751);
  guint8 corrupt[512];
  for (guint i = 0; i < 1000; i++) {
    for (guint j = 0; j < sizeof(corrupt); j++) corrupt[j] = (guint8)g_rand_int(random);
    corrupt[0] = 10; corrupt[1] = corrupt[2] = 0;
    cairo_surface_t *image = pumpkin_map_decode_terrain(corrupt, sizeof(corrupt), NULL);
    if (image != NULL) cairo_surface_destroy(image);
  }
}
static void test_region(void)
{
  g_autofree char *dir = g_dir_make_tmp("smashed-terrain-test-XXXXXX", NULL);
  g_autofree char *path = g_build_filename(dir, "r.-1.0.mca", NULL);
  g_autoptr(GByteArray) nbt = chunk();
  for (guint scenario = 1; scenario <= 5; scenario++) {
    guint compression = MIN(scenario, 4);
    g_autoptr(GBytes) payload = NULL;
    if (compression == 3) payload = g_bytes_new(nbt->data, nbt->len);
    else if (compression == 4) {
      g_autoptr(GByteArray) block = g_byte_array_new();
      if (scenario == 5) {
        byte(block, 0xf0); guint length = nbt->len - 15;
        while (length >= 255) { byte(block, 255); length -= 255; }
        byte(block, (guint8)length);
      }
      g_byte_array_append(block, nbt->data, nbt->len);
      g_autoptr(GByteArray) framed = g_byte_array_new();
      g_byte_array_append(framed, (const guint8 *)"LZ4Block", 8); byte(framed, scenario == 5 ? 0x24 : 0x14);
      little_number(framed, block->len); little_number(framed, nbt->len);
      little_number(framed, 0x06a764c9); /* Fixed xxHash32 of the synthetic NBT fixture. */
      g_byte_array_append(framed, block->data, block->len);
      payload = g_byte_array_free_to_bytes(g_steal_pointer(&framed));
    }
    else {
      g_autoptr(GOutputStream) memory = g_memory_output_stream_new_resizable();
      g_autoptr(GZlibCompressor) coder = g_zlib_compressor_new(compression == 1 ? G_ZLIB_COMPRESSOR_FORMAT_GZIP : G_ZLIB_COMPRESSOR_FORMAT_ZLIB, 6);
      g_autoptr(GOutputStream) output = g_converter_output_stream_new(memory, G_CONVERTER(coder));
      g_assert_true(g_output_stream_write_all(output, nbt->data, nbt->len, NULL, NULL, NULL));
      g_assert_true(g_output_stream_close(output, NULL, NULL));
      payload = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(memory));
    }
    gsize size; const guint8 *packed = g_bytes_get_data(payload, &size);
    guint sectors = (guint)(size + 5 + 4095) / 4096;
    g_autoptr(GByteArray) region = g_byte_array_new(); g_byte_array_set_size(region, 8192 + sectors * 4096);
    memset(region->data, 0, region->len); region->data[31 * 4 + 2] = 2; region->data[31 * 4 + 3] = (guint8)sectors;
    guint length = (guint)size + 1;
    region->data[8192] = (guint8)(length >> 24); region->data[8193] = (guint8)(length >> 16);
    region->data[8194] = (guint8)(length >> 8); region->data[8195] = (guint8)length;
    region->data[8196] = (guint8)compression; memcpy(region->data + 8197, packed, size);
    g_assert_true(g_file_set_contents(path, (const char *)region->data, region->len, NULL));
    g_autoptr(GError) error = NULL;
    check_image(pumpkin_map_read_terrain(dir, -1, 0, NULL, &error)); g_assert_no_error(error);
    if (compression == 4) {
      region->data[8197 + 17] ^= 1;
      g_assert_true(g_file_set_contents(path, (const char *)region->data, region->len, NULL));
      g_assert_null(pumpkin_map_read_terrain(dir, -1, 0, NULL, &error));
      g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA); g_clear_error(&error);
      region->data[8197 + 17] ^= 1;
      g_assert_true(g_file_set_contents(path, (const char *)region->data, region->len, NULL));
    }
    g_assert_null(pumpkin_map_read_terrain(dir, -2, 0, NULL, &error)); g_assert_nonnull(error);
    g_clear_error(&error);
    g_autoptr(GCancellable) cancel = g_cancellable_new(); g_cancellable_cancel(cancel);
    g_assert_null(pumpkin_map_read_terrain(dir, -1, 0, cancel, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  }
  g_assert_cmpint(g_remove(path), ==, 0); g_assert_cmpint(g_rmdir(dir), ==, 0);
}
static void test_sample(void)
{
  const char *dir = g_getenv("SMASHED_TERRAIN_SAMPLE");
  if (dir == NULL) { g_test_skip("No read-only real-world sample requested"); return; }
  g_autoptr(GError) error = NULL;
  cairo_surface_t *surface = pumpkin_map_read_terrain(dir, 0, 0, NULL, &error);
  g_assert_no_error(error); g_assert_nonnull(surface);
  g_assert_cmpint(cairo_image_surface_get_width(surface), ==, 16);
  cairo_surface_destroy(surface);
}
int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/terrain/nbt-and-malformed", test_decode);
  g_test_add_func("/terrain/region-compression-negative-and-cancel", test_region);
  g_test_add_func("/terrain/read-only-sample", test_sample);
  return g_test_run();
}
