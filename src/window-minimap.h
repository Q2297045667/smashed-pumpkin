#pragma once
#include "window-internal.h"

void pumpkin_window_minimap_setup(PumpkinWindow *self);
void pumpkin_window_minimap_update(PumpkinWindow *self);
void pumpkin_window_minimap_shutdown(PumpkinWindow *self);
void pumpkin_window_minimap_set_enabled(PumpkinWindow *self, gboolean enabled);
gboolean pumpkin_window_minimap_prepare_start(PumpkinWindow *self, PumpkinServer *server);
