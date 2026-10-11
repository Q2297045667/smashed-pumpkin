#include "minimap-store.h"
#include "smashed-pumpkin-resources.h"
#include "vendor/cjson/cJSON.h"
#include "vendor/tomlc17/tomlc17.h"
#include <glib/gstdio.h>
#include <string.h>

static const char *bridge_folder(const char *data_dir)
{
  g_autofree char *canonical = g_build_filename(data_dir, "plugins", PUMPKIN_MAP_BRIDGE_FILENAME, NULL);
  g_autofree char *parked = g_strconcat(canonical, ".disabled", NULL);
  return g_file_test(canonical, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK) ||
    g_file_test(parked, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK) ? "map-bridge" : "smashed-map-bridge";
}
static char *bridge_path(const char *data_dir, const char *file)
{
  return g_build_filename(data_dir, "plugins", "data", bridge_folder(data_dir), file, NULL);
}
gboolean pumpkin_map_is_internal_plugin(const char *filename)
{
  if (filename == NULL) return FALSE;
  g_autofree char *name = g_strdup(filename);
  if (g_str_has_suffix(name, ".disabled")) name[strlen(name) - strlen(".disabled")] = '\0';
  else if (g_str_has_suffix(name, ".deactivated")) name[strlen(name) - strlen(".deactivated")] = '\0';
  return g_strcmp0(name, PUMPKIN_MAP_BRIDGE_FILENAME) == 0 || g_strcmp0(name, "smashed-map-bridge.wasm") == 0;
}
gboolean pumpkin_map_bridge_present(const char *data_dir)
{
  g_autofree char *canonical = g_build_filename(data_dir, "plugins", PUMPKIN_MAP_BRIDGE_FILENAME, NULL);
  g_autofree char *legacy = g_build_filename(data_dir, "plugins", "smashed-map-bridge.wasm", NULL);
  return g_file_test(canonical, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK) ||
    g_file_test(legacy, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK);
}
static char *installed_bridge_path(const char *data_dir, GError **error)
{
  g_autofree char *canonical = g_build_filename(data_dir, "plugins", PUMPKIN_MAP_BRIDGE_FILENAME, NULL);
  g_autofree char *legacy = g_build_filename(data_dir, "plugins", "smashed-map-bridge.wasm", NULL);
  gboolean has_current = g_file_test(canonical, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK);
  gboolean has_legacy = g_file_test(legacy, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK);
  if (has_current && has_legacy) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
      "Both current and legacy Map Bridge files exist; resolve the duplicate before updating"); return NULL;
  }
  return has_current ? g_steal_pointer(&canonical) : g_steal_pointer(&legacy);
}
static gboolean safe_file(const char *path, GFileType type, GError **error)
{
  g_autoptr(GFile) file = g_file_new_for_path(path);
  g_autoptr(GFileInfo) info = g_file_query_info(file, "standard::type,standard::is-symlink", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
  if (info == NULL) return FALSE;
  if (g_file_info_get_is_symlink(info) || g_file_info_get_file_type(info) != type) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Map paths must be ordinary local files and directories");
    return FALSE;
  }
  return TRUE;
}
static gboolean safe_bridge(const char *data_dir, GError **error)
{
  const char *parts[] = {"", "plugins", "plugins/data"};
  for (guint i = 0; i < G_N_ELEMENTS(parts); i++) {
    g_autofree char *path = g_build_filename(data_dir, parts[i], NULL);
    if (!safe_file(path, G_FILE_TYPE_DIRECTORY, error)) return FALSE;
  }
  g_autofree char *folder = bridge_path(data_dir, "");
  return safe_file(folder, G_FILE_TYPE_DIRECTORY, error);
}
PumpkinMapSnapshot *pumpkin_map_read_live(const char *data_dir, const char *request, gint64 now_ms, GError **error)
{
  if (!safe_bridge(data_dir, error)) return NULL;
  g_autofree char *path = bridge_path(data_dir, "live.json");
  if (!safe_file(path, G_FILE_TYPE_REGULAR, error)) return NULL;
  g_autoptr(GFile) file = g_file_new_for_path(path);
  g_autoptr(GFileInputStream) stream = g_file_read(file, NULL, error);
  if (stream == NULL) return NULL;
  g_autofree char *bytes = g_malloc(1024 * 1024 + 2);
  gsize count = 0;
  if (!g_input_stream_read_all(G_INPUT_STREAM(stream), bytes, 1024 * 1024 + 1, &count, NULL, error)) return NULL;
  bytes[count] = '\0';
  if (count > 1024 * 1024 || memchr(bytes, '\0', count) != NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Map Bridge snapshot is oversized or binary");
    return NULL;
  }
  return pumpkin_map_snapshot_parse(bytes, request, now_ms, error);
}
gboolean pumpkin_map_write_request(const char *data_dir, const char *json, GError **error)
{
  return pumpkin_map_write_request_cancellable(data_dir, json, NULL, error);
}
gboolean pumpkin_map_write_request_cancellable(const char *data_dir, const char *json,
                                               GCancellable *cancel, GError **error)
{
  if (cancel != NULL && g_cancellable_set_error_if_cancelled(cancel, error)) return FALSE;
  if (!safe_bridge(data_dir, error)) return FALSE;
  g_autofree char *path = bridge_path(data_dir, "request.json");
  if (g_file_test(path, G_FILE_TEST_EXISTS) && !safe_file(path, G_FILE_TYPE_REGULAR, error)) return FALSE;
  g_autoptr(GFile) file = g_file_new_for_path(path);
  return g_file_replace_contents(file, json, strlen(json), NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, cancel, error);
}

