#include "app-config.h"
#include <glib/gstdio.h>
#include <string.h>

static char *profile;

static void
reset_config(void)
{
  g_autofree char *path = g_build_filename(profile, "smashed-pumpkin", "config.ini", NULL);
  g_remove(path);
}

static void
test_defaults(void)
{
  reset_config();
  PumpkinConfig *config = pumpkin_config_load(NULL);
  g_assert_false(pumpkin_config_get_start_expanded(config));
  g_assert_false(pumpkin_config_get_minimap_enabled(config));
  g_assert_false(pumpkin_config_get_minimap_consent(config));
  g_assert_cmpuint(pumpkin_config_get_minimap_refresh_hz(config), ==, 2);
  g_assert_cmpint(pumpkin_config_get_start_mode(config), ==, PUMPKIN_START_MAXIMIZED);
  g_assert_cmpstr(pumpkin_config_get_start_monitor(config), ==, "");
  g_assert_cmpint(pumpkin_config_get_release_channel(config), ==, PUMPKIN_RELEASE_NIGHTLY);
  g_autofree char *nightly = pumpkin_release_default_url(PUMPKIN_RELEASE_NIGHTLY);
  g_assert_cmpstr(pumpkin_config_get_default_download_url(config), ==, nightly);
  pumpkin_config_free(config);
}

static void
test_roundtrip(void)
{
  reset_config();
  PumpkinConfig *config = pumpkin_config_load(NULL);
  pumpkin_config_set_start_minimized(config, TRUE);
  pumpkin_config_set_start_expanded(config, TRUE);
  pumpkin_config_set_start_mode(config, PUMPKIN_START_FULLSCREEN);
  pumpkin_config_set_start_monitor(config, "connector:HDMI-2");
  pumpkin_config_set_use_cache(config, FALSE);
  pumpkin_config_set_minimap_enabled(config, TRUE);
  pumpkin_config_set_minimap_consent(config, TRUE);
  pumpkin_config_set_minimap_refresh_hz(config, 20);
  pumpkin_config_set_release_channel(config, PUMPKIN_RELEASE_STABLE);
  pumpkin_config_set_default_download_url(config, "https://example.org/server");
  g_autoptr(GError) error = NULL;
  pumpkin_config_save(config, &error);
  g_assert_no_error(error);
  pumpkin_config_free(config);
  config = pumpkin_config_load(NULL);
  g_assert_true(pumpkin_config_get_start_expanded(config));
  g_assert_true(pumpkin_config_get_start_minimized(config));
  g_assert_false(pumpkin_config_should_expand_on_startup(config));
  g_assert_cmpint(pumpkin_config_get_start_mode(config), ==, PUMPKIN_START_FULLSCREEN);
  g_assert_cmpstr(pumpkin_config_get_start_monitor(config), ==, "connector:HDMI-2");
  g_assert_false(pumpkin_config_get_use_cache(config));
  g_assert_true(pumpkin_config_get_minimap_enabled(config));
  g_assert_true(pumpkin_config_get_minimap_consent(config));
  g_assert_cmpuint(pumpkin_config_get_minimap_refresh_hz(config), ==, 20);
  g_assert_cmpint(pumpkin_config_get_release_channel(config), ==, PUMPKIN_RELEASE_STABLE);
  g_assert_cmpstr(pumpkin_config_get_default_download_url(config), ==, "https://example.org/server");
  pumpkin_config_set_start_minimized(config, TRUE);
  g_assert_true(pumpkin_config_get_start_expanded(config));
  pumpkin_config_set_start_minimized(config, FALSE);
  g_assert_false(pumpkin_config_should_expand_on_startup(config));
  pumpkin_config_set_autostart_on_boot(config, TRUE);
  g_assert_true(pumpkin_config_should_expand_on_startup(config));
  pumpkin_config_set_start_minimized(config, TRUE);
  g_assert_false(pumpkin_config_should_expand_on_startup(config));
  pumpkin_config_set_start_minimized(config, FALSE);
  pumpkin_config_set_autostart_on_boot(config, FALSE);
  g_assert_false(pumpkin_config_should_expand_on_startup(config));
  pumpkin_config_free(config);
}

