#pragma once
#include "server.h"

/* Serialize runtime changes through Pumpkin's load/unload acknowledgements.
 * File parking also prevents the bridge loading after a crash or next launch. */
void pumpkin_minimap_bridge_set_enabled(PumpkinServer *server, gboolean enabled);