static GBytes *bounded_file(const char *path, gsize limit, char **etag, GError **error)
{
  if (!safe_file(path, G_FILE_TYPE_REGULAR, error)) return NULL;
  g_autoptr(GFile) file = g_file_new_for_path(path);
  g_autoptr(GFileInputStream) stream = g_file_read(file, NULL, error);
  if (stream == NULL) return NULL;
  if (etag != NULL) {
    g_autoptr(GFileInfo) info = g_file_input_stream_query_info(stream, G_FILE_ATTRIBUTE_ETAG_VALUE, NULL, error);
    if (info == NULL) return NULL;
    *etag = g_strdup(g_file_info_get_etag(info));
  }
  g_autofree guint8 *bytes = g_malloc(limit + 1); gsize size = 0;
  if (!g_input_stream_read_all(G_INPUT_STREAM(stream), bytes, limit + 1, &size, NULL, error)) return NULL;
  if (size > limit) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Map Bridge file is oversized"); return NULL;
  }
  return g_bytes_new_take(g_steal_pointer(&bytes), size);
}
static char *verified_bridge(const char *data_dir, GBytes *bundled, GError **error)
{
  g_autofree char *folder = g_build_filename(data_dir, "plugins", NULL);
  if (!safe_file(data_dir, G_FILE_TYPE_DIRECTORY, error) || !safe_file(folder, G_FILE_TYPE_DIRECTORY, error)) return NULL;
  g_autofree char *path = installed_bridge_path(data_dir, error);
  if (path == NULL) return NULL;
  g_autoptr(GBytes) installed = bounded_file(path, 2 * 1024 * 1024, NULL, error);
  if (installed == NULL) return NULL;
  g_autofree char *name = g_path_get_basename(path);
  if (bundled == NULL || !g_bytes_equal(installed, bundled) || g_strcmp0(name, PUMPKIN_MAP_BRIDGE_FILENAME) != 0) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
      "This Map Bridge does not match the bundled plugin; automatic permission management is unavailable"); return NULL;
  }
  gsize size; const guint8 *bytes = g_bytes_get_data(installed, &size);
  return g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes, size);
}
static gboolean data_only_release(const char *hash)
{
  /* Add a release here only after verifying its exact requested permissions. */
  if (g_strcmp0(hash, "538a4d9a01d74e20c35878184c8c6f9672bc9d2f383cb02b7c08adbdfaae7713") == 0 ||
    g_strcmp0(hash, "388d4c9a89cf9237d0dc47cedf854fdf7ae06b9861a64ebf1286ddcab1d909a1") == 0 ||
    g_strcmp0(hash, "85010387416105c03fe3bebf018f0b7455e0fb90ed7a2f63a663bc013aaaa26b") == 0 ||
    g_strcmp0(hash, "99a788aa6a9ef7f41112ef5773c87e4f4fedd8836358801832ab8daaaca92194") == 0) return TRUE;
  /* Compiler versions can change the binary; trust only this build's own resource. */
  g_autoptr(GBytes) bundled = g_resource_lookup_data(smashed_pumpkin_get_resource(),
    "/dev/rotstein/SmashedPumpkin/plugins/map-bridge.wasm", G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
  if (bundled == NULL) return FALSE;
  g_autofree char *bundled_hash = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, bundled);
  return g_strcmp0(hash, bundled_hash) == 0;
}
gboolean pumpkin_map_bridge_set_enabled(const char *data_dir, gboolean enabled,
                                         char **plugin_name, gboolean *changed, GError **error)
{
  if (plugin_name != NULL) *plugin_name = NULL;
  if (changed != NULL) *changed = FALSE;
  const char *names[] = {PUMPKIN_MAP_BRIDGE_FILENAME, "smashed-map-bridge.wasm"};
  g_autofree char *installed = NULL, *destination = NULL;
  gboolean active = FALSE;
  for (guint i = 0; i < G_N_ELEMENTS(names); ++i) {
    g_autofree char *path = g_build_filename(data_dir, "plugins", names[i], NULL);
    g_autofree char *disabled = g_strconcat(path, ".disabled", NULL);
    const char *paths[] = {path, disabled};
    for (guint j = 0; j < G_N_ELEMENTS(paths); ++j) {
      if (!g_file_test(paths[j], G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)) continue;
      if (installed != NULL) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
          "Multiple active or disabled Map Bridge files exist; no files have been changed"); return FALSE;
      }
      installed = g_strdup(paths[j]); active = j == 0;
      destination = g_strdup(enabled ? path : disabled);
    }
  }
  if (installed == NULL) return TRUE;
  g_autofree char *folder = g_build_filename(data_dir, "plugins", NULL);
  if (!safe_file(data_dir, G_FILE_TYPE_DIRECTORY, error) || !safe_file(folder, G_FILE_TYPE_DIRECTORY, error)) return FALSE;
  g_autoptr(GBytes) bytes = bounded_file(installed, 2 * 1024 * 1024, NULL, error);
  if (bytes == NULL) return FALSE;
  gsize size; const guint8 *contents = g_bytes_get_data(bytes, &size);
  g_autofree char *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, contents, size);
  if (!data_only_release(hash)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
      "This Map Bridge is not a verified bundled release; it has not been changed"); return FALSE;
  }
  if (active != enabled) {
    g_autoptr(GFile) source = g_file_new_for_path(installed), target = g_file_new_for_path(destination);
    if (!g_file_move(source, target, G_FILE_COPY_NO_FALLBACK_FOR_MOVE, NULL, NULL, NULL, error)) return FALSE;
    if (changed != NULL) *changed = TRUE;
  }
  if (plugin_name != NULL) *plugin_name = g_strdup(
    g_strcmp0(hash, "538a4d9a01d74e20c35878184c8c6f9672bc9d2f383cb02b7c08adbdfaae7713") == 0 ||
    g_strcmp0(hash, "388d4c9a89cf9237d0dc47cedf854fdf7ae06b9861a64ebf1286ddcab1d909a1") == 0 ?
      "smashed-map-bridge" : "map-bridge");
  return TRUE;
}
gboolean pumpkin_map_stop_request(const char *data_dir, const char *request, GError **error)
{
  if (!safe_bridge(data_dir, NULL)) return TRUE;
  g_autofree char *path = bridge_path(data_dir, "request.json"), *etag = NULL;
  g_autoptr(GError) local = NULL;
  g_autoptr(GBytes) bytes = bounded_file(path, 8192, &etag, &local);
  if (bytes == NULL) {
    if (g_error_matches(local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) return TRUE;
    g_propagate_error(error, g_steal_pointer(&local)); return FALSE;
  }
  gsize size; const char *contents = g_bytes_get_data(bytes, &size);
  if (size == 0) return TRUE;
  g_autofree char *json = g_strndup(contents, size);
  cJSON *root = memchr(contents, '\0', size) == NULL ? cJSON_ParseWithOpts(json, NULL, TRUE) : NULL;
  cJSON *nonce = cJSON_GetObjectItemCaseSensitive(root, "request");
  gboolean matches = cJSON_IsString(nonce) && g_strcmp0(nonce->valuestring, request) == 0;
  cJSON_Delete(root);
  if (!matches) return TRUE;
  g_autoptr(GFile) file = g_file_new_for_path(path);
  /* Empty contents revoke this lease. Its ETag prevents overwriting a newly
   * enabled session's request while a cancelled worker finishes. */
  return g_file_replace_contents(file, "", 0, etag, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, error);
}
static gboolean previous_bridge(const char *data_dir, char **etag, char **previous_hash,
                                 char **installed_path, GError **error)
{
  g_autofree char *folder = g_build_filename(data_dir, "plugins", NULL);
  if (!safe_file(data_dir, G_FILE_TYPE_DIRECTORY, error) || !safe_file(folder, G_FILE_TYPE_DIRECTORY, error)) return FALSE;
  g_autofree char *path = installed_bridge_path(data_dir, error);
  if (path == NULL) return FALSE;
  g_autoptr(GBytes) bytes = bounded_file(path, 2 * 1024 * 1024, etag, error);
  if (bytes == NULL) return FALSE;
  gsize size; const guint8 *contents = g_bytes_get_data(bytes, &size);
  g_autofree char *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, contents, size);
  /* Only the previously shipped artifact is eligible, never an unknown local build. */
  if (data_only_release(hash)) {
    if (previous_hash != NULL) *previous_hash = g_steal_pointer(&hash);
    if (installed_path != NULL) *installed_path = g_steal_pointer(&path);
    return TRUE;
  }
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
    "The installed Map Bridge is not a recognized previous version; it has not been changed");
  return FALSE;
}
static gboolean carry_bridge_decision(const char *data_dir, const char *old_hash,
                                      GBytes *bundled, GError **error);