static void
test_invalid_mode(void)
{
  reset_config();
  PumpkinConfig *config = pumpkin_config_load(NULL);
  pumpkin_config_set_start_mode(config, (PumpkinStartMode)99);
  g_assert_cmpint(pumpkin_config_get_start_mode(config), ==, PUMPKIN_START_MAXIMIZED);
  pumpkin_config_set_start_mode(config, (PumpkinStartMode)-1);
  g_assert_cmpint(pumpkin_config_get_start_mode(config), ==, PUMPKIN_START_MAXIMIZED);
  pumpkin_config_set_start_monitor(config, NULL);
  pumpkin_config_set_minimap_refresh_hz(config, 0);
  g_assert_cmpuint(pumpkin_config_get_minimap_refresh_hz(config), ==, 2);
  pumpkin_config_set_minimap_refresh_hz(config, 999);
  g_assert_cmpuint(pumpkin_config_get_minimap_refresh_hz(config), ==, 20);
  g_assert_cmpstr(pumpkin_config_get_start_monitor(config), ==, "");
  pumpkin_config_free(config);

  g_autofree char *dir = g_build_filename(profile, "smashed-pumpkin", NULL);
  g_mkdir_with_parents(dir, 0700);
  g_autofree char *path = g_build_filename(dir, "config.ini", NULL);
  g_assert_true(g_file_set_contents(path, "[behavior]\nstart_mode=99\nstart_expanded=true\nstart_minimized=true\n", -1, NULL));
  config = pumpkin_config_load(NULL);
  g_assert_cmpint(pumpkin_config_get_start_mode(config), ==, PUMPKIN_START_MAXIMIZED);
  g_assert_true(pumpkin_config_get_start_expanded(config));
  g_assert_true(pumpkin_config_get_start_minimized(config));
  g_assert_false(pumpkin_config_should_expand_on_startup(config));
  pumpkin_config_free(config);
}

static void
test_legacy(void)
{
  reset_config();
  g_autofree char *dir = g_build_filename(profile, "smashed-pumpkin", NULL);
  g_mkdir_with_parents(dir, 0700);
  g_autofree char *path = g_build_filename(dir, "config.ini", NULL);
  g_assert_true(g_file_set_contents(path, "[behavior]\nstart_minimized=true\nautostart_on_boot=true\n", -1, NULL));
  PumpkinConfig *config = pumpkin_config_load(NULL);
  g_assert_false(pumpkin_config_get_start_expanded(config));
  g_assert_true(pumpkin_config_get_start_minimized(config));
  g_assert_true(pumpkin_config_get_autostart_on_boot(config));
  pumpkin_config_free(config);
}

