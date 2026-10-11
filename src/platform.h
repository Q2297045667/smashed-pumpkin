#pragma once

#include <glib.h>

G_BEGIN_DECLS

#if defined(__ANDROID__)
#define PUMPKIN_PLATFORM_ANDROID 1
#else
#define PUMPKIN_PLATFORM_ANDROID 0
#endif

static inline gboolean
pumpkin_platform_is_android(void)
{
  return PUMPKIN_PLATFORM_ANDROID;
}

static inline gboolean
pumpkin_platform_supports_local_hosting(void)
{
  return !PUMPKIN_PLATFORM_ANDROID;
}

static inline gboolean
pumpkin_platform_supports_desktop_integrations(void)
{
  return !PUMPKIN_PLATFORM_ANDROID;
}

static inline gboolean
pumpkin_platform_supports_review_prompt(void)
{
  return !PUMPKIN_PLATFORM_ANDROID;
}

G_END_DECLS
