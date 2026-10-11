#include "minimap-motion.h"
#include <math.h>

typedef struct {
  char *world, *dimension;
  PumpkinMapPose from, to;
  gboolean yaw_known;
  gint64 started, duration, sampled_at;
} PlayerMotion;
struct _PumpkinMapMotion { GHashTable *players; };

static void player_motion_free(gpointer data)
{
  PlayerMotion *player = data;
  g_free(player->world); g_free(player->dimension); g_free(player);
}
PumpkinMapMotion *pumpkin_map_motion_new(void)
{
  PumpkinMapMotion *motion = g_new0(PumpkinMapMotion, 1);
  motion->players = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, player_motion_free);
  return motion;
}
void pumpkin_map_motion_free(PumpkinMapMotion *motion)
{
  if (motion == NULL) return;
  g_hash_table_unref(motion->players); g_free(motion);
}
void pumpkin_map_motion_clear(PumpkinMapMotion *motion) { g_hash_table_remove_all(motion->players); }
static PumpkinMapPose sample_pose(const PlayerMotion *player, gint64 now_us)
{
  double t = player->duration == 0 ? 1 : CLAMP((double)(now_us - player->started) / player->duration, 0, 1);
  return (PumpkinMapPose){player->from.x + (player->to.x - player->from.x) * t,
    player->from.z + (player->to.z - player->from.z) * t,
    player->from.yaw + remainder(player->to.yaw - player->from.yaw, 360) * t};
}
PumpkinMapPose pumpkin_map_motion_pose(PumpkinMapMotion *motion, const PumpkinMapPlayer *player, gint64 now_us)
{
  PlayerMotion *state = g_hash_table_lookup(motion->players, player->id);
  return state != NULL ? sample_pose(state, now_us) : (PumpkinMapPose){player->x, player->z, player->yaw};
}
void pumpkin_map_motion_update(PumpkinMapMotion *motion, const PumpkinMapSnapshot *snapshot,
                               gint64 now_us, guint refresh_hz)
{
  g_autoptr(GHashTable) present = g_hash_table_new(g_str_hash, g_str_equal);
  for (guint i = 0; i < snapshot->players->len; ++i) {
    PumpkinMapPlayer *player = g_ptr_array_index(snapshot->players, i);
    g_hash_table_add(present, player->id);
    PlayerMotion *state = g_hash_table_lookup(motion->players, player->id);
    if (state != NULL && snapshot->updated_at_ms <= state->sampled_at) continue;
    gboolean same_world = state != NULL && g_strcmp0(state->world, player->world) == 0 &&
      g_strcmp0(state->dimension, player->dimension) == 0;
    PumpkinMapPose target = {player->x, player->z, player->yaw};
    PumpkinMapPose current = state != NULL ? sample_pose(state, now_us) : target;
    gint64 elapsed_ms = state != NULL ? snapshot->updated_at_ms - state->sampled_at : 0;
    double dx = target.x - current.x, dz = target.z - current.z;
    gboolean interpolate = same_world && elapsed_ms <= 1000 && dx * dx + dz * dz <= 128 * 128;
    if (state == NULL) {
      state = g_new0(PlayerMotion, 1);
      g_hash_table_insert(motion->players, g_strdup(player->id), state);
    }
    /* Retarget from the visible position, never the preceding network position.
     * Re-reading the same snapshot must not restart an animation. */
    state->from = interpolate ? current : target;
    if (!state->yaw_known || !player->yaw_known) state->from.yaw = target.yaw;
    state->to = target;
    gboolean moving = fabs(dx) > .001 || fabs(dz) > .001 ||
      fabs(remainder(state->to.yaw - state->from.yaw, 360)) > .01;
    gint64 interval = elapsed_ms > 0 ? elapsed_ms * 1000 : G_USEC_PER_SEC / CLAMP(refresh_hz, 2, 20);
    state->duration = interpolate && moving ? CLAMP(interval, 50000, 500000) : 0;
    state->started = now_us; state->sampled_at = snapshot->updated_at_ms; state->yaw_known = player->yaw_known;
    g_free(state->world); state->world = g_strdup(player->world);
    g_free(state->dimension); state->dimension = g_strdup(player->dimension);
  }
  GHashTableIter iter; gpointer key;
  g_hash_table_iter_init(&iter, motion->players);
  while (g_hash_table_iter_next(&iter, &key, NULL))
    if (!g_hash_table_contains(present, key)) g_hash_table_iter_remove(&iter);
}
gboolean pumpkin_map_motion_active(PumpkinMapMotion *motion, gint64 now_us)
{
  GHashTableIter iter; gpointer value;
  g_hash_table_iter_init(&iter, motion->players);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    PlayerMotion *player = value;
    if (now_us < player->started + player->duration) return TRUE;
  }
  return FALSE;
}
