#pragma once
#include "minimap-model.h"

typedef struct { double x, z, yaw; } PumpkinMapPose;
typedef struct _PumpkinMapMotion PumpkinMapMotion;
PumpkinMapMotion *pumpkin_map_motion_new(void);
void pumpkin_map_motion_free(PumpkinMapMotion *motion);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PumpkinMapMotion, pumpkin_map_motion_free)
void pumpkin_map_motion_clear(PumpkinMapMotion *motion);
void pumpkin_map_motion_update(PumpkinMapMotion *motion, const PumpkinMapSnapshot *snapshot,
                               gint64 now_us, guint refresh_hz);
PumpkinMapPose pumpkin_map_motion_pose(PumpkinMapMotion *motion, const PumpkinMapPlayer *player,
                                       gint64 now_us);
gboolean pumpkin_map_motion_active(PumpkinMapMotion *motion, gint64 now_us);
