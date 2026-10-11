#pragma once

#include <gtk/gtk.h>
#include "app-config.h"

G_BEGIN_DECLS

char *pumpkin_monitor_id(GdkMonitor *monitor);
gboolean pumpkin_startup_can_place_maximized(GdkDisplay *display);
void pumpkin_window_apply_startup(GtkWindow *window, PumpkinConfig *config);

G_END_DECLS
