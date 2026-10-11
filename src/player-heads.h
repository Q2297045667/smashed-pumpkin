#pragma once

#include <gtk/gtk.h>

void pumpkin_player_head_set(GtkImage *image, const char *uuid, const char *name,
                             gboolean java, gboolean bedrock);
gboolean pumpkin_player_head_is_java(GtkImage *image);
char *pumpkin_player_skin_url(const char *profile_json, const char *identifier);
GdkTexture *pumpkin_player_head_from_skin(GBytes *bytes);
