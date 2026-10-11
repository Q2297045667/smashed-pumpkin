#include "release-channel.h"
#include <gio/gio.h>
#include <string.h>

const char *
pumpkin_release_channel_id(PumpkinReleaseChannel channel)
{
  return channel == PUMPKIN_RELEASE_STABLE ? "stable" : "nightly";
}

const char *
pumpkin_release_asset_name(void)
{
#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
#define PUMPKIN_ASSET_ARCH "ARM64"
#else
#define PUMPKIN_ASSET_ARCH "X64"
#endif
#if defined(G_OS_WIN32)
  return "pumpkin-" PUMPKIN_ASSET_ARCH "-Windows.exe";
#elif defined(__APPLE__)
  return "pumpkin-" PUMPKIN_ASSET_ARCH "-macOS";
#elif defined(__ANDROID__)
  return "pumpkin-aarch64-android";
#else
  return "pumpkin-" PUMPKIN_ASSET_ARCH "-Linux";
#endif
#undef PUMPKIN_ASSET_ARCH
}

char *
pumpkin_release_default_url(PumpkinReleaseChannel channel)
{
  return g_strdup_printf("https://github.com/Pumpkin-MC/Pumpkin/releases/%s/%s",
    channel == PUMPKIN_RELEASE_STABLE ? "latest/download" : "download/nightly", pumpkin_release_asset_name());
}

gboolean
pumpkin_release_is_managed_url(const char *url)
{
  if (url == NULL || *url == '\0') {
    return TRUE;
  }
  g_autoptr(GUri) uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
  if (uri == NULL || g_strcmp0(g_uri_get_scheme(uri), "https") != 0 ||
      g_ascii_strcasecmp(g_uri_get_host(uri) != NULL ? g_uri_get_host(uri) : "", "github.com") != 0 ||
      g_uri_get_userinfo(uri) != NULL || g_uri_get_port(uri) != -1 ||
      g_uri_get_query(uri) != NULL || g_uri_get_fragment(uri) != NULL) {
    return FALSE;
  }
  const char *path = g_uri_get_path(uri);
  if (path == NULL) {
    return FALSE;
  }
  const char *prefix = "/Pumpkin-MC/Pumpkin/releases/download/";
  if (g_str_has_prefix(path, prefix)) {
    const char *tag = path + strlen(prefix);
    const char *file = strchr(tag, '/');
    return file != NULL && file != tag && g_strcmp0(file + 1, pumpkin_release_asset_name()) == 0;
  }
  g_autofree char *latest = g_strdup_printf("/Pumpkin-MC/Pumpkin/releases/latest/download/%s", pumpkin_release_asset_name());
  return g_strcmp0(path, latest) == 0;
}