static void
test_release_channels(void)
{
  reset_config();
  PumpkinConfig *config = pumpkin_config_load(NULL);
  pumpkin_config_set_release_channel(config, PUMPKIN_RELEASE_STABLE);
  g_autofree char *stable = pumpkin_release_default_url(PUMPKIN_RELEASE_STABLE);
  g_assert_cmpstr(pumpkin_config_get_default_download_url(config), ==, stable);
  pumpkin_config_save(config, NULL);
  pumpkin_config_free(config);
  config = pumpkin_config_load(NULL);
  g_assert_cmpint(pumpkin_config_get_release_channel(config), ==, PUMPKIN_RELEASE_STABLE);
  g_assert_cmpstr(pumpkin_config_get_default_download_url(config), ==, stable);
  pumpkin_config_set_default_download_url(config, "https://example.org/custom");
  pumpkin_config_set_release_channel(config, PUMPKIN_RELEASE_NIGHTLY);
  g_assert_cmpstr(pumpkin_config_get_default_download_url(config), ==, "https://example.org/custom");
  pumpkin_config_set_release_channel(config, PUMPKIN_RELEASE_STABLE);
  pumpkin_config_set_default_download_url(config, "");
  g_assert_cmpstr(pumpkin_config_get_default_download_url(config), ==, stable);
  pumpkin_config_set_release_channel(config, (PumpkinReleaseChannel)99);
  g_assert_cmpint(pumpkin_config_get_release_channel(config), ==, PUMPKIN_RELEASE_NIGHTLY);
  pumpkin_config_free(config);

  g_autofree char *dir = g_build_filename(profile, "smashed-pumpkin", NULL);
  g_autofree char *path = g_build_filename(dir, "config.ini", NULL);
  const char *channels[] = {"stable", "nightly", "invalid", ""};
  for (guint i = 0; i < G_N_ELEMENTS(channels); ++i) {
    g_autofree char *legacy_url = g_strdup_printf("https://github.com/Pumpkin-MC/Pumpkin/releases/download/0.2.0%%2B26.3-26.51/%s", pumpkin_release_asset_name());
    g_autofree char *contents = g_strdup_printf("[updates]\nrelease_channel=%s\ndefault_download_url=%s\n", channels[i], legacy_url);
    g_assert_true(g_file_set_contents(path, contents, -1, NULL));
    config = pumpkin_config_load(NULL);
    PumpkinReleaseChannel expected = i == 0 ? PUMPKIN_RELEASE_STABLE : PUMPKIN_RELEASE_NIGHTLY;
    g_autofree char *expected_url = pumpkin_release_default_url(expected);
    g_assert_cmpint(pumpkin_config_get_release_channel(config), ==, expected);
    g_assert_cmpstr(pumpkin_config_get_default_download_url(config), ==, expected_url);
    pumpkin_config_free(config);
  }
  g_assert_true(g_file_set_contents(path, "[updates]\ndefault_download_url=https://example.org/custom\n", -1, NULL));
  config = pumpkin_config_load(NULL);
  g_assert_cmpint(pumpkin_config_get_release_channel(config), ==, PUMPKIN_RELEASE_NIGHTLY);
  g_assert_cmpstr(pumpkin_config_get_default_download_url(config), ==, "https://example.org/custom");
  pumpkin_config_free(config);
}

#ifdef __linux__
static void
test_autostart_flags(void)
{
  reset_config();
  PumpkinConfig *config = pumpkin_config_load(NULL);
  pumpkin_config_set_autostart_on_boot(config, TRUE);
  pumpkin_config_set_start_minimized(config, TRUE);
  pumpkin_config_save(config, NULL);
  pumpkin_config_manage_autostart_desktop(TRUE);
  g_autofree char *path = g_build_filename(profile, "autostart", "dev.rotstein.SmashedPumpkin.desktop", NULL);
  g_autofree char *contents = NULL;
  g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
  g_assert_nonnull(strstr(contents, "--minimized"));
  pumpkin_config_set_start_expanded(config, TRUE);
  pumpkin_config_set_start_minimized(config, FALSE);
  pumpkin_config_save(config, NULL);
  pumpkin_config_manage_autostart_desktop(TRUE);
  g_clear_pointer(&contents, g_free);
  g_assert_true(g_file_get_contents(path, &contents, NULL, NULL));
  g_assert_null(strstr(contents, "--minimized"));
  pumpkin_config_manage_autostart_desktop(FALSE);
  g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
  g_autofree char *dir = g_path_get_dirname(path);
  g_rmdir(dir);
  pumpkin_config_free(config);
}
#endif

int
main(int argc, char **argv)
{
  profile = g_dir_make_tmp("smashed-pumpkin-startup-tests-XXXXXX", NULL);
  g_assert_nonnull(profile);
  g_setenv("HOME", profile, TRUE);
  g_setenv("XDG_CONFIG_HOME", profile, TRUE);
  g_setenv("XDG_DATA_HOME", profile, TRUE);
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/startup/defaults", test_defaults);
  g_test_add_func("/startup/roundtrip", test_roundtrip);
  g_test_add_func("/startup/invalid-mode", test_invalid_mode);
  g_test_add_func("/startup/legacy", test_legacy);
  g_test_add_func("/startup/release-channels", test_release_channels);
#ifdef __linux__
  g_test_add_func("/startup/autostart-flags", test_autostart_flags);
#endif
  int result = g_test_run();
  reset_config();
  g_autofree char *config_dir = g_build_filename(profile, "smashed-pumpkin", NULL);
  g_autofree char *servers_dir = g_build_filename(config_dir, "servers", NULL);
  g_rmdir(servers_dir);
  g_rmdir(config_dir);
  g_rmdir(profile);
  g_free(profile);
  return result;
}
