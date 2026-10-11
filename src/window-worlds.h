#pragma once
#include "window-internal.h"

void pumpkin_window_worlds_setup(PumpkinWindow *self);
void pumpkin_window_worlds_shutdown(PumpkinWindow *self);
void pumpkin_window_worlds_update_state(PumpkinWindow *self);
void refresh_world_list(PumpkinWindow *self);
