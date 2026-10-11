#pragma once

#include <glib-object.h>

/* Console replies have no request IDs; match supported replies in send order. */
gboolean pumpkin_command_poll_ready(GObject *server, const char *command, gint64 now);
void pumpkin_command_track(GObject *server, const char *command, gboolean automatic, gint64 now);
gboolean pumpkin_command_consume_reply(GObject *server, const char *command);
void pumpkin_command_tracking_reset(GObject *server);