static gboolean check_legacy_policy(const char *data_dir, const char *old_hash, GError **error)
{
  if (g_strcmp0(old_hash, "85010387416105c03fe3bebf018f0b7455e0fb90ed7a2f63a663bc013aaaa26b") == 0) return TRUE;
  const char *configs[] = {"pumpkin.toml", "config/features.toml", "config/configuration.toml"};
  for (guint i = 0; i < G_N_ELEMENTS(configs); ++i) {
    g_autofree char *path = g_build_filename(data_dir, configs[i], NULL);
    if (!g_file_test(path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)) continue;
    g_autoptr(GBytes) bytes = bounded_file(path, 1024 * 1024, NULL, error);
    if (bytes == NULL) return FALSE;
    gsize size; const char *contents = g_bytes_get_data(bytes, &size);
    toml_result_t parsed = toml_parse(contents, (int)size);
    if (!parsed.ok) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
        "Cannot check Map Bridge overrides in %s: %s", configs[i], parsed.errmsg);
      toml_free(parsed); return FALSE;
    }
    toml_datum_t plugins = toml_get(parsed.toptab, "plugins");
    toml_datum_t overrides = plugins.type == TOML_TABLE ? toml_get(plugins, "overrides") : (toml_datum_t){0};
    toml_datum_t legacy = overrides.type == TOML_TABLE ? toml_get(overrides, "smashed-map-bridge") : (toml_datum_t){0};
    gboolean configured = legacy.type != TOML_UNKNOWN;
    toml_free(parsed);
    if (configured) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
        "Review the custom smashed-map-bridge override in %s for the new map-bridge name before updating; policy has not been changed", configs[i]);
      return FALSE;
    }
  }
  return TRUE;
}
gboolean pumpkin_map_bridge_upgrade(const char *data_dir, GBytes *bundled, GError **error)
{
  g_autofree char *etag = NULL, *old_hash = NULL, *installed_path = NULL;
  if (!previous_bridge(data_dir, &etag, &old_hash, &installed_path, error)) return FALSE;
  if (etag == NULL || bundled == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Cannot safely update Map Bridge without a file revision and bundled binary"); return FALSE;
  }
  if (!check_legacy_policy(data_dir, old_hash, error)) return FALSE;
  g_autofree char *path = g_build_filename(data_dir, "plugins", PUMPKIN_MAP_BRIDGE_FILENAME, NULL);
  g_autoptr(GFile) installed_file = g_file_new_for_path(installed_path);
  g_autoptr(GFile) file = g_file_new_for_path(path);
  gsize size; const char *bytes = g_bytes_get_data(bundled, &size);
  /* Persist only an equivalent existing decision before committing the binary.
   * A failed ETag replacement may leave an unused hash entry, never a new grant. */
  if (!carry_bridge_decision(data_dir, old_hash, bundled, error)) return FALSE;
  gboolean renamed = g_strcmp0(path, installed_path) != 0;
  if (renamed && !g_file_move(installed_file, file, G_FILE_COPY_NO_FALLBACK_FOR_MOVE, NULL, NULL, NULL, error)) return FALSE;
  gboolean ok = g_file_replace_contents(file, bytes, size, etag, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, error);
  if (!ok && renamed) g_file_move(file, installed_file, G_FILE_COPY_NO_FALLBACK_FOR_MOVE, NULL, NULL, NULL, NULL);
  return ok;
}
static cJSON *permission_cache(const char *path, char **etag, GError **error)
{
  g_autoptr(GBytes) bytes = bounded_file(path, 1024 * 1024, etag, error);
  if (bytes == NULL) return NULL;
  gsize size; const char *contents = g_bytes_get_data(bytes, &size);
  g_autofree char *json = g_strndup(contents, size);
  cJSON *root = size != 0 && memchr(contents, '\0', size) == NULL ? cJSON_ParseWithOpts(json, NULL, TRUE) : NULL;
  if (!cJSON_IsObject(root) || !cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root, "entries"))) {
    cJSON_Delete(root);
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
      "Pumpkin's permission cache is invalid; it has not been changed"); return NULL;
  }
  g_autoptr(GHashTable) names = g_hash_table_new(g_str_hash, g_str_equal);
  cJSON *entries = cJSON_GetObjectItemCaseSensitive(root, "entries"), *entry;
  gboolean valid = TRUE;
  cJSON_ArrayForEach(entry, entries) {
    cJSON *scopes = cJSON_GetObjectItemCaseSensitive(entry, "permissions_requested");
    if (entry->string == NULL || !g_hash_table_add(names, entry->string) || !cJSON_IsObject(entry) ||
        !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(entry, "approved")) || !cJSON_IsArray(scopes)) { valid = FALSE; break; }
    cJSON *scope;
    cJSON_ArrayForEach(scope, scopes) if (!cJSON_IsString(scope)) { valid = FALSE; break; }
    if (!valid) break;
  }
  if (!valid) {
    cJSON_Delete(root); g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
      "Pumpkin's permission cache contains invalid or duplicate decisions; it has not been changed"); return NULL;
  }
  return root;
}
static gboolean data_scope(cJSON *entry)
{
  cJSON *scopes = cJSON_GetObjectItemCaseSensitive(entry, "permissions_requested");
  cJSON *scope = cJSON_GetArrayItem(scopes, 0);
  return cJSON_IsArray(scopes) && cJSON_GetArraySize(scopes) == 1 && cJSON_IsString(scope) &&
    g_strcmp0(scope->valuestring, "fs.write.data") == 0;
}
static gboolean carry_bridge_decision(const char *data_dir, const char *old_hash,
                                      GBytes *bundled, GError **error)
{
  g_autofree char *path = g_build_filename(data_dir, "plugins", "permission_cache.json", NULL);
  g_autofree char *etag = NULL;
  g_autoptr(GError) local = NULL;
  cJSON *root = permission_cache(path, &etag, &local);
  if (root == NULL) {
    if (g_error_matches(local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) return TRUE;
    g_propagate_error(error, g_steal_pointer(&local)); return FALSE;
  }
  gsize size;
  const guint8 *bytes = g_bytes_get_data(bundled, &size);
  g_autofree char *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes, size);
  cJSON *entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
  cJSON *old = cJSON_GetObjectItemCaseSensitive(entries, old_hash);
  /* Both recognized releases and the bundled bridge request only fs.write.data.
   * Preserve explicit refusals too; never override a decision for the new hash. */
  if (!data_only_release(hash) || !data_scope(old) || cJSON_GetObjectItemCaseSensitive(entries, hash) != NULL) {
    cJSON_Delete(root); return TRUE;
  }
  if (etag == NULL) {
    cJSON_Delete(root);
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
      "Cannot safely preserve the Bridge permission decision without a file revision"); return FALSE;
  }
  cJSON_AddItemToObject(entries, hash, cJSON_Duplicate(old, TRUE));
  char *json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
  if (json == NULL) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Cannot serialize plugin permission"); return FALSE;
  }
  g_autoptr(GFile) file = g_file_new_for_path(path);
  gboolean ok = g_file_replace_contents(file, json, strlen(json), etag, FALSE,
    G_FILE_CREATE_PRIVATE, NULL, NULL, error);
  cJSON_free(json); return ok;
}
PumpkinMapBridgeAccess pumpkin_map_bridge_access(const char *data_dir, GBytes *bundled, GError **error)
{
  g_autoptr(GError) local = NULL;
  g_autofree char *hash = verified_bridge(data_dir, bundled, &local);
  if (hash == NULL) {
    PumpkinMapBridgeAccess state = g_error_matches(local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ?
      PUMPKIN_MAP_BRIDGE_MISSING : PUMPKIN_MAP_BRIDGE_UNTRUSTED;
    if (state == PUMPKIN_MAP_BRIDGE_UNTRUSTED && previous_bridge(data_dir, NULL, NULL, NULL, NULL)) {
      g_clear_error(&local); return PUMPKIN_MAP_BRIDGE_OUTDATED;
    }
    g_propagate_error(error, g_steal_pointer(&local)); return state;
  }
  g_autofree char *path = g_build_filename(data_dir, "plugins", "permission_cache.json", NULL);
  cJSON *root = permission_cache(path, NULL, &local);
  if (root == NULL) {
    PumpkinMapBridgeAccess state = g_error_matches(local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ?
      PUMPKIN_MAP_BRIDGE_UNKNOWN : PUMPKIN_MAP_BRIDGE_INVALID;
    g_propagate_error(error, g_steal_pointer(&local)); return state;
  }
  cJSON *entry = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "entries"), hash);
  cJSON *approved = cJSON_GetObjectItemCaseSensitive(entry, "approved");
  PumpkinMapBridgeAccess state = data_scope(entry) && cJSON_IsBool(approved) ?
    (cJSON_IsTrue(approved) ? PUMPKIN_MAP_BRIDGE_APPROVED : PUMPKIN_MAP_BRIDGE_DENIED) : PUMPKIN_MAP_BRIDGE_UNKNOWN;
  cJSON_Delete(root); return state;
}
gboolean pumpkin_map_bridge_approve(const char *data_dir, GBytes *bundled, GError **error)
{
  g_autofree char *hash = verified_bridge(data_dir, bundled, error);
  if (hash == NULL) return FALSE;
  g_autofree char *path = g_build_filename(data_dir, "plugins", "permission_cache.json", NULL);
  g_autofree char *etag = NULL;
  g_autoptr(GError) read_error = NULL;
  cJSON *root = permission_cache(path, &etag, &read_error);
  gboolean missing = g_error_matches(read_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  if (root == NULL && !missing) { g_propagate_error(error, g_steal_pointer(&read_error)); return FALSE; }
  if (missing) { root = cJSON_CreateObject(); cJSON_AddObjectToObject(root, "entries"); }
  else if (etag == NULL) {
    cJSON_Delete(root); g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
      "Cannot safely update this permission cache without a file revision"); return FALSE;
  }
  cJSON *entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
  cJSON_DeleteItemFromObjectCaseSensitive(entries, hash);
  cJSON *entry = cJSON_AddObjectToObject(entries, hash);
  cJSON *scopes = cJSON_AddArrayToObject(entry, "permissions_requested");
  cJSON_AddItemToArray(scopes, cJSON_CreateString("fs.write.data")); cJSON_AddBoolToObject(entry, "approved", TRUE);
  char *json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
  if (json == NULL) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Cannot serialize plugin permission"); return FALSE; }
  g_autoptr(GFile) file = g_file_new_for_path(path);
  gboolean ok;
  if (missing) {
    g_autoptr(GFileOutputStream) stream = g_file_create(file, G_FILE_CREATE_PRIVATE, NULL, error);
    ok = stream != NULL && g_output_stream_write_all(G_OUTPUT_STREAM(stream), json, strlen(json), NULL, NULL, error) &&
      g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, error);
    if (!ok && stream != NULL) { g_output_stream_close(G_OUTPUT_STREAM(stream), NULL, NULL); g_file_delete(file, NULL, NULL); }
  } else ok = g_file_replace_contents(file, json, strlen(json), etag, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, error);
  cJSON_free(json); return ok;
}

