#include "plugin-store.h"
#include "vendor/tomlc17/tomlc17.h"

#include <glib/gstdio.h>
#include <math.h>
#include <string.h>

typedef struct {
  GFile *file;
  char *contents;
  char *etag;
  gboolean exists;
  toml_result_t parsed;
} PluginLock;

static void
plugin_lock_free(PluginLock *lock)
{
  if (lock == NULL) {
    return;
  }
  toml_free(lock->parsed);
  g_clear_object(&lock->file);
  g_free(lock->contents);
  g_free(lock->etag);
  g_free(lock);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PluginLock, plugin_lock_free)

void
pumpkin_plugin_metadata_free(PumpkinPluginMetadata *metadata)
{
  if (metadata == NULL) {
    return;
  }
  g_free(metadata->public_id);
  g_free(metadata->name);
  g_free(metadata->author);
  g_free(metadata->version);
  g_free(metadata->file);
  g_free(metadata->updated_at);
  g_free(metadata);
}

static char *
active_filename(const char *file)
{
  if (file == NULL) {
    return NULL;
  }
  if (g_str_has_suffix(file, ".deactivated")) {
    return g_strndup(file, strlen(file) - strlen(".deactivated"));
  }
  if (g_str_has_suffix(file, ".disabled")) {
    return g_strndup(file, strlen(file) - strlen(".disabled"));
  }
  return g_strdup(file);
}

static gboolean
same_file(const char *left, const char *right)
{
  g_autofree char *a = active_filename(left);
  g_autofree char *b = active_filename(right);
  return a != NULL && b != NULL && g_str_equal(a, b);
}

static gboolean
safe_filename(const char *file)
{
  return file != NULL && *file != '\0' && strcmp(file, ".") != 0 &&
         strcmp(file, "..") != 0 && strchr(file, '/') == NULL &&
         strchr(file, '\\') == NULL && strchr(file, ':') == NULL &&
         !g_path_is_absolute(file);
}

static gboolean
valid_strings(toml_datum_t value)
{
  if (value.type == TOML_STRING) {
    return (gsize)value.u.str.len == strlen(value.u.s) && g_utf8_validate(value.u.s, -1, NULL);
  }
  if (value.type == TOML_TABLE) {
    for (int i = 0; i < value.u.tab.size; i++) {
      if ((gsize)value.u.tab.len[i] != strlen(value.u.tab.key[i]) ||
          !valid_strings(value.u.tab.value[i])) return FALSE;
    }
  } else if (value.type == TOML_ARRAY) {
    for (int i = 0; i < value.u.arr.size; i++) {
      if (!valid_strings(value.u.arr.elem[i])) return FALSE;
    }
  }
  return TRUE;
}

static gboolean
valid_entry(const char *key, toml_datum_t entry)
{
  if (entry.type != TOML_TABLE) return FALSE;
  toml_datum_t id = toml_get(entry, "id");
  toml_datum_t public_id = toml_get(entry, "public_id");
  toml_datum_t file = toml_get(entry, "file");
  if (id.type != TOML_INT64 || id.u.int64 <= 0 || public_id.type != TOML_STRING ||
      *public_id.u.s == '\0' || strcmp(key, public_id.u.s) != 0 ||
      file.type != TOML_STRING || !safe_filename(file.u.s) ||
      toml_get(entry, "name").type != TOML_STRING || toml_get(entry, "author").type != TOML_STRING) {
    return FALSE;
  }
  toml_datum_t version = toml_get(entry, "version");
  toml_datum_t updated = toml_get(entry, "updated_at");
  return (version.type == TOML_STRING || version.type == TOML_UNKNOWN) &&
         (updated.type == TOML_STRING || updated.type == TOML_UNKNOWN);
}

