#include "minimap-model.h"
#include "vendor/cjson/cJSON.h"
#include <math.h>
#include <string.h>

void pumpkin_map_tile_free(gpointer data)
{
  PumpkinMapTile *tile = data;
  cairo_surface_destroy(tile->mask);
  g_free(tile);
}
void pumpkin_map_world_free(gpointer data)
{
  PumpkinMapWorld *world = data;
  g_free(world->id); g_free(world->name); g_free(world->dimension); g_free(world->region);
  g_clear_pointer(&world->chunks, g_array_unref);
  g_clear_pointer(&world->tile_index, g_hash_table_unref);
  g_clear_pointer(&world->tiles, g_ptr_array_unref); g_free(world);
}
void pumpkin_map_player_free(gpointer data)
{
  PumpkinMapPlayer *player = data;
  g_free(player->id); g_free(player->name); g_free(player->world); g_free(player->dimension); g_free(player);
}
void pumpkin_map_snapshot_free(PumpkinMapSnapshot *snapshot)
{
  if (snapshot == NULL) return;
  g_ptr_array_unref(snapshot->worlds); g_ptr_array_unref(snapshot->players);
  g_free(snapshot->action_id); g_free(snapshot->action_message); g_free(snapshot);
}
static const char *string(const cJSON *object, const char *key, gsize maximum)
{
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsString(value) || value->valuestring == NULL) return NULL;
  const char *text = value->valuestring;
  if (text[0] == '\0' || strlen(text) > maximum || !g_utf8_validate(text, -1, NULL)) return NULL;
  for (const char *p = text; *p != '\0'; p = g_utf8_next_char(p))
    if (g_unichar_iscntrl(g_utf8_get_char(p))) return NULL;
  return text;
}
static gboolean number(const cJSON *object, const char *key, double *out, double minimum, double maximum)
{
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
      value->valuedouble < minimum || value->valuedouble > maximum) return FALSE;
  *out = value->valuedouble;
  return TRUE;
}
gboolean pumpkin_map_snapshot_fresh(const PumpkinMapSnapshot *snapshot, gint64 now_ms)
{
  return snapshot != NULL && snapshot->updated_at_ms <= now_ms + 1000 &&
    snapshot->updated_at_ms >= now_ms - 5000;
}
gboolean pumpkin_map_player_matches(const PumpkinMapPlayer *player, const PumpkinMapWorld *world)
{
  return g_strcmp0(player->world, world->name) == 0 && g_strcmp0(player->dimension, world->dimension) == 0;
}
PumpkinMapPlayer *pumpkin_map_find_player(const PumpkinMapSnapshot *snapshot, const char *id)
{
  if (snapshot == NULL || id == NULL) return NULL;
  for (guint i = 0; i < snapshot->players->len; i++) {
    PumpkinMapPlayer *player = g_ptr_array_index(snapshot->players, i);
    if (g_strcmp0(player->id, id) == 0) return player;
  }
  return NULL;
}
guint32 pumpkin_map_locator_color(const char *uuid)
{
  if (uuid == NULL || !g_uuid_string_is_valid(uuid)) return 0x7ebde8;
  /* Java UUID.hashCode(): XOR its four 32-bit words; RGB is the low 24 bits. */
  guint32 hash = 0, word = 0; guint digits = 0;
  for (const char *p = uuid; *p != '\0'; p++) {
    if (*p == '-') continue;
    word = (word << 4) | (guint32)g_ascii_xdigit_value(*p);
    if (++digits == 8) { hash ^= word; word = 0; digits = 0; }
  }
  return hash & 0xffffff;
}
double pumpkin_map_border_size(const PumpkinMapWorld *world, gint64 now_ms)
{
  if (!world->transition_known) return world->diameter;
  if (world->transition_ms <= 0) return world->target;
  double progress = CLAMP((double)(now_ms - world->transition_at_ms) / (double)world->transition_ms, 0.0, 1.0);
  return world->diameter + (world->target - world->diameter) * progress;
}
PumpkinMapSnapshot *pumpkin_map_snapshot_parse(const char *json, const char *request, gint64 now_ms, GError **error)
{
  if (json == NULL || request == NULL || !g_uuid_string_is_valid(request) ||
      strlen(json) > 1024 * 1024 || strstr(json, "\\u0000") != NULL) goto invalid;
  cJSON *root = cJSON_ParseWithOpts(json, NULL, TRUE);
  if (root == NULL) goto invalid;
  PumpkinMapSnapshot *snapshot = g_new0(PumpkinMapSnapshot, 1);
  snapshot->worlds = g_ptr_array_new_with_free_func(pumpkin_map_world_free);
  snapshot->players = g_ptr_array_new_with_free_func(pumpkin_map_player_free);
  double version = 0, timestamp = 0;
  const cJSON *worlds = cJSON_GetObjectItemCaseSensitive(root, "worlds");
  const cJSON *players = cJSON_GetObjectItemCaseSensitive(root, "players");
  gboolean valid = number(root, "version", &version, 1, 1) &&
    g_strcmp0(string(root, "request", 36), request) == 0 &&
    number(root, "updated_at_ms", &timestamp, 0, 9e15) && trunc(timestamp) == timestamp &&
    cJSON_IsArray(worlds) && cJSON_GetArraySize(worlds) <= 256 &&
    cJSON_IsArray(players) && cJSON_GetArraySize(players) <= 512 &&
    !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "complete"));
  snapshot->updated_at_ms = (gint64)timestamp;
  valid = valid && pumpkin_map_snapshot_fresh(snapshot, now_ms);
  const cJSON *item;
  guint loaded_count = 0;
  cJSON_ArrayForEach(item, worlds) {
    if (!valid) break;
    const char *name = string(item, "name", 255), *dimension = string(item, "dimension", 255);
    if (name == NULL || dimension == NULL) { valid = FALSE; break; }
    PumpkinMapWorld *world = g_new0(PumpkinMapWorld, 1);
    world->name = g_strdup(name); world->dimension = g_strdup(dimension);
    world->id = g_strdup_printf("%s|%s", name, dimension);
    world->chunks = g_array_new(FALSE, FALSE, sizeof(PumpkinMapChunk));
    const cJSON *chunks = cJSON_GetObjectItemCaseSensitive(item, "loaded_chunks");
    if (chunks != NULL && (!cJSON_IsArray(chunks) || cJSON_GetArraySize(chunks) > 512)) valid = FALSE;
    const cJSON *chunk;
    cJSON_ArrayForEach(chunk, chunks) {
      double x = 0, z = 0;
      if (++loaded_count > 512 || !number(chunk, "x", &x, -3750000, 3750000) ||
          !number(chunk, "z", &z, -3750000, 3750000) || trunc(x) != x || trunc(z) != z) {
        valid = FALSE; break;
      }
      PumpkinMapChunk coordinate = {(int)x, (int)z};
      g_array_append_val(world->chunks, coordinate);
    }
    for (guint i = 0; i < snapshot->worlds->len; i++) {
      PumpkinMapWorld *prior = g_ptr_array_index(snapshot->worlds, i);
      if (g_strcmp0(prior->id, world->id) == 0) valid = FALSE;
    }
    const cJSON *border = cJSON_GetObjectItemCaseSensitive(item, "border");
    world->border_known = number(border, "x", &world->border_x, -6e7, 6e7) &&
      number(border, "z", &world->border_z, -6e7, 6e7) && number(border, "diameter", &world->diameter, 0, 1.2e8);
    world->transition_known = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(border, "transition_known"));
    if (world->transition_known) {
      double at = 0, duration = 0;
      valid = valid && world->border_known && number(border, "target", &world->target, 0, 1.2e8) &&
        number(border, "at_ms", &at, 0, 9e15) && number(border, "duration_ms", &duration, 0, 9e15) &&
        trunc(at) == at && trunc(duration) == duration && at <= now_ms + 1000;
      world->transition_at_ms = (gint64)at; world->transition_ms = (gint64)duration;
    }
    g_ptr_array_add(snapshot->worlds, world);
  }
  cJSON_ArrayForEach(item, players) {
    if (!valid) break;
    const char *id = string(item, "id", 36), *name = string(item, "name", 64);
    const char *world = string(item, "world", 255), *dimension = string(item, "dimension", 255);
    if (id == NULL || !g_uuid_string_is_valid(id) || name == NULL || world == NULL || dimension == NULL) {
      valid = FALSE; break;
    }
    PumpkinMapPlayer *player = g_new0(PumpkinMapPlayer, 1);
    player->id = g_ascii_strdown(id, -1); player->name = g_strdup(name);
    player->world = g_strdup(world); player->dimension = g_strdup(dimension);
    double ping = 0;
    valid = number(item, "x", &player->x, -6e7, 6e7) && number(item, "y", &player->y, -1e6, 1e6) &&
      number(item, "z", &player->z, -6e7, 6e7) && number(item, "ping", &ping, 0, G_MAXUINT) && trunc(ping) == ping &&
      pumpkin_map_find_player(snapshot, player->id) == NULL;
    player->ping = (guint)ping;
    player->color = pumpkin_map_locator_color(player->id);
    const cJSON *yaw = cJSON_GetObjectItemCaseSensitive(item, "yaw");
    if (yaw != NULL) {
      player->yaw_known = number(item, "yaw", &player->yaw, -1e9, 1e9);
      valid = valid && player->yaw_known;
      player->yaw = remainder(player->yaw, 360);
    }
    gboolean known = FALSE;
    for (guint i = 0; i < snapshot->worlds->len; i++)
      if (pumpkin_map_player_matches(player, g_ptr_array_index(snapshot->worlds, i))) known = TRUE;
    valid = valid && known;
    g_ptr_array_add(snapshot->players, player);
  }
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action_result");
  const char *action_id = string(action, "id", 36);
  if (action_id != NULL && g_uuid_string_is_valid(action_id)) {
    snapshot->action_id = g_strdup(action_id);
    snapshot->action_message = g_strdup(string(action, "message", 512));
    snapshot->action_ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(action, "ok"));
  }
  cJSON_Delete(root);
  if (valid) return snapshot;
  pumpkin_map_snapshot_free(snapshot);
invalid:
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Map Bridge data is invalid, stale or belongs to another session");
  return NULL;
}
