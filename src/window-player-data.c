#include "window-player-data.h"
#include "vendor/cjson/cJSON.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC(cJSON, cJSON_Delete)

#include <stdlib.h>

void
add_name_uuid_pairs(GHashTable *map, const char *contents)
{
  if (contents == NULL) {
    return;
  }

  g_autoptr(GRegex) regex = g_regex_new(
    "\"uuid\"\\s*:\\s*\"([^\"]+)\"\\s*,\\s*\"name\"\\s*:\\s*\"([^\"]+)\"|\"name\"\\s*:\\s*\"([^\"]+)\"\\s*,\\s*\"uuid\"\\s*:\\s*\"([^\"]+)\"",
    G_REGEX_CASELESS | G_REGEX_DOTALL, 0, NULL);
  g_autoptr(GMatchInfo) match = NULL;

  if (!g_regex_match(regex, contents, 0, &match)) {
    return;
  }

  while (g_match_info_matches(match)) {
    g_autofree char *uuid1 = g_match_info_fetch(match, 1);
    g_autofree char *name1 = g_match_info_fetch(match, 2);
    g_autofree char *name2 = g_match_info_fetch(match, 3);
    g_autofree char *uuid2 = g_match_info_fetch(match, 4);

    if (uuid1 != NULL && name1 != NULL) {
      g_hash_table_replace(map, g_strdup(uuid1), g_strdup(name1));
    } else if (uuid2 != NULL && name2 != NULL) {
      g_hash_table_replace(map, g_strdup(uuid2), g_strdup(name2));
    }

    g_match_info_next(match, NULL);
  }
}

static void
player_entry_free(PlayerEntry *entry)
{
  if (entry == NULL) {
    return;
  }
  g_clear_pointer(&entry->name, g_free);
  g_clear_pointer(&entry->uuid, g_free);
  g_clear_pointer(&entry->ip, g_free);
  g_clear_pointer(&entry->reason, g_free);
  g_clear_pointer(&entry->created, g_free);
  g_clear_pointer(&entry->source, g_free);
  g_clear_pointer(&entry->expires, g_free);
  g_free(entry);
}

char *
extract_json_string_field(const char *object_text, const char *field)
{
  if (object_text == NULL || field == NULL || *field == '\0') {
    return NULL;
  }
  g_autofree char *pattern = g_strdup_printf("\"%s\"\\s*:\\s*\"((?:\\\\.|[^\"\\\\])*)\"", field);
  g_autoptr(GRegex) regex = g_regex_new(pattern, G_REGEX_CASELESS | G_REGEX_DOTALL, 0, NULL);
  if (regex == NULL) {
    return NULL;
  }
  g_autoptr(GMatchInfo) match = NULL;
  if (!g_regex_match(regex, object_text, 0, &match) || !g_match_info_matches(match)) {
    return NULL;
  }
  return g_match_info_fetch(match, 1);
}

int
extract_json_int_field(const char *object_text, const char *field, int default_value)
{
  if (object_text == NULL || field == NULL || *field == '\0') {
    return default_value;
  }
  g_autofree char *pattern = g_strdup_printf("\"%s\"\\s*:\\s*(-?[0-9]+)", field);
  g_autoptr(GRegex) regex = g_regex_new(pattern, G_REGEX_CASELESS | G_REGEX_DOTALL, 0, NULL);
  if (regex == NULL) {
    return default_value;
  }
  g_autoptr(GMatchInfo) match = NULL;
  if (!g_regex_match(regex, object_text, 0, &match) || !g_match_info_matches(match)) {
    return default_value;
  }
  g_autofree char *value_txt = g_match_info_fetch(match, 1);
  if (value_txt == NULL || *value_txt == '\0') {
    return default_value;
  }
  return (int)strtol(value_txt, NULL, 10);
}

gboolean
extract_json_bool_field(const char *object_text, const char *field, gboolean default_value)
{
  if (object_text == NULL || field == NULL || *field == '\0') {
    return default_value;
  }
  g_autofree char *pattern = g_strdup_printf("\"%s\"\\s*:\\s*(true|false)", field);
  g_autoptr(GRegex) regex = g_regex_new(pattern, G_REGEX_CASELESS | G_REGEX_DOTALL, 0, NULL);
  if (regex == NULL) {
    return default_value;
  }
  g_autoptr(GMatchInfo) match = NULL;
  if (!g_regex_match(regex, object_text, 0, &match) || !g_match_info_matches(match)) {
    return default_value;
  }
  g_autofree char *value_txt = g_match_info_fetch(match, 1);
  if (value_txt == NULL) {
    return default_value;
  }
  return g_ascii_strcasecmp(value_txt, "true") == 0;
}

char *
resolve_data_file(PumpkinServer *server, const char *filename)
{
  g_autofree char *data_dir = pumpkin_server_get_data_dir(server);
  g_autofree char *path = g_build_filename(data_dir, "data", filename, NULL);
  if (g_file_test(path, G_FILE_TEST_EXISTS)) {
    return g_strdup(path);
  }
  g_autofree char *fallback = g_build_filename(data_dir, filename, NULL);
  if (g_file_test(fallback, G_FILE_TEST_EXISTS)) {
    return g_strdup(fallback);
  }
  return g_strdup(path);
}

