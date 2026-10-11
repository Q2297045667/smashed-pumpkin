#pragma once
#include "download.h"

PumpkinResolvedDownload *pumpkin_release_parse_metadata(const char *json, PumpkinReleaseChannel channel,
                                                       const char *asset_name, GError **error);