typedef struct { GPtrArray *worlds, *watched; guint directories, files; GCancellable *cancel; } Scan;

static PumpkinMapWorld *region_world(const char *region)
{
  g_autofree char *dimension_dir = g_path_get_dirname(region);
  g_autofree char *last = g_path_get_basename(dimension_dir);
  g_autofree char *root = NULL;
  char *dimension = NULL;
  g_autofree char *namespace_dir = g_path_get_dirname(dimension_dir);
  g_autofree char *namespace = g_path_get_basename(namespace_dir);
  g_autofree char *dimensions_dir = g_path_get_dirname(namespace_dir);
  g_autofree char *dimensions = g_path_get_basename(dimensions_dir);
  if (g_strcmp0(dimensions, "dimensions") == 0) {
    root = g_path_get_dirname(dimensions_dir);
    dimension = g_strdup_printf("%s:%s", namespace, last);
  } else if (g_strcmp0(last, "DIM-1") == 0 || g_strcmp0(last, "DIM1") == 0) {
    root = g_path_get_dirname(dimension_dir);
    dimension = g_strdup(g_strcmp0(last, "DIM-1") == 0 ? "minecraft:the_nether" : "minecraft:the_end");
  } else { root = g_strdup(dimension_dir); dimension = g_strdup("minecraft:overworld"); }
  if (g_strcmp0(dimensions, "dimensions") != 0) {
    const char *standard = g_strcmp0(dimension, "minecraft:the_nether") == 0 ? "the_nether" :
      g_strcmp0(dimension, "minecraft:the_end") == 0 ? "the_end" : "overworld";
    g_autofree char *canonical = g_build_filename(root, "dimensions", "minecraft", standard, NULL);
    if (g_file_test(canonical, G_FILE_TEST_IS_DIR)) { g_free(dimension); return NULL; }
  }
  PumpkinMapWorld *world = g_new0(PumpkinMapWorld, 1);
  world->name = g_filename_display_basename(root); world->dimension = dimension;
  world->id = g_strdup(region); world->region = g_strdup(region);
  world->chunks = g_array_new(FALSE, FALSE, sizeof(PumpkinMapChunk));
  world->tiles = g_ptr_array_new_with_free_func(pumpkin_map_tile_free);
  return world;
}