static PluginLock *
load_lock(const char *directory, GError **error)
{
  g_autoptr(PluginLock) lock = g_new0(PluginLock, 1);
  g_autofree char *path = g_build_filename(directory, ".ppm.lock", NULL);
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    g_autofree char *fallback = g_build_filename(directory, "ppm.lock", NULL);
    if (g_file_test(fallback, G_FILE_TEST_EXISTS)) {
      g_free(path);
      path = g_steal_pointer(&fallback);
    }
  }
  lock->file = g_file_new_for_path(path);
  g_autoptr(GError) read_error = NULL;
  gsize size = 0;
  if (!g_file_load_contents(lock->file, NULL, &lock->contents, &size, &lock->etag, &read_error)) {
    if (!g_error_matches(read_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
      g_propagate_error(error, g_steal_pointer(&read_error));
      return NULL;
    }
    lock->contents = g_strdup("version = 1\n[plugins]\n");
    size = strlen(lock->contents);
  } else {
    lock->exists = TRUE;
  }
  if (size > 2 * 1024 * 1024 || memchr(lock->contents, '\0', size) != NULL ||
      !g_utf8_validate(lock->contents, (gssize)size, NULL)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid or oversized ppm lockfile.");
    return NULL;
  }
  lock->parsed = toml_parse(lock->contents, (int)size);
  if (!lock->parsed.ok) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid ppm lockfile: %s", lock->parsed.errmsg);
    return NULL;
  }
  toml_datum_t version = toml_get(lock->parsed.toptab, "version");
  toml_datum_t plugins = toml_get(lock->parsed.toptab, "plugins");
  if (version.type != TOML_INT64 || version.u.int64 != 1 ||
      (plugins.type != TOML_TABLE && plugins.type != TOML_UNKNOWN)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Unsupported ppm lockfile format; it was not modified.");
    return NULL;
  }
  if (!valid_strings(lock->parsed.toptab)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid strings in ppm lockfile; it was not modified.");
    return NULL;
  }
  if (plugins.type == TOML_TABLE) {
    for (int i = 0; i < plugins.u.tab.size; i++) {
      if (!valid_entry(plugins.u.tab.key[i], plugins.u.tab.value[i])) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid plugin entry in ppm lockfile; it was not modified.");
        return NULL;
      }
    }
  }
  return g_steal_pointer(&lock);
}

static char *
table_string(toml_datum_t table, const char *key)
{
  toml_datum_t value = toml_get(table, key);
  return value.type == TOML_STRING ? g_strdup(value.u.s) : NULL;
}

static PumpkinPluginMetadata *
metadata_from_table(toml_datum_t table)
{
  if (table.type != TOML_TABLE) {
    return NULL;
  }
  PumpkinPluginMetadata *metadata = g_new0(PumpkinPluginMetadata, 1);
  toml_datum_t id = toml_get(table, "id");
  metadata->id = id.type == TOML_INT64 ? id.u.int64 : 0;
  metadata->public_id = table_string(table, "public_id");
  metadata->name = table_string(table, "name");
  metadata->author = table_string(table, "author");
  metadata->version = table_string(table, "version");
  metadata->file = table_string(table, "file");
  metadata->updated_at = table_string(table, "updated_at");
  return metadata;
}

static gboolean
identity_matches(const PumpkinPluginMetadata *metadata, gint64 id, const char *public_id)
{
  if (public_id != NULL && *public_id != '\0' &&
      metadata->public_id != NULL && *metadata->public_id != '\0') {
    return g_str_equal(public_id, metadata->public_id);
  }
  return id > 0 && metadata->id == id;
}

