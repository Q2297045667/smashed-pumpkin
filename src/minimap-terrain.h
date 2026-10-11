#pragma once
#include <gio/gio.h>
#include <cairo.h>

typedef struct _PumpkinTerrainRegion PumpkinTerrainRegion;
PumpkinTerrainRegion *pumpkin_terrain_region_open(const char *region, int rx, int rz,
                                                 GCancellable *cancel, GError **error);
void pumpkin_terrain_region_free(PumpkinTerrainRegion *region);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PumpkinTerrainRegion, pumpkin_terrain_region_free)
cairo_surface_t *pumpkin_terrain_region_read(PumpkinTerrainRegion *region, int x, int z,
                                            GCancellable *cancel, GError **error);

/* Read-only, bounded decoding. One pixel per surface block, no host calls. */
cairo_surface_t *pumpkin_map_decode_terrain(const guint8 *nbt, gsize size, GError **error);
cairo_surface_t *pumpkin_map_read_terrain(const char *region, int x, int z,
                                        GCancellable *cancel, GError **error);
