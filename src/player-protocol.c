#include "player-protocol.h"
#include "window-protocol.h"

#include <string.h>

char *
pumpkin_player_log_message(const char *line)
{
  if (line == NULL) return NULL;
  g_autofree char *clean = pumpkin_strip_ansi(line);
  const char *message = clean;
  /* Pumpkin's bracketed logger and tracing's ISO timestamp format. Consume
   * only a complete log header, never a colon inside a player's name. */
  g_autoptr(GRegex) header = g_regex_new(
    "^(?:(?:\\[[^\\]\\r\\n]+\\]\\s*)?\\[(?:INFO|DEBUG|TRACE|WARN(?:ING)?|ERROR)\\]\\s*"
    "(?:\\[[^\\]\\r\\n]+\\]\\s*)?|"
    "(?:[0-9]{4}-[0-9]{2}-[0-9]{2}T\\S+|[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\\.[0-9]+)?)\\s+(?:INFO|DEBUG|TRACE|WARN(?:ING)?|ERROR)\\s+"
    "(?:[A-Za-z0-9_:]+:\\s*)?)", 0, 0, NULL);
  g_autoptr(GMatchInfo) match = NULL;
  if (g_regex_match(header, clean, 0, &match)) {
    int end = 0;
    g_match_info_fetch_pos(match, 0, NULL, &end);
    message += end;
  }
  char *result = g_strdup(message);
  return g_strstrip(result);
}

gboolean
pumpkin_player_name_is_valid(const char *name)
{
  if (name == NULL || *name == '\0' || !g_utf8_validate(name, -1, NULL) ||
      g_utf8_strlen(name, -1) > 64) return FALSE;
  for (const char *p = name; *p != '\0'; p = g_utf8_next_char(p)) {
    gunichar ch = g_utf8_get_char(p);
    if (g_unichar_iscntrl(ch) || (ch < 128 && strchr("[]<>:,()/\\\"", (int)ch) != NULL))
      return FALSE;
  }
  return TRUE;
}

char *
pumpkin_player_name_before_suffix(const char *message, const char *suffix)
{
  if (message == NULL || suffix == NULL || !g_str_has_suffix(message, suffix)) return NULL;
  g_autofree char *name = g_strndup(message, strlen(message) - strlen(suffix));
  g_strstrip(name);
  /* Java's rename announcement still refers to the current account. */
  char *formerly = strstr(name, " (formerly known as ");
  if (formerly != NULL) *formerly = '\0';
  return pumpkin_player_name_is_valid(name) ? g_strdup(name) : NULL;
}

gboolean
pumpkin_player_parse_list_entry(const char *entry, char **name, char **uuid)
{
  g_return_val_if_fail(name != NULL && uuid != NULL, FALSE);
  *name = NULL;
  *uuid = NULL;
  if (entry == NULL) return FALSE;
  g_autofree char *text = g_strdup(entry);
  g_strstrip(text);
  char *id = strrchr(text, '(');
  if (id != NULL) {
    gsize len = strlen(id);
    if (len != 38 || id[len - 1] != ')') return FALSE;
    id[len - 1] = '\0';
    if (!g_uuid_string_is_valid(id + 1)) return FALSE;
    *uuid = g_ascii_strdown(id + 1, -1);
    *id = '\0';
    g_strstrip(text);
  }
  if (!pumpkin_player_name_is_valid(text)) {
    g_clear_pointer(uuid, g_free);
    return FALSE;
  }
  *name = g_strdup(text);
  return TRUE;
}