static PumpkinPluginMetadata *
lookup_with_lock(const char *directory, const char *file, const PluginLock *lock)
{
  if (lock != NULL) {
    toml_datum_t plugins = toml_get(lock->parsed.toptab, "plugins");
    if (plugins.type == TOML_TABLE) {
      for (int i = 0; i < plugins.u.tab.size; i++) {
        g_autoptr(PumpkinPluginMetadata) metadata = metadata_from_table(plugins.u.tab.value[i]);
        if (metadata != NULL && safe_filename(metadata->file) && same_file(metadata->file, file)) {
          g_free(metadata->file);
          metadata->file = g_strdup(file);
          return g_steal_pointer(&metadata);
        }
      }
    }
  }
  g_autofree char *base = active_filename(file);
  g_autofree char *sidecar = g_strconcat(base, ".marketplace", NULL);
  g_autofree char *path = g_build_filename(directory, sidecar, NULL);
  g_autoptr(GKeyFile) keyfile = g_key_file_new();
  if (!g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, NULL)) {
    return NULL;
  }
  PumpkinPluginMetadata *metadata = g_new0(PumpkinPluginMetadata, 1);
  metadata->id = g_key_file_get_int64(keyfile, "marketplace", "id", NULL);
  metadata->public_id = g_key_file_get_string(keyfile, "marketplace", "public_id", NULL);
  metadata->name = g_key_file_get_string(keyfile, "marketplace", "name", NULL);
  metadata->version = g_key_file_get_string(keyfile, "marketplace", "version", NULL);
  metadata->author = g_key_file_get_string(keyfile, "marketplace", "author", NULL);
  metadata->file = g_strdup(file);
  return metadata;
}

PumpkinPluginMetadata *
pumpkin_plugin_store_lookup(const char *directory, const char *file)
{
  g_return_val_if_fail(directory != NULL && file != NULL, NULL);
  g_autoptr(PluginLock) lock = load_lock(directory, NULL);
  return lookup_with_lock(directory, file, lock);
}

gboolean
pumpkin_plugin_store_validate(const char *directory, GError **error)
{
  g_autoptr(PluginLock) lock = load_lock(directory, error);
  return lock != NULL;
}

PumpkinPluginMetadata *
pumpkin_plugin_store_find(const char *directory, gint64 id, const char *public_id, GError **error)
{
  g_autoptr(PluginLock) lock = load_lock(directory, error);
  if (lock == NULL) return NULL;
  g_autoptr(GDir) dir = g_dir_open(directory, 0, NULL);
  if (dir == NULL) {
    return NULL;
  }
  g_autoptr(PumpkinPluginMetadata) found = NULL;
  g_autofree char *legacy = g_strdup_printf("market-%" G_GINT64_FORMAT ".wasm", id);
  const char *entry;
  while ((entry = g_dir_read_name(dir)) != NULL) {
    g_autofree char *base = active_filename(entry);
    if (!g_str_has_suffix(base, ".wasm") && !g_str_has_suffix(base, ".so")) {
      continue;
    }
    g_autofree char *path = g_build_filename(directory, entry, NULL);
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
      continue;
    }
    g_autoptr(PumpkinPluginMetadata) metadata = lookup_with_lock(directory, entry, lock);
    if (metadata == NULL && id > 0 && g_str_equal(base, legacy)) {
      metadata = g_new0(PumpkinPluginMetadata, 1);
      metadata->id = id;
      metadata->file = g_strdup(entry);
    }
    if (metadata == NULL || !identity_matches(metadata, id, public_id)) {
      continue;
    }
    if (found != NULL) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                          "Multiple installed files belong to this Marketplace plugin. Remove the duplicate before updating.");
      return NULL;
    }
    found = g_steal_pointer(&metadata);
  }
  return g_steal_pointer(&found);
}

static void
append_string(GString *out, const char *value)
{
  g_string_append_c(out, '"');
  for (const unsigned char *p = (const unsigned char *)(value != NULL ? value : ""); *p != '\0'; p++) {
    switch (*p) {
      case '"': g_string_append(out, "\\\""); break;
      case '\\': g_string_append(out, "\\\\"); break;
      case '\n': g_string_append(out, "\\n"); break;
      case '\r': g_string_append(out, "\\r"); break;
      case '\t': g_string_append(out, "\\t"); break;
      default:
        if (*p < 0x20 || *p == 0x7f) {
          g_string_append_printf(out, "\\u%04x", *p);
        } else {
          g_string_append_c(out, (char)*p);
        }
    }
  }
  g_string_append_c(out, '"');
}

