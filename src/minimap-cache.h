#pragma once
#include <gio/gio.h>
#include <cairo.h>

/* Optional, bounded disk cache. Failures always fall back to the saved world. */
cairo_surface_t *pumpkin_map_cache_read(const char *region, int x, int z, guint64 revision);
/* Display a previous image while refreshing; caller must verify current chunk coverage. */
cairo_surface_t *pumpkin_map_cache_read_previous(const char *region, int x, int z, guint64 *revision);
void pumpkin_map_cache_write(const char *region, int x, int z, guint64 revision, cairo_surface_t *surface);
