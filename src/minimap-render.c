#include "minimap-render.h"
#include <math.h>

void pumpkin_map_paint_chunk(cairo_t *cr, cairo_surface_t *surface, double x, double y, double scale)
{
  double left = x, top = y, right = x + 16 * scale, bottom = y + 16 * scale;
  cairo_user_to_device(cr, &left, &top); cairo_user_to_device(cr, &right, &bottom);
  left = round(left); top = round(top); right = round(right); bottom = round(bottom);
  cairo_device_to_user(cr, &left, &top); cairo_device_to_user(cr, &right, &bottom);
  if (right <= left || bottom <= top) return;
  cairo_save(cr);
  /* Adjacent tiles share the same device-pixel edge at every zoom/DPI. Padding
   * source edges prevents transparent samples beyond a rounded chunk clip. */
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
  cairo_rectangle(cr, left, top, right - left, bottom - top); cairo_clip(cr);
  cairo_translate(cr, x, y); cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
  cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
  cairo_paint(cr); cairo_restore(cr);
}
void pumpkin_map_paint_player(cairo_t *cr, double x, double y, double yaw,
                              gboolean yaw_known, guint32 color, gboolean following, gboolean selected)
{
  double red = ((color >> 16) & 255) / 255.0;
  double green = ((color >> 8) & 255) / 255.0, blue = (color & 255) / 255.0;
  cairo_save(cr); cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
  if (following || selected) {
    cairo_set_source_rgba(cr, red, green, blue, .26); cairo_arc(cr, x, y, 14, 0, 2 * G_PI); cairo_fill(cr);
  }
  cairo_translate(cr, x, y);
  double r = following ? 6.5 : 5.5;
  if (yaw_known) {
    cairo_rotate(cr, (yaw + 180) * G_PI / 180);
    double tip = r + 3, k = .55228475;
    cairo_move_to(cr, 0, -tip);
    cairo_curve_to(cr, 1.8, -tip, r, -.75 * r, r, 0);
    cairo_curve_to(cr, r, k * r, k * r, r, 0, r);
    cairo_curve_to(cr, -k * r, r, -r, k * r, -r, 0);
    cairo_curve_to(cr, -r, -.75 * r, -1.8, -tip, 0, -tip);
    cairo_close_path(cr);
  } else cairo_arc(cr, 0, 0, r, 0, 2 * G_PI);
  cairo_set_source_rgb(cr, red, green, blue); cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, .95); cairo_set_line_width(cr, 1.5); cairo_stroke(cr);
  cairo_restore(cr);
}