static char *
player_json_string(const cJSON *object, const char *field)
{
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, field);
  return cJSON_IsString(value) && *value->valuestring != '\0' ? g_strdup(value->valuestring) : NULL;
}

GPtrArray *
load_player_entries_from_file(const char *path)
{
  g_autofree char *contents = NULL;
  if (path == NULL || !g_file_get_contents(path, &contents, NULL, NULL)) return NULL;
  g_autoptr(cJSON) root = cJSON_ParseWithOpts(contents, NULL, TRUE);
  if (!cJSON_IsArray(root)) return NULL;
  GPtrArray *entries = g_ptr_array_new_with_free_func((GDestroyNotify)player_entry_free);
  const cJSON *item;
  cJSON_ArrayForEach(item, root) {
    if (!cJSON_IsObject(item)) continue;
    PlayerEntry *entry = g_new0(PlayerEntry, 1);
    entry->name = player_json_string(item, "name");
    entry->uuid = player_json_string(item, "uuid");
    entry->ip = player_json_string(item, "ip");
    entry->reason = player_json_string(item, "reason");
    entry->created = player_json_string(item, "created");
    entry->source = player_json_string(item, "source");
    entry->expires = player_json_string(item, "expires");
    const cJSON *level = cJSON_GetObjectItemCaseSensitive(item, "level");
    entry->op_level = cJSON_IsNumber(level) && level->valuedouble >= 0 &&
      level->valuedouble <= G_MAXINT && level->valuedouble == level->valueint ? level->valueint : -1;
    entry->bypasses_player_limit =
      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "bypasses_player_limit")) ||
      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "bypassesPlayerLimit"));
    if (entry->name != NULL || entry->ip != NULL || (entry->uuid != NULL && g_uuid_string_is_valid(entry->uuid)))
      g_ptr_array_add(entries, entry);
    else player_entry_free(entry);
  }
  return entries;
}

char *
pick_latest_banned_ip(PumpkinWindow *self)
{
  if (self == NULL || self->current == NULL) {
    return NULL;
  }

  g_autofree char *banned_ips_path = resolve_data_file(self->current, "banned-ips.json");
  g_autoptr(GPtrArray) banned_ip_entries = load_player_entries_from_file(banned_ips_path);
  if (banned_ip_entries == NULL || banned_ip_entries->len == 0) {
    return NULL;
  }

  const char *best_ip = NULL;
  const char *best_created = NULL;
  for (guint i = 0; i < banned_ip_entries->len; i++) {
    PlayerEntry *entry = g_ptr_array_index(banned_ip_entries, i);
    if (entry == NULL || entry->ip == NULL || *entry->ip == '\0') {
      continue;
    }
    if (best_ip == NULL) {
      best_ip = entry->ip;
      best_created = entry->created;
      continue;
    }

    if (entry->created != NULL && *entry->created != '\0') {
      if (best_created == NULL || *best_created == '\0' || g_strcmp0(entry->created, best_created) > 0) {
        best_ip = entry->ip;
        best_created = entry->created;
      }
    }
  }

  return best_ip != NULL ? g_strdup(best_ip) : NULL;
}

gboolean
player_lookup_contains_state(GHashTable *set, const PlayerState *state)
{
  if (set == NULL || state == NULL) {
    return FALSE;
  }
  g_autofree char *uuid_key = normalized_key(state->uuid);
  if (uuid_key != NULL && g_hash_table_contains(set, uuid_key)) {
    return TRUE;
  }
  g_autofree char *name_key = normalized_key(state->name);
  if (name_key != NULL && g_hash_table_contains(set, name_key)) {
    return TRUE;
  }
  return FALSE;
}

int
player_lookup_op_level_for_state(GHashTable *map, const PlayerState *state)
{
  if (map == NULL || state == NULL) {
    return -1;
  }
  g_autofree char *uuid_key = normalized_key(state->uuid);
  if (uuid_key != NULL) {
    gpointer raw = g_hash_table_lookup(map, uuid_key);
    if (raw != NULL) {
      return GPOINTER_TO_INT(raw) - 1;
    }
  }
  g_autofree char *name_key = normalized_key(state->name);
  if (name_key != NULL) {
    gpointer raw = g_hash_table_lookup(map, name_key);
    if (raw != NULL) {
      return GPOINTER_TO_INT(raw) - 1;
    }
  }
  return -1;
}

const char *
player_lookup_reason_for_state(GHashTable *map, const PlayerState *state)
{
  if (map == NULL || state == NULL) {
    return NULL;
  }
  g_autofree char *uuid_key = normalized_key(state->uuid);
  if (uuid_key != NULL) {
    const char *reason = g_hash_table_lookup(map, uuid_key);
    if (reason != NULL) {
      return reason;
    }
  }
  g_autofree char *name_key = normalized_key(state->name);
  if (name_key != NULL) {
    return g_hash_table_lookup(map, name_key);
  }
  return NULL;
}