/* Preserve unknown TOML values instead of dropping another tool's metadata. */
static void
append_value(GString *out, toml_datum_t value)
{
  switch (value.type) {
    case TOML_STRING: append_string(out, value.u.s); break;
    case TOML_INT64: g_string_append_printf(out, "%" G_GINT64_FORMAT, (gint64)value.u.int64); break;
    case TOML_BOOLEAN: g_string_append(out, value.u.boolean ? "true" : "false"); break;
    case TOML_FP64: {
      char buffer[G_ASCII_DTOSTR_BUF_SIZE];
      g_ascii_dtostr(buffer, sizeof(buffer), value.u.fp64);
      g_string_append(out, buffer);
      if (isfinite(value.u.fp64) && strpbrk(buffer, ".eE") == NULL) {
        g_string_append(out, ".0");
      }
      break;
    }
    case TOML_ARRAY:
      g_string_append_c(out, '[');
      for (int i = 0; i < value.u.arr.size; i++) {
        if (i > 0) g_string_append(out, ", ");
        append_value(out, value.u.arr.elem[i]);
      }
      g_string_append_c(out, ']');
      break;
    case TOML_TABLE:
      g_string_append(out, "{ ");
      for (int i = 0; i < value.u.tab.size; i++) {
        if (i > 0) g_string_append(out, ", ");
        append_string(out, value.u.tab.key[i]);
        g_string_append(out, " = ");
        append_value(out, value.u.tab.value[i]);
      }
      g_string_append(out, " }");
      break;
    case TOML_DATE:
    case TOML_DATETIME:
    case TOML_DATETIMETZ:
      g_string_append_printf(out, "%04d-%02d-%02d", value.u.ts.year, value.u.ts.month, value.u.ts.day);
      if (value.type == TOML_DATE) break;
      g_string_append_c(out, 'T');
      G_GNUC_FALLTHROUGH;
    case TOML_TIME:
      g_string_append_printf(out, "%02d:%02d:%02d.%06d", value.u.ts.hour,
                             value.u.ts.minute, value.u.ts.second, value.u.ts.usec);
      if (value.type == TOML_DATETIMETZ) {
        int offset = value.u.ts.tz;
        g_string_append_printf(out, "%c%02d:%02d", offset < 0 ? '-' : '+',
                               ABS(offset) / 60, ABS(offset) % 60);
      }
      break;
    case TOML_UNKNOWN: g_assert_not_reached();
  }
}

static void
append_field(GString *out, const char *key, const char *value)
{
  append_string(out, key);
  g_string_append(out, " = ");
  append_string(out, value);
}

static void
append_metadata(GString *out, const PumpkinPluginMetadata *metadata, toml_datum_t previous)
{
  g_string_append_printf(out, "{ id = %" G_GINT64_FORMAT ", ", metadata->id);
  append_field(out, "public_id", metadata->public_id);
  g_string_append(out, ", "); append_field(out, "name", metadata->name);
  g_string_append(out, ", "); append_field(out, "author", metadata->author);
  g_string_append(out, ", "); append_field(out, "file", metadata->file);
  if (metadata->version != NULL) {
    g_string_append(out, ", "); append_field(out, "version", metadata->version);
  }
  if (metadata->updated_at != NULL) {
    g_string_append(out, ", "); append_field(out, "updated_at", metadata->updated_at);
  }
  if (previous.type == TOML_TABLE) {
    for (int i = 0; i < previous.u.tab.size; i++) {
      const char *key = previous.u.tab.key[i];
      if (strcmp(key, "id") == 0 || strcmp(key, "public_id") == 0 || strcmp(key, "name") == 0 ||
          strcmp(key, "author") == 0 || strcmp(key, "file") == 0 || strcmp(key, "version") == 0 ||
          strcmp(key, "updated_at") == 0) {
        continue;
      }
      g_string_append(out, ", ");
      append_string(out, key);
      g_string_append(out, " = ");
      append_value(out, previous.u.tab.value[i]);
    }
  }
  g_string_append(out, " }");
}

typedef enum { STORE_RECORD, STORE_RENAME, STORE_REMOVE } StoreOperation;

