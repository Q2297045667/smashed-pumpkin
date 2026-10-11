#include "download.h"
#include "release-metadata.h"
#include "vendor/cjson/cJSON.h"
#include <string.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(cJSON, cJSON_Delete)

static const char *asset_name = "pumpkin-X64-Linux";
static const char *digest = "sha256:bff2aaede4f8695b688aa7e088f31f9e762ee567c1d6f9f15da64ad63ba66d64";

static cJSON *fixture(const char *tag, const char *asset)
{
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "tag_name", tag);
  cJSON_AddBoolToObject(root, "draft", FALSE);
  cJSON_AddBoolToObject(root, "prerelease", g_str_equal(tag, "nightly"));
  cJSON_AddStringToObject(root, "published_at", "2026-03-25T15:30:45Z");
  cJSON *assets = cJSON_AddArrayToObject(root, "assets");
  cJSON *entry = cJSON_CreateObject();
  cJSON_AddItemToArray(assets, entry);
  cJSON_AddStringToObject(entry, "name", asset);
  cJSON_AddNumberToObject(entry, "id", 618027230);
  cJSON_AddStringToObject(entry, "state", "uploaded");
  cJSON_AddStringToObject(entry, "updated_at", "2026-10-07T08:08:12Z");
  cJSON_AddStringToObject(entry, "digest", digest);
  g_autofree char *escaped = g_uri_escape_string(tag, NULL, FALSE);
  g_autofree char *url = g_strdup_printf("https://github.com/Pumpkin-MC/Pumpkin/releases/download/%s/%s", escaped, asset);
  cJSON_AddStringToObject(entry, "browser_download_url", url);
  return root;
}

static cJSON *first_asset(cJSON *root)
{
  return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "assets"), 0);
}

static PumpkinResolvedDownload *parse(cJSON *root, PumpkinReleaseChannel channel, const char *asset, GError **error)
{
  char *json = cJSON_PrintUnformatted(root);
  PumpkinResolvedDownload *resolved = pumpkin_release_parse_metadata(json, channel, asset, error);
  cJSON_free(json);
  return resolved;
}

