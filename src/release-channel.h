#pragma once
#include <glib.h>

typedef enum {
  PUMPKIN_RELEASE_NIGHTLY = 0,
  PUMPKIN_RELEASE_STABLE
} PumpkinReleaseChannel;

const char *pumpkin_release_channel_id(PumpkinReleaseChannel channel);
const char *pumpkin_release_asset_name(void);
char *pumpkin_release_default_url(PumpkinReleaseChannel channel);
gboolean pumpkin_release_is_managed_url(const char *url);
