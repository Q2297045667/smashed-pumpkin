#include "minimap-terrain.h"
#include <math.h>
#include <string.h>

#define MAX_NBT (4 * 1024 * 1024)
#define MAX_COMPRESSED (1024 * 1024)
typedef struct { const guint8 *p; gsize size; guint8 type; } Tag;
typedef struct { const guint8 *p, *end; guint remaining; } Reader;

static guint32 u32(const guint8 *p)
{ return ((guint32)p[0] << 24) | ((guint32)p[1] << 16) | ((guint32)p[2] << 8) | p[3]; }
static guint32 le32(const guint8 *p)
{ return p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) | ((guint32)p[3] << 24); }
static guint64 u64(const guint8 *p) { return ((guint64)u32(p) << 32) | u32(p + 4); }
static gboolean take(Reader *r, gsize count)
{
  if (count > (gsize)(r->end - r->p)) return FALSE;
  r->p += count; return TRUE;
}
static gboolean text(Reader *r, const guint8 **value, guint *size)
{
  if ((gsize)(r->end - r->p) < 2) return FALSE;
  *size = ((guint)r->p[0] << 8) | r->p[1]; r->p += 2;
  *value = r->p; return take(r, *size);
}
static gboolean skip(Reader *r, guint8 type, guint depth)
{
  if (depth > 32 || r->remaining == 0) return FALSE;
  --r->remaining;
  static const guint widths[] = {0, 1, 2, 4, 8, 4, 8};
  if (type > 0 && type < G_N_ELEMENTS(widths)) return take(r, widths[type]);
  if (type == 8) { const guint8 *s; guint length; return text(r, &s, &length); }
  if (type == 7 || type == 11 || type == 12) {
    if ((gsize)(r->end - r->p) < 4) return FALSE;
    guint32 count = u32(r->p); r->p += 4;
    guint width = type == 7 ? 1 : type == 11 ? 4 : 8;
    return count <= MAX_NBT / width && take(r, (gsize)count * width);
  }
  if (type == 9) {
    if ((gsize)(r->end - r->p) < 5) return FALSE;
    guint8 item = *r->p++; guint32 count = u32(r->p); r->p += 4;
    if (count > r->remaining || (item == 0 && count != 0)) return FALSE;
    for (guint i = 0; i < count; i++) if (!skip(r, item, depth + 1)) return FALSE;
    return TRUE;
  }
  if (type == 10) {
    while (r->p < r->end) {
      guint8 child = *r->p++;
      if (child == 0) return TRUE;
      const guint8 *name; guint length;
      if (!text(r, &name, &length) || !skip(r, child, depth + 1)) return FALSE;
    }
  }
  return FALSE;
}
static Tag field(Tag compound, const char *key)
{
  if (compound.type != 10) return (Tag){0};
  Reader r = {compound.p, compound.p + compound.size, 200000};
  while (r.p < r.end) {
    guint8 type = *r.p++; if (type == 0) break;
    const guint8 *name; guint length;
    if (!text(&r, &name, &length)) break;
    const guint8 *start = r.p;
    if (!skip(&r, type, 0)) break;
    if (length == strlen(key) && memcmp(name, key, length) == 0)
      return (Tag){start, (gsize)(r.p - start), type};
  }
  return (Tag){0};
}
static gboolean equals(Tag tag, const char *value)
{
  return tag.type == 8 && tag.size == strlen(value) + 2 &&
    memcmp(tag.p + 2, value, tag.size - 2) == 0;
}
static guint32 block_color(Tag name)
{
  if (name.type != 8 || name.size < 3 || name.size > 258) return 0xff888888;
  char block[257]; memcpy(block, name.p + 2, name.size - 2); block[name.size - 2] = 0;
  if (equals(name, "minecraft:air") || equals(name, "minecraft:cave_air") || equals(name, "minecraft:void_air")) return 0;
  if (strstr(block, "water")) return 0xff397fc1;
  if (strstr(block, "lava")) return 0xffe57d26;
  if (strstr(block, "snow") || strstr(block, "quartz")) return 0xffe9eeee;
  if (strstr(block, "ice")) return 0xff90bfd9;
  if (strstr(block, "leaves") || strstr(block, "moss")) return 0xff477f43;
  if (strstr(block, "grass") || strstr(block, "fern")) return 0xff79a956;
  if (strstr(block, "sand") || strstr(block, "end_stone")) return 0xffd9cc97;
  if (strstr(block, "dirt") || strstr(block, "mud") || strstr(block, "farmland")) return 0xff937650;
  if (strstr(block, "log") || strstr(block, "wood") || strstr(block, "planks")) return 0xffa8875c;
  if (strstr(block, "netherrack") || strstr(block, "nether_wart")) return 0xff98534b;
  if (strstr(block, "soul")) return 0xff665447;
  if (strstr(block, "deepslate") || strstr(block, "basalt") || strstr(block, "blackstone")) return 0xff53535c;
  if (strstr(block, "stone") || strstr(block, "gravel") || strstr(block, "ore")) return 0xff92958e;
  if (strstr(block, "obsidian")) return 0xff443956;
  return 0xffa39b89;
}
static gboolean section_pixels(Tag section, guint32 *pixels, int *heights)
{
  Tag y = field(section, "Y"); int sy;
  if (y.type == 1) sy = y.p[0] <= 127 ? y.p[0] : (int)y.p[0] - 256;
  else if (y.type == 3) { guint32 n = u32(y.p); if (n > 2047 && n < 0xfffff800) return FALSE; sy = n <= 2047 ? (int)n : -(int)(~n) - 1; }
  else return FALSE;
  Tag states = field(section, "block_states");
  Tag palette = field(states.type == 10 ? states : section, states.type == 10 ? "palette" : "Palette");
  Tag data = field(states.type == 10 ? states : section, states.type == 10 ? "data" : "BlockStates");
  if (palette.type != 9 || palette.size < 5 || palette.p[0] != 10) return FALSE;
  guint count = u32(palette.p + 1); if (count == 0 || count > 4096) return FALSE;
  guint32 colors[4096];
  Reader r = {palette.p + 5, palette.p + palette.size, 200000};
  for (guint i = 0; i < count; i++) {
    const guint8 *start = r.p; if (!skip(&r, 10, 0)) return FALSE;
    colors[i] = block_color(field((Tag){start, (gsize)(r.p - start), 10}, "Name"));
  }
  guint bits = 4; while ((1u << bits) < count) ++bits;
  guint per_long = 64 / bits;
  guint words = data.type == 12 ? u32(data.p) : 0;
  gboolean padded = words == (4096 + per_long - 1) / per_long;
  if (count > 1 && (!padded && words != (4096 * bits + 63) / 64)) return FALSE;
  if (count == 1 && colors[0] == 0) return TRUE;
  for (guint z = 0; z < 16; z++) for (guint x = 0; x < 16; x++) {
    guint column = z * 16 + x;
    for (int ly = 15; ly >= 0; ly--) {
      int height = sy * 16 + ly; if (height <= heights[column]) break;
      guint index = (guint)ly * 256 + column, state = 0;
      if (count > 1) {
        guint word = padded ? index / per_long : index * bits / 64;
        guint shift = padded ? (index % per_long) * bits : index * bits % 64;
        guint64 value = u64(data.p + 4 + word * 8) >> shift;
        if (!padded && shift + bits > 64) value |= u64(data.p + 4 + (word + 1) * 8) << (64 - shift);
        state = (guint)(value & ((1u << bits) - 1));
      }
      if (state >= count) return FALSE;
      if (colors[state] != 0) { pixels[column] = colors[state]; heights[column] = height; break; }
    }
  }
  return TRUE;
}
static cairo_surface_t *invalid(GError **error, const char *message)
{
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, message); return NULL;
}
cairo_surface_t *pumpkin_map_decode_terrain(const guint8 *nbt, gsize size, GError **error)
{
  if (size < 4 || size > MAX_NBT || nbt[0] != 10) return invalid(error, "Invalid chunk NBT root");
  Reader r = {nbt + 1, nbt + size, 200000}; const guint8 *name; guint length;
  if (!text(&r, &name, &length)) return invalid(error, "Truncated chunk NBT root");
  const guint8 *start = r.p;
  if (!skip(&r, 10, 0) || r.p != r.end) return invalid(error, "Invalid or oversized chunk NBT tree");
  Tag root = {start, (gsize)(r.p - start), 10}, level = field(root, "Level");
  if (level.type == 10) root = level;
  Tag status = field(root, "Status");
  if (status.type != 0 && !equals(status, "minecraft:full") && !equals(status, "full"))
    return invalid(error, "Chunk terrain is not fully generated");
  Tag sections = field(root, "sections"); if (sections.type == 0) sections = field(root, "Sections");
  if (sections.type != 9 || sections.p[0] != 10 || u32(sections.p + 1) > 64)
    return invalid(error, "Unsupported chunk sections");
  guint32 pixels[256] = {0}; int heights[256]; for (guint i = 0; i < 256; i++) heights[i] = G_MININT;
  Reader list = {sections.p + 5, sections.p + sections.size, 200000}; guint valid = 0;
  for (guint i = 0; i < u32(sections.p + 1); i++) {
    const guint8 *begin = list.p;
    if (!skip(&list, 10, 0)) return invalid(error, "Invalid chunk section");
    Tag section = {begin, (gsize)(list.p - begin), 10};
    if (field(section, "block_states").type == 0 && field(section, "Palette").type == 0) continue;
    if (!section_pixels(section, pixels, heights)) return invalid(error, "Invalid chunk block palette");
    ++valid;
  }
  if (valid == 0) return invalid(error, "No block terrain in this chunk");
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(surface); return invalid(error, "Cannot allocate terrain tile"); }
  guint8 *bytes = cairo_image_surface_get_data(surface); int stride = cairo_image_surface_get_stride(surface);
  for (guint z = 0; z < 16; z++) for (guint x = 0; x < 16; x++) {
    guint i = z * 16 + x; guint32 color = pixels[i];
    if (color != 0 && z > 0 && pixels[i - 16] != 0) {
      double shade = heights[i] > heights[i - 16] ? 1.08 : heights[i] < heights[i - 16] ? .86 : 1;
      guint red = (guint)MIN(255, ((color >> 16) & 255) * shade);
      guint green = (guint)MIN(255, ((color >> 8) & 255) * shade);
      guint blue = (guint)MIN(255, (color & 255) * shade);
      color = 0xff000000 | (red << 16) | (green << 8) | blue;
    }
    memcpy(bytes + (gsize)z * stride + x * 4, &color, 4);
  }
  cairo_surface_mark_dirty(surface); return surface;
}
static guint32 rot(guint32 n, guint bits) { return (n << bits) | (n >> (32 - bits)); }
/* xxHash32 for the Java LZ4Block checksum (seed 0x9747b28c). */
static guint32 checksum(const guint8 *p, guint length)
{
  const guint32 a = 2654435761u, b = 2246822519u, c = 3266489917u, d = 668265263u, e = 374761393u;
  guint32 hash; guint offset = 0;
  if (length >= 16) {
    guint32 v[] = {0x9747b28cu + a + b, 0x9747b28cu + b, 0x9747b28cu, 0x9747b28cu - a};
    do { for (guint i = 0; i < 4; i++) { v[i] = rot(v[i] + le32(p + offset) * b, 13) * a; offset += 4; } } while (offset <= length - 16);
    hash = rot(v[0], 1) + rot(v[1], 7) + rot(v[2], 12) + rot(v[3], 18);
  } else hash = 0x9747b28cu + e;
  hash += length;
  while (offset + 4 <= length) { hash = rot(hash + le32(p + offset) * c, 17) * d; offset += 4; }
  while (offset < length) hash = rot(hash + p[offset++] * e, 11) * a;
  hash ^= hash >> 15; hash *= b; hash ^= hash >> 13; hash *= c; hash ^= hash >> 16;
  return hash & 0x0fffffff;
}
static gboolean lz4_block(const guint8 *src, guint size, guint8 *dest, guint length)
{
  guint in = 0, out = 0;
  while (in < size) {
    guint token = src[in++], literals = token >> 4;
    if (literals == 15) { guint more; do { if (in == size) return FALSE; more = src[in++]; literals += more; } while (more == 255); }
    if (literals > size - in || literals > length - out) return FALSE;
    memcpy(dest + out, src + in, literals); out += literals; in += literals;
    if (in == size) return out == length;
    if (size - in < 2) return FALSE;
    guint distance = src[in] | ((guint)src[in + 1] << 8); in += 2;
    if (distance == 0 || distance > out) return FALSE;
    guint match = (token & 15) + 4;
    if ((token & 15) == 15) { guint more; do { if (in == size) return FALSE; more = src[in++]; match += more; } while (more == 255); }
    if (match > length - out) return FALSE;
    for (guint i = 0; i < match; i++) { dest[out] = dest[out - distance]; ++out; }
  }
  return out == length;
}
static GBytes *decompress(const guint8 *data, gsize size, guint type, GCancellable *cancel, GError **error)
{
  if (type == 3) return g_bytes_new(data, size);
  if (type == 4) {
    g_autoptr(GByteArray) out = g_byte_array_new(); gsize offset = 0;
    while (size - offset >= 21) {
      const guint8 *h = data + offset; offset += 21;
      if (memcmp(h, "LZ4Block", 8) != 0) break;
      guint packed = le32(h + 9), length = le32(h + 13), check = le32(h + 17), kind = h[8] & 0xf0;
      if (length == 0 && packed == 0 && check == 0 && offset == size && (kind == 0x10 || kind == 0x20))
        return g_byte_array_free_to_bytes(g_steal_pointer(&out));
      if (length == 0 || packed == 0 || packed > size - offset || length > MAX_NBT - out->len ||
          length > (1u << (10 + (h[8] & 15))) || g_cancellable_is_cancelled(cancel)) break;
      guint start = out->len; g_byte_array_set_size(out, start + length);
      if (kind == 0x10 && packed == length) memcpy(out->data + start, data + offset, length);
      else if (kind != 0x20 || !lz4_block(data + offset, packed, out->data + start, length)) break;
      if (checksum(out->data + start, length) != check) break;
      offset += packed;
      /* Pumpkin's writer ends at EOF after the last checked block. */
      if (offset == size) return g_byte_array_free_to_bytes(g_steal_pointer(&out));
    }
    invalid(error, "Invalid Java LZ4 chunk stream or checksum"); return NULL;
  }
  if (type != 1 && type != 2) { invalid(error, "Unsupported chunk compression"); return NULL; }
  g_autoptr(GInputStream) memory = g_memory_input_stream_new_from_data(data, (gssize)size, NULL);
  g_autoptr(GZlibDecompressor) decoder = g_zlib_decompressor_new(type == 1 ? G_ZLIB_COMPRESSOR_FORMAT_GZIP : G_ZLIB_COMPRESSOR_FORMAT_ZLIB);
  g_autoptr(GInputStream) input = g_converter_input_stream_new(memory, G_CONVERTER(decoder));
  g_autofree guint8 *bytes = g_malloc(MAX_NBT + 1); gsize count;
  if (!g_input_stream_read_all(input, bytes, MAX_NBT + 1, &count, cancel, error)) return NULL;
  if (count > MAX_NBT) { invalid(error, "Decompressed chunk exceeds map limit"); return NULL; }
  return g_bytes_new_take(g_steal_pointer(&bytes), count);
}
static gboolean read_at(GFileInputStream *stream, goffset offset, void *bytes, gsize size, GCancellable *cancel, GError **error)
{
  gsize count;
  if (!g_seekable_seek(G_SEEKABLE(stream), offset, G_SEEK_SET, cancel, error) ||
      !g_input_stream_read_all(G_INPUT_STREAM(stream), bytes, size, &count, cancel, error)) return FALSE;
  if (count != size) { invalid(error, "Chunk region was truncated while reading"); return FALSE; }
  return TRUE;
}
struct _PumpkinTerrainRegion {
  GFileInputStream *stream;
  guint8 locations[4096];
  guint64 size;
  int x, z;
};
void pumpkin_terrain_region_free(PumpkinTerrainRegion *region)
{
  if (region == NULL) return;
  g_clear_object(&region->stream); g_free(region);
}
PumpkinTerrainRegion *pumpkin_terrain_region_open(const char *region, int rx, int rz, GCancellable *cancel, GError **error)
{
  g_autofree char *name = g_strdup_printf("r.%d.%d.mca", rx, rz);
  g_autofree char *path = g_build_filename(region, name, NULL);
  g_autoptr(GFile) file = g_file_new_for_path(path);
  g_autoptr(GFileInfo) info = g_file_query_info(file, "standard::type,standard::is-symlink,standard::size", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
  if (info == NULL) return NULL;
  if (g_file_info_get_is_symlink(info) || g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR)
    { invalid(error, "Terrain region must be an ordinary file"); return NULL; }
  g_autoptr(PumpkinTerrainRegion) reader = g_new0(PumpkinTerrainRegion, 1);
  reader->stream = g_file_read(file, cancel, error); if (reader->stream == NULL) return NULL;
  reader->size = (guint64)g_file_info_get_size(info); reader->x = rx; reader->z = rz;
  if (!read_at(reader->stream, 0, reader->locations, sizeof(reader->locations), cancel, error)) return NULL;
  return g_steal_pointer(&reader);
}
cairo_surface_t *pumpkin_terrain_region_read(PumpkinTerrainRegion *region, int x, int z, GCancellable *cancel, GError **error)
{
  if ((int)floor(x / 32.0) != region->x || (int)floor(z / 32.0) != region->z)
    return invalid(error, "Chunk is outside this region");
  GFileInputStream *stream = region->stream;
  guint8 header[5]; guint slot = (guint)((z - region->z * 32) * 32 + x - region->x * 32);
  const guint8 *location = region->locations + slot * 4;
  guint offset = u32(location) >> 8, sectors = location[3];
  if (offset < 2 || sectors == 0 || ((guint64)offset + sectors) * 4096 > region->size)
    return invalid(error, "Chunk is not saved or region is being rewritten");
  if (!read_at(stream, (goffset)offset * 4096, header, 5, cancel, error)) return NULL;
  guint length = u32(header);
  if ((header[4] & 128) != 0) return invalid(error, "External chunk payload is not supported yet");
  if (length < 2 || length > MAX_COMPRESSED || length + 4 > sectors * 4096)
    return invalid(error, "Invalid chunk payload length");
  g_autofree guint8 *bytes = g_malloc(length - 1); gsize count;
  if (!g_input_stream_read_all(G_INPUT_STREAM(stream), bytes, length - 1, &count, cancel, error) || count != length - 1) return NULL;
  g_autoptr(GBytes) decoded = decompress(bytes, length - 1, header[4], cancel, error);
  if (decoded == NULL) return NULL;
  gsize size; const guint8 *nbt = g_bytes_get_data(decoded, &size);
  return pumpkin_map_decode_terrain(nbt, size, error);
}
cairo_surface_t *pumpkin_map_read_terrain(const char *region, int x, int z, GCancellable *cancel, GError **error)
{
  g_autoptr(PumpkinTerrainRegion) reader = pumpkin_terrain_region_open(region,
    (int)floor(x / 32.0), (int)floor(z / 32.0), cancel, error);
  return reader != NULL ? pumpkin_terrain_region_read(reader, x, z, cancel, error) : NULL;
}