static void test_nightly(void)
{
  g_autoptr(cJSON) root = fixture("nightly", asset_name);
  g_autoptr(GError) error = NULL;
  g_autoptr(PumpkinResolvedDownload) resolved = parse(root, PUMPKIN_RELEASE_NIGHTLY, asset_name, &error);
  g_assert_no_error(error);
  g_assert_nonnull(resolved);
  g_assert_cmpstr(resolved->build_id, ==, digest);
  g_assert_cmpstr(resolved->build_label, ==, "Nightly 2026-10-07 08:08 UTC");
  g_assert_cmpint(resolved->result_code, ==, PUMPKIN_DOWNLOAD_OK);
  g_assert_cmpstr(resolved->url, ==, "https://github.com/Pumpkin-MC/Pumpkin/releases/download/nightly/pumpkin-X64-Linux");

  cJSON_ReplaceItemInObjectCaseSensitive(first_asset(root), "digest", cJSON_CreateString(
    "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
  g_autoptr(PumpkinResolvedDownload) changed = parse(root, PUMPKIN_RELEASE_NIGHTLY, asset_name, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(changed->build_id, !=, resolved->build_id);
}

static void test_stable(void)
{
  g_autoptr(cJSON) root = fixture("0.2.0+26.3-26.51", asset_name);
  g_autoptr(GError) error = NULL;
  g_autoptr(PumpkinResolvedDownload) resolved = parse(root, PUMPKIN_RELEASE_STABLE, asset_name, &error);
  g_assert_no_error(error);
  g_assert_nonnull(resolved);
  g_assert_cmpstr(resolved->build_label, ==, "Stable 0.2.0+26.3-26.51");
  g_assert_nonnull(strstr(resolved->url, "0.2.0%2B26.3-26.51/"));
}

static void test_asset_identity_fallback(void)
{
  g_autoptr(cJSON) root = fixture("nightly", asset_name);
  cJSON_ReplaceItemInObjectCaseSensitive(first_asset(root), "digest", cJSON_CreateNull());
  g_autoptr(GError) error = NULL;
  g_autoptr(PumpkinResolvedDownload) before = parse(root, PUMPKIN_RELEASE_NIGHTLY, asset_name, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(before->build_id, ==, "github:nightly:618027230:2026-10-07T08:08:12Z");
  cJSON_ReplaceItemInObjectCaseSensitive(first_asset(root), "updated_at", cJSON_CreateString("2026-10-08T08:08:12Z"));
  g_autoptr(PumpkinResolvedDownload) after = parse(root, PUMPKIN_RELEASE_NIGHTLY, asset_name, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(after->build_id, !=, before->build_id);
  cJSON_ReplaceItemInObjectCaseSensitive(first_asset(root), "id", cJSON_CreateNumber(618027231));
  g_autoptr(PumpkinResolvedDownload) replaced = parse(root, PUMPKIN_RELEASE_NIGHTLY, asset_name, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(replaced->build_id, !=, after->build_id);
}

static void test_platform_assets(void)
{
  const char *names[] = {"pumpkin-X64-Linux", "pumpkin-ARM64-Linux", "pumpkin-X64-Windows.exe",
    "pumpkin-ARM64-Windows.exe", "pumpkin-X64-macOS", "pumpkin-ARM64-macOS", "pumpkin-aarch64-android"};
  for (guint i = 0; i < G_N_ELEMENTS(names); ++i) {
    g_autoptr(cJSON) root = fixture("nightly", names[i]);
    g_autoptr(GError) error = NULL;
    g_autoptr(PumpkinResolvedDownload) resolved = parse(root, PUMPKIN_RELEASE_NIGHTLY, names[i], &error);
    g_assert_no_error(error);
    g_assert_true(g_str_has_suffix(resolved->url, names[i]));
  }
}

static void test_missing_platform(void)
{
  const char *wrong_assets[] = {"pumpkin-ARM64-Linux", "pumpkin-X64-Linux-musl", "pumpkin-ARM64-macOS"};
  for (guint i = 0; i < G_N_ELEMENTS(wrong_assets); ++i) {
    g_autoptr(cJSON) root = fixture("0.2.0+26.3-26.51", wrong_assets[i]);
    g_autoptr(GError) error = NULL;
    g_autoptr(PumpkinResolvedDownload) resolved = parse(root, PUMPKIN_RELEASE_STABLE, asset_name, &error);
    g_assert_null(resolved);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  }
}

static void test_reject_mismatched_channel(void)
{
  const char *tags[] = {"nightly", "canary", "../escape", "v1/path", "v1?query", "v1#fragment", "v1\n"};
  for (guint i = 0; i < G_N_ELEMENTS(tags); ++i) {
    g_autoptr(cJSON) root = fixture(tags[i], asset_name);
    g_autoptr(GError) error = NULL;
    g_autoptr(PumpkinResolvedDownload) resolved = parse(root, PUMPKIN_RELEASE_STABLE, asset_name, &error);
    g_assert_null(resolved);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  }
  const char *flags[] = {"draft", "prerelease"};
  for (guint i = 0; i < G_N_ELEMENTS(flags); ++i) {
    g_autoptr(cJSON) root = fixture("v1.0.0", asset_name);
    cJSON_ReplaceItemInObjectCaseSensitive(root, flags[i], cJSON_CreateTrue());
    g_autoptr(GError) error = NULL;
    g_autoptr(PumpkinResolvedDownload) resolved = parse(root, PUMPKIN_RELEASE_STABLE, asset_name, &error);
    g_assert_null(resolved);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  }
  g_autoptr(cJSON) root = fixture("v1.0.0", asset_name);
  g_autoptr(GError) error = NULL;
  g_autoptr(PumpkinResolvedDownload) resolved = parse(root, PUMPKIN_RELEASE_NIGHTLY, asset_name, &error);
  g_assert_null(resolved);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}

static void test_invalid_json(void)
{
  const char *values[] = {NULL, "", "<html>Not Found</html>", "null", "[]", "{}", "{\"message\":\"rate limited\"}", "{} trailing"};
  for (guint i = 0; i < G_N_ELEMENTS(values); ++i) {
    g_autoptr(GError) error = NULL;
    g_autoptr(PumpkinResolvedDownload) resolved = pumpkin_release_parse_metadata(values[i], PUMPKIN_RELEASE_NIGHTLY, asset_name, &error);
    g_assert_null(resolved);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  }
}

static void test_invalid_asset(void)
{
  const char *keys[] = {"browser_download_url", "browser_download_url", "updated_at", "state", "id", "id"};
  const char *values[] = {"https://example.org/server", "https://github.com/Pumpkin-MC/Pumpkin/releases/download/nightly/pumpkin-X64-Linux?token=x",
    "not a timestamp", "new", NULL, NULL};
  for (guint i = 0; i < G_N_ELEMENTS(keys); ++i) {
    g_autoptr(cJSON) root = fixture("nightly", asset_name);
    cJSON *value = values[i] != NULL ? cJSON_CreateString(values[i]) : cJSON_CreateNumber(i == 4 ? -1 : 1.5);
    cJSON_ReplaceItemInObjectCaseSensitive(first_asset(root), keys[i], value);
    g_autoptr(GError) error = NULL;
    g_autoptr(PumpkinResolvedDownload) resolved = parse(root, PUMPKIN_RELEASE_NIGHTLY, asset_name, &error);
    g_assert_null(resolved);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  }
}

static void test_url_helpers(void)
{
  g_autofree char *nightly = pumpkin_release_default_url(PUMPKIN_RELEASE_NIGHTLY);
  g_autofree char *stable = pumpkin_release_default_url(PUMPKIN_RELEASE_STABLE);
  g_assert_true(pumpkin_release_is_managed_url(nightly));
  g_assert_true(pumpkin_release_is_managed_url(stable));
  g_assert_true(pumpkin_release_is_managed_url(NULL));
  g_assert_true(pumpkin_release_is_managed_url(""));
  g_autofree char *versioned = g_strdup_printf("https://github.com/Pumpkin-MC/Pumpkin/releases/download/0.2.0%%2B26.3-26.51/%s", pumpkin_release_asset_name());
  g_assert_true(pumpkin_release_is_managed_url(versioned));
  g_assert_false(pumpkin_release_is_managed_url("https://github.com"));
  g_assert_false(pumpkin_release_is_managed_url("https://example.org/server"));
  g_autofree char *fork = g_strdup_printf("https://github.com/other/Pumpkin/releases/download/nightly/%s", pumpkin_release_asset_name());
  g_assert_false(pumpkin_release_is_managed_url(fork));
  g_autofree char *query = g_strconcat(nightly, "?custom=1", NULL);
  g_assert_false(pumpkin_release_is_managed_url(query));
  g_assert_nonnull(strstr(nightly, "/download/nightly/"));
  g_assert_nonnull(strstr(stable, "/latest/download/"));
}

typedef struct {
  GMainLoop *loop;
  PumpkinResolvedDownload *resolved;
  GError *error;
} AsyncResult;

static void on_resolved(GObject *object, GAsyncResult *result, gpointer user_data)
{
  (void)object;
  AsyncResult *state = user_data;
  state->resolved = pumpkin_resolve_latest_finish(result, NULL, &state->error);
  g_main_loop_quit(state->loop);
}

static void test_cancelled(void)
{
  g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
  g_autoptr(GCancellable) cancel = g_cancellable_new();
  g_cancellable_cancel(cancel);
  AsyncResult state = {.loop = loop};
  pumpkin_resolve_channel_async(PUMPKIN_RELEASE_STABLE, cancel, on_resolved, &state);
  g_main_loop_run(loop);
  g_assert_null(state.resolved);
  g_assert_error(state.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error(&state.error);
}

int main(int argc, char **argv)
{
  /* Opt-in probe exercises the production HTTP resolver without downloading a binary. */
  if (argc == 3 && g_str_equal(argv[1], "--probe")) {
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    AsyncResult state = {.loop = loop};
    PumpkinReleaseChannel channel = g_str_equal(argv[2], "stable") ? PUMPKIN_RELEASE_STABLE : PUMPKIN_RELEASE_NIGHTLY;
    pumpkin_resolve_channel_async(channel, NULL, on_resolved, &state);
    g_main_loop_run(loop);
    if (state.error != NULL) {
      g_printerr("%s\n", state.error->message);
      g_clear_error(&state.error);
      return 1;
    }
    g_print("%s\n%s\n%s\n", state.resolved->build_label, state.resolved->build_id, state.resolved->url);
    pumpkin_resolved_download_free(state.resolved);
    return 0;
  }
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/releases/nightly-asset-not-release-date", test_nightly);
  g_test_add_func("/releases/stable", test_stable);
  g_test_add_func("/releases/identity-fallback", test_asset_identity_fallback);
  g_test_add_func("/releases/platform-assets", test_platform_assets);
  g_test_add_func("/releases/missing-platform", test_missing_platform);
  g_test_add_func("/releases/reject-mismatched-channel", test_reject_mismatched_channel);
  g_test_add_func("/releases/invalid-json", test_invalid_json);
  g_test_add_func("/releases/invalid-asset", test_invalid_asset);
  g_test_add_func("/releases/url-helpers", test_url_helpers);
  g_test_add_func("/releases/cancelled", test_cancelled);
  return g_test_run();
}
