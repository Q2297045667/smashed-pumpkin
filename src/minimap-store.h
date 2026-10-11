#pragma once
#include "minimap-model.h"
#define PUMPKIN_MAP_BRIDGE_FILENAME "map-bridge.wasm"
#define PUMPKIN_MAP_BRIDGE_RESOURCE "/dev/rotstein/SmashedPumpkin/plugins/map-bridge.wasm"

gboolean pumpkin_map_bridge_present(const char *data_dir);
gboolean pumpkin_map_is_internal_plugin(const char *filename);
/* Only verified bundled releases may be parked or restored. Never overwrite an
 * existing active/disabled file. plugin_name is set only for an existing bridge. */
gboolean pumpkin_map_bridge_set_enabled(const char *data_dir, gboolean enabled,
                                         char **plugin_name, gboolean *changed, GError **error);

GPtrArray *pumpkin_map_scan_regions(const char *data_dir, GCancellable *cancel, GError **error);
GPtrArray *pumpkin_map_scan_regions_watched(const char *data_dir, GCancellable *cancel,
                                           GPtrArray **directories, GError **error);
PumpkinMapSnapshot *pumpkin_map_read_live(const char *data_dir, const char *request,
                                          gint64 now_ms, GError **error);
gboolean pumpkin_map_write_request(const char *data_dir, const char *json, GError **error);
gboolean pumpkin_map_write_request_cancellable(const char *data_dir, const char *json,
                                               GCancellable *cancel, GError **error);
gboolean pumpkin_map_stop_request(const char *data_dir, const char *request, GError **error);

typedef enum {
  PUMPKIN_MAP_BRIDGE_MISSING,
  PUMPKIN_MAP_BRIDGE_UNKNOWN,
  PUMPKIN_MAP_BRIDGE_DENIED,
  PUMPKIN_MAP_BRIDGE_APPROVED,
  PUMPKIN_MAP_BRIDGE_UNTRUSTED,
  PUMPKIN_MAP_BRIDGE_OUTDATED,
  PUMPKIN_MAP_BRIDGE_INVALID
} PumpkinMapBridgeAccess;
PumpkinMapBridgeAccess pumpkin_map_bridge_access(const char *data_dir, GBytes *bundled, GError **error);
/* Caller must hold stopped-server maintenance. Approval requires explicit consent;
 * upgrades preserve only existing decisions for identical permission scopes. */
gboolean pumpkin_map_bridge_approve(const char *data_dir, GBytes *bundled, GError **error);
gboolean pumpkin_map_bridge_upgrade(const char *data_dir, GBytes *bundled, GError **error);
