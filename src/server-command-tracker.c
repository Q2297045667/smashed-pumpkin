#include "server-command-tracker.h"

#include <string.h>

#define REPLY_TIMEOUT_USEC (30 * G_USEC_PER_SEC)
#define MAX_PENDING_REPLIES 32

typedef struct {
  gint64 sent_at;
  gboolean automatic;
} PendingReply;

typedef struct {
  GQueue tps;
  GQueue players;
} CommandTracker;

static void
tracker_free(gpointer data)
{
  CommandTracker *tracker = data;
  g_queue_clear_full(&tracker->tps, g_free);
  g_queue_clear_full(&tracker->players, g_free);
  g_free(tracker);
}

static GQueue *
command_queue(GObject *server, const char *command)
{
  if (command == NULL) return NULL;
  gboolean tps = g_strcmp0(command, "tps") == 0;
  gboolean list = g_strcmp0(command, "list") == 0 || g_str_has_prefix(command, "list ");
  if (!tps && !list) return NULL;
  CommandTracker *tracker = g_object_get_data(server, "pumpkin-command-tracker");
  if (tracker == NULL) {
    tracker = g_new0(CommandTracker, 1);
    g_object_set_data_full(server, "pumpkin-command-tracker", tracker, tracker_free);
  }
  return tps ? &tracker->tps : &tracker->players;
}

gboolean
pumpkin_command_poll_ready(GObject *server, const char *command, gint64 now)
{
  GQueue *queue = command_queue(server, command);
  if (queue == NULL) return FALSE;
  PendingReply *pending;
  /* A missing/unsupported response must not disable polling for the session. */
  while ((pending = g_queue_peek_head(queue)) != NULL &&
         now - pending->sent_at >= REPLY_TIMEOUT_USEC) {
    g_free(g_queue_pop_head(queue));
  }
  return g_queue_is_empty(queue);
}

void
pumpkin_command_track(GObject *server, const char *command, gboolean automatic, gint64 now)
{
  GQueue *queue = command_queue(server, command);
  if (queue == NULL) return;
  if (g_queue_get_length(queue) >= MAX_PENDING_REPLIES) {
    g_free(g_queue_pop_head(queue));
  }
  PendingReply *pending = g_new0(PendingReply, 1);
  pending->sent_at = now;
  pending->automatic = automatic;
  g_queue_push_tail(queue, pending);
}

gboolean
pumpkin_command_consume_reply(GObject *server, const char *command)
{
  GQueue *queue = command_queue(server, command);
  if (queue == NULL) return FALSE;
  PendingReply *pending = g_queue_pop_head(queue);
  if (pending == NULL) return FALSE;
  gboolean automatic = pending->automatic;
  g_free(pending);
  return automatic;
}

void
pumpkin_command_tracking_reset(GObject *server)
{
  g_object_set_data(server, "pumpkin-command-tracker", NULL);
}