static gboolean
modify_lock(const char *directory, StoreOperation operation,
            const PumpkinPluginMetadata *metadata, const char *from, const char *to, GError **error)
{
  g_autoptr(PluginLock) lock = load_lock(directory, error);
  if (lock == NULL) return FALSE;
  if (!lock->exists && operation != STORE_RECORD) return TRUE;
  toml_datum_t root = lock->parsed.toptab;
  toml_datum_t plugins = toml_get(root, "plugins");
  g_autoptr(GString) output = g_string_new("# Managed by ppm and Smashed Pumpkin.\n");
  for (int i = 0; i < root.u.tab.size; i++) {
    if (strcmp(root.u.tab.key[i], "plugins") == 0) continue;
    append_string(output, root.u.tab.key[i]);
    g_string_append(output, " = ");
    append_value(output, root.u.tab.value[i]);
    g_string_append_c(output, '\n');
  }
  g_string_append(output, "\n[plugins]\n");
  gboolean changed = FALSE;
  if (plugins.type == TOML_TABLE) {
    for (int i = 0; i < plugins.u.tab.size; i++) {
      toml_datum_t table = plugins.u.tab.value[i];
      g_autoptr(PumpkinPluginMetadata) existing = metadata_from_table(table);
      gboolean match = existing != NULL && (operation == STORE_RECORD
                        ? identity_matches(existing, metadata->id, metadata->public_id)
                        : same_file(existing->file, from));
      if (match && changed) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Duplicate entries in ppm lockfile; it was not modified.");
        return FALSE;
      }
      if (match) changed = TRUE;
      if (match && operation == STORE_REMOVE) continue;
      const char *key = match && operation == STORE_RECORD ? metadata->public_id : plugins.u.tab.key[i];
      append_string(output, key);
      g_string_append(output, " = ");
      if (match && operation == STORE_RECORD) {
        append_metadata(output, metadata, table);
      } else if (match && operation == STORE_RENAME) {
        g_free(existing->file);
        existing->file = g_strdup(to);
        append_metadata(output, existing, table);
      } else {
        if (operation == STORE_RECORD && (g_str_equal(key, metadata->public_id) ||
            (existing != NULL && same_file(existing->file, metadata->file)))) {
          g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "The ppm lockfile associates this file with another plugin.");
          return FALSE;
        }
        append_value(output, table);
      }
      g_string_append_c(output, '\n');
    }
  }
  if (operation == STORE_RECORD && !changed) {
    append_string(output, metadata->public_id);
    g_string_append(output, " = ");
    append_metadata(output, metadata, (toml_datum_t){0});
    g_string_append_c(output, '\n');
    changed = TRUE;
  }
  if (!changed) return TRUE;
  /* Catch serialization mistakes before touching the user's lockfile. */
  toml_result_t check = toml_parse(output->str, (int)output->len);
  gboolean valid = check.ok;
  toml_free(check);
  if (!valid) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Could not serialize ppm lockfile; it was not modified.");
    return FALSE;
  }
  return g_file_replace_contents(lock->file, output->str, output->len, lock->etag, FALSE,
                                 G_FILE_CREATE_PRIVATE, NULL, NULL, error);
}

gboolean
pumpkin_plugin_store_record(const char *directory, const PumpkinPluginMetadata *metadata, GError **error)
{
  if (metadata == NULL || metadata->id <= 0 || metadata->public_id == NULL ||
      *metadata->public_id == '\0' || !safe_filename(metadata->file)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Marketplace metadata is missing a stable ID or safe filename.");
    return FALSE;
  }
  return modify_lock(directory, STORE_RECORD, metadata, NULL, NULL, error);
}

gboolean
pumpkin_plugin_store_rename(const char *directory, const char *from, const char *to, GError **error)
{
  if (!safe_filename(from) || !safe_filename(to)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid plugin filename.");
    return FALSE;
  }
  return modify_lock(directory, STORE_RENAME, NULL, from, to, error);
}

gboolean
pumpkin_plugin_store_remove(const char *directory, const char *file, GError **error)
{
  return modify_lock(directory, STORE_REMOVE, NULL, file, NULL, error);
}