static guint32 be32(const guint8 *bytes)
{
  return ((guint32)bytes[0] << 24) | ((guint32)bytes[1] << 16) | ((guint32)bytes[2] << 8) | bytes[3];
}
static gboolean read_region(Scan *scan, PumpkinMapWorld *world, const char *path, int rx, int rz)
{
  g_autoptr(GFile) file = g_file_new_for_path(path);
  g_autoptr(GFileInfo) info = g_file_query_info(file, "standard::size,standard::type,standard::is-symlink,time::modified,time::modified-usec,id::file", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, scan->cancel, NULL);
  if (info == NULL || g_file_info_get_is_symlink(info) || g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR) return FALSE;
  goffset size = g_file_info_get_size(info);
  if (size < 8192) return FALSE;
  g_autoptr(GFileInputStream) stream = g_file_read(file, scan->cancel, NULL);
  if (stream == NULL) return FALSE;
  guint8 header[8192]; gsize read = 0;
  if (!g_input_stream_read_all(G_INPUT_STREAM(stream), header, sizeof(header), &read, scan->cancel, NULL) || read != sizeof(header)) return FALSE;
  PumpkinMapTile *tile = g_new0(PumpkinMapTile, 1);
  tile->x = rx; tile->z = rz;
  tile->mask = cairo_image_surface_create(CAIRO_FORMAT_A8, 32, 32);
  if (cairo_surface_status(tile->mask) != CAIRO_STATUS_SUCCESS) { pumpkin_map_tile_free(tile); return FALSE; }
  guint8 *pixels = cairo_image_surface_get_data(tile->mask);
  int stride = cairo_image_surface_get_stride(tile->mask);
  guint entries = 0;
  for (guint i = 0; i < 1024; i++) {
    guint32 location = be32(header + i * 4), offset = location >> 8, sectors = location & 255;
    if (offset < 2 || sectors == 0 || ((guint64)offset + sectors) * 4096 > (guint64)size) continue;
    PumpkinMapChunk chunk = {rx * 32 + (int)(i % 32), rz * 32 + (int)(i / 32)};
    g_array_append_val(world->chunks, chunk);
    pixels[(i / 32) * (guint)stride + i % 32] = 255;
    guint32 timestamp = be32(header + 4096 + i * 4);
    guint64 revision = ((guint64)timestamp << 32) | location;
    /* Old writers omit timestamps; use their file's modification time instead. */
    if (timestamp == 0) revision ^= g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED) * G_USEC_PER_SEC +
      g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
    const char *identity = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_ID_FILE);
    if (identity != NULL) revision ^= (guint64)g_str_hash(identity) << 16;
    tile->revisions[i] = revision != 0 ? revision : 1;
    ++entries;
  }
  cairo_surface_mark_dirty(tile->mask);
  if (entries != 0) g_ptr_array_add(world->tiles, tile);
  else pumpkin_map_tile_free(tile);
  return TRUE;
}
static gboolean scan_directory(Scan *scan, const char *path, guint depth, GError **error)
{
  if (g_cancellable_set_error_if_cancelled(scan->cancel, error)) return FALSE;
  if (depth > 8 || ++scan->directories > 4096) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Map discovery limit reached; refusing an unbounded filesystem scan"); return FALSE;
  }
  g_ptr_array_add(scan->watched, g_strdup(path));
  g_autoptr(GFile) folder = g_file_new_for_path(path);
  g_autoptr(GFileEnumerator) entries = g_file_enumerate_children(folder, "standard::name,standard::type,standard::is-symlink", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, scan->cancel, error);
  if (entries == NULL) return FALSE;
  g_autofree char *name = g_path_get_basename(path);
  PumpkinMapWorld *world = g_strcmp0(name, "region") == 0 ? region_world(path) : NULL;
  if (g_strcmp0(name, "region") == 0 && world == NULL) return TRUE;
  gboolean ok = TRUE;
  for (;;) {
    g_autoptr(GError) enumeration_error = NULL;
    g_autoptr(GFileInfo) info = g_file_enumerator_next_file(entries, scan->cancel, &enumeration_error);
    if (info == NULL) {
      if (enumeration_error != NULL) {
        ok = FALSE; g_propagate_error(error, g_steal_pointer(&enumeration_error));
      }
      break;
    }
    const char *entry = g_file_info_get_name(info);
    if (entry[0] == '.' || g_file_info_get_is_symlink(info)) continue;
    g_autofree char *child = g_build_filename(path, entry, NULL);
    if (world != NULL && g_file_info_get_file_type(info) == G_FILE_TYPE_REGULAR) {
      if (!g_str_has_prefix(entry, "r.")) continue;
      char *end = NULL;
      gint64 rx = g_ascii_strtoll(entry + 2, &end, 10);
      if (end == entry + 2 || *end != '.' || rx < -117188 || rx > 117188) continue;
      const char *second = end + 1;
      gint64 rz = g_ascii_strtoll(second, &end, 10);
      if (end == second || g_strcmp0(end, ".mca") != 0 || rz < -117188 || rz > 117188) continue;
      if (++scan->files > 4096) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Region discovery limit reached"); ok = FALSE; break;
      }
      if (!read_region(scan, world, child, (int)rx, (int)rz)) world->inventory_incomplete = TRUE;
    } else if (world == NULL && g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY &&
               g_strcmp0(entry, "plugins") != 0 && g_strcmp0(entry, "logs") != 0 &&
               g_strcmp0(entry, "playerdata") != 0 && g_strcmp0(entry, "entities") != 0 &&
               g_strcmp0(entry, "poi") != 0 && g_strcmp0(entry, "data") != 0) {
      if (!scan_directory(scan, child, depth + 1, error)) { ok = FALSE; break; }
    }
  }
  if (world != NULL) {
    if (world->chunks->len > 0 || world->inventory_incomplete) g_ptr_array_add(scan->worlds, world);
    else pumpkin_map_world_free(world);
  }
  return ok;
}
GPtrArray *pumpkin_map_scan_regions_watched(const char *data_dir, GCancellable *cancel, GPtrArray **directories, GError **error)
{
  if (!safe_file(data_dir, G_FILE_TYPE_DIRECTORY, error)) return NULL;
  g_autoptr(GCancellable) owned_cancel = cancel == NULL ? g_cancellable_new() : NULL;
  g_autoptr(GPtrArray) worlds = g_ptr_array_new_with_free_func(pumpkin_map_world_free);
  g_autoptr(GPtrArray) watched = g_ptr_array_new_with_free_func(g_free);
  Scan scan = {.worlds = worlds, .watched = watched, .cancel = cancel != NULL ? cancel : owned_cancel};
  if (!scan_directory(&scan, data_dir, 0, error)) return NULL;
  if (directories != NULL) *directories = g_steal_pointer(&watched);
  return g_steal_pointer(&worlds);
}
GPtrArray *pumpkin_map_scan_regions(const char *data_dir, GCancellable *cancel, GError **error)
{ return pumpkin_map_scan_regions_watched(data_dir, cancel, NULL, error); }
