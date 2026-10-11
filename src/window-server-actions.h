#pragma once

#include "window-internal.h"

void pumpkin_window_update_server_actions(PumpkinWindow *self, gboolean installed,
                                          gboolean update_available, gboolean busy,
                                          gboolean running);
