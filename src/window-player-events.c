#include "window-players.h"
#include "player-protocol.h"
#include "window-protocol.h"

void
player_states_consume_log(PumpkinWindow *self, const char *line)
{
  if (self == NULL || self->player_states == NULL || line == NULL) return;
  g_autofree char *message = pumpkin_player_log_message(line);
  PlayerPlatform edition = platform_from_line(line);

  g_autoptr(GRegex) accepted = g_regex_new(
    "^Accepted connection from (Java|Bedrock) Edition: (\\S+)", G_REGEX_CASELESS, 0, NULL);
  g_autoptr(GMatchInfo) connection = NULL;
  if (g_regex_match(accepted, message, 0, &connection)) {
    g_autofree char *platform = g_match_info_fetch(connection, 1);
    g_autofree char *socket = g_match_info_fetch(connection, 2);
    /* Keep the port: sharing an IP does not imply sharing an edition. */
    remember_platform_hint_for_ip(self, socket,
      g_ascii_strcasecmp(platform, "Java") == 0 ? PLAYER_PLATFORM_JAVA : PLAYER_PLATFORM_BEDROCK);
    return;
  }

  int count = -1;
  g_autofree char *names_csv = NULL;
  if (pumpkin_parse_player_list_snapshot_line(message, &count, &names_csv)) {
    self->list_snapshot_players = count;
    self->list_snapshot_updated_at = g_get_monotonic_time();
    g_autoptr(GRegex) max_re = g_regex_new("max\\s+of\\s+([0-9]+)", G_REGEX_CASELESS, 0, NULL);
    g_autoptr(GMatchInfo) max_match = NULL;
    if (g_regex_match(max_re, message, 0, &max_match)) {
      g_autofree char *value = g_match_info_fetch(max_match, 1);
      guint64 maximum = g_ascii_strtoull(value, NULL, 10);
      if (maximum <= G_MAXINT) self->list_snapshot_max_players = (int)maximum;
    }

    g_autoptr(GHashTable) present = g_hash_table_new(g_direct_hash, g_direct_equal);
    gboolean complete = TRUE;
    if (names_csv != NULL && *names_csv != '\0') {
      g_auto(GStrv) entries = g_strsplit(names_csv, ",", -1);
      for (guint i = 0; entries[i] != NULL; i++) {
        g_autofree char *name = NULL;
        g_autofree char *uuid = NULL;
        if (!pumpkin_player_parse_list_entry(entries[i], &name, &uuid)) {
          complete = FALSE;
          continue;
        }
        allow_deleted_player_tracking(self, uuid, name);
        PlayerState *state = ensure_player_state(self, uuid, name, TRUE);
        if (state != NULL) {
          g_hash_table_add(present, state);
          player_state_mark_online(self, state, platform_guess_from_uuid(uuid));
        } else {
          complete = FALSE;
        }
      }
    }
    /* A truncated response must not disconnect players that were omitted. */
    if (complete && g_hash_table_size(present) == (guint)count) {
      GHashTableIter iter;
      gpointer value = NULL;
      g_hash_table_iter_init(&iter, self->player_states);
      while (g_hash_table_iter_next(&iter, NULL, &value)) {
        PlayerState *state = value;
        if (state->online && !g_hash_table_contains(present, state))
          player_state_mark_offline(self, state);
      }
    }
    return;
  }

  g_autoptr(GRegex) login = g_regex_new("^(.+?)\\[/([^\\]]+)\\] logged in\\b", 0, 0, NULL);
  g_autoptr(GMatchInfo) login_match = NULL;
  if (g_regex_match(login, message, 0, &login_match)) {
    g_autofree char *name = g_match_info_fetch(login_match, 1);
    g_autofree char *socket = g_match_info_fetch(login_match, 2);
    if (!pumpkin_player_name_is_valid(name)) return;
    PlayerState *state = ensure_player_state(self, NULL, name, TRUE);
    if (state == NULL) return;
    g_autofree char *ip = extract_ip_from_socket_text(socket);
    g_free(state->last_ip);
    state->last_ip = g_strdup(ip);
    if (edition == PLAYER_PLATFORM_UNKNOWN) edition = platform_hint_for_ip(self, socket);
    g_autofree char *socket_key = normalized_key(socket);
    g_hash_table_remove(self->platform_hint_by_ip, socket_key);
    if (edition != PLAYER_PLATFORM_UNKNOWN) state->platform = edition;
    player_states_set_dirty(self);
    /* Login/authentication is not proof of entering the world yet. */
    return;
  }

  g_autoptr(GRegex) profile = g_regex_new(
    "^UUID of player (.+) is ([0-9a-fA-F-]{36})$", 0, 0, NULL);
  g_autoptr(GMatchInfo) profile_match = NULL;
  if (g_regex_match(profile, message, 0, &profile_match)) {
    g_autofree char *name = g_match_info_fetch(profile_match, 1);
    g_autofree char *uuid = g_match_info_fetch(profile_match, 2);
    if (pumpkin_player_name_is_valid(name) && g_uuid_string_is_valid(uuid)) {
      PlayerState *state = ensure_player_state(self, uuid, name, TRUE);
      if (state != NULL) state->platform = PLAYER_PLATFORM_JAVA;
      player_states_set_dirty(self);
    }
    return;
  }

  g_autofree char *joined = pumpkin_player_name_before_suffix(message, " joined the game");
  if (joined != NULL) {
    self->last_player_list_request_at = 0;
    allow_deleted_player_tracking(self, NULL, joined);
    PlayerState *state = ensure_player_state(self, NULL, joined, TRUE);
    player_state_mark_online(self, state, edition);
    return;
  }
  g_autofree char *left = pumpkin_player_name_before_suffix(message, " left the game");
  if (left != NULL) {
    self->last_player_list_request_at = 0;
    PlayerState *state = ensure_player_state(self, NULL, left, FALSE);
    player_state_mark_offline(self, state);
  }
}
