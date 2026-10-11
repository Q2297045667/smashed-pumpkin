#pragma once

#include <glib.h>

char *pumpkin_player_log_message(const char *line);
gboolean pumpkin_player_name_is_valid(const char *name);
char *pumpkin_player_name_before_suffix(const char *message, const char *suffix);
gboolean pumpkin_player_parse_list_entry(const char *entry, char **name, char **uuid);
