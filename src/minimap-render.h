#pragma once
#include <cairo.h>
#include <glib.h>

void pumpkin_map_paint_chunk(cairo_t *cr, cairo_surface_t *surface, double x, double y, double scale);
void pumpkin_map_paint_player(cairo_t *cr, double x, double y, double yaw,
                              gboolean yaw_known, guint32 color, gboolean following, gboolean selected);
