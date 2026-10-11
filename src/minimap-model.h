#pragma once
#include <gio/gio.h>
#include <cairo.h>

typedef struct { int x, z; } PumpkinMapChunk;
typedef struct {
  int x, z;
  cairo_surface_t *mask;
  guint64 revisions[1024];
} PumpkinMapTile;
typedef struct {
  char *id, *name, *dimension, *region;
  GArray *chunks;
  GPtrArray *tiles;
  GHashTable *tile_index;
  gboolean border_known, transition_known, inventory_incomplete;
  double border_x, border_z, diameter, target;
  gint64 transition_at_ms, transition_ms;
} PumpkinMapWorld;
typedef struct {
  char *id, *name, *world, *dimension;
  double x, y, z;
  double yaw;
  gboolean yaw_known;
  guint32 color;
  guint ping;
} PumpkinMapPlayer;
typedef struct {
  GPtrArray *worlds, *players;
  gint64 updated_at_ms;
  char *action_id, *action_message;
  gboolean action_ok;
} PumpkinMapSnapshot;

void pumpkin_map_world_free(gpointer world);
void pumpkin_map_tile_free(gpointer tile);
void pumpkin_map_player_free(gpointer player);
void pumpkin_map_snapshot_free(PumpkinMapSnapshot *snapshot);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PumpkinMapSnapshot, pumpkin_map_snapshot_free)
PumpkinMapSnapshot *pumpkin_map_snapshot_parse(const char *json, const char *request,
                                               gint64 now_ms, GError **error);
double pumpkin_map_border_size(const PumpkinMapWorld *world, gint64 now_ms);
gboolean pumpkin_map_player_matches(const PumpkinMapPlayer *player, const PumpkinMapWorld *world);
gboolean pumpkin_map_snapshot_fresh(const PumpkinMapSnapshot *snapshot, gint64 now_ms);
PumpkinMapPlayer *pumpkin_map_find_player(const PumpkinMapSnapshot *snapshot, const char *id);
guint32 pumpkin_map_locator_color(const char *uuid);
