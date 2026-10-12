#include "language.h"
#include "app-config.h"
#include <glib/gstdio.h>
#include <string.h>
#ifdef G_OS_UNIX
#include <signal.h>
#include <sys/resource.h>
#endif

static char *profile;
static char *path;

static void
write_config(const char *text)
{
  g_autofree char *dir = g_path_get_dirname(path);
  g_assert_cmpint(g_mkdir_with_parents(dir, 0700), ==, 0);
  if (text == NULL) { g_remove(path); return; }
  g_assert_true(g_file_set_contents(path, text, -1, NULL));
}

static void
expect_prepare(const char *id)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *effective = pumpkin_language_prepare(&error);
  g_assert_no_error(error);
  g_assert_cmpstr(effective, ==, id);
}

static void
test_detection(void)
{
  const struct { const char *tag; const char *id; } cases[] = {
    {"de_DE.UTF-8", "de"}, {"DE-ch", "de"}, {"en_GB", "en"},
    {"zh_CN.UTF-8", "zh_CN"}, {"zh-SG", "zh_CN"}, {"zh-Hans-CN", "zh_CN"},
    {"zh-Hans", "zh_CN"}, {"zh_TW", "en"}, {"zh-Hant", "en"},
    {"zh_HK", "en"}, {"fr_FR", "en"}, {"C", "en"}, {"POSIX", "en"},
    {"C.UTF-8", "en"}, {"deceptive", "en"}, {"english", "en"}, {"", "en"}
  };
  for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
    const char *preferences[] = { cases[i].tag, NULL };
    g_assert_cmpstr(pumpkin_language_detect(preferences), ==, cases[i].id);
  }
  const char *chain[] = { "fr_FR", "zh_TW", "de_AT", "en", NULL };
  g_assert_cmpstr(pumpkin_language_detect(chain), ==, "de");
  g_assert_cmpstr(pumpkin_language_detect(NULL), ==, "en");
  const char *ids[] = { "system", "en", "zh_CN", "de" };
  for (guint i = 0; i < G_N_ELEMENTS(ids); i++) {
    g_assert_true(pumpkin_language_is_valid(ids[i]));
    g_assert_cmpuint(pumpkin_language_index(ids[i]), ==, i);
    g_assert_cmpstr(pumpkin_language_from_index(i), ==, ids[i]);
  }
  g_assert_false(pumpkin_language_is_valid(NULL));
  g_assert_false(pumpkin_language_is_valid("Deutsch"));
  g_assert_cmpstr(pumpkin_language_from_index(G_MAXUINT), ==, "en");
}

static void
test_first_launch(void)
{
  write_config("# Preserve this comment\n[custom]\nunknown=original\n[behavior]\nstart_minimized=true\n");
  g_setenv("LC_ALL", "en_US.UTF-8", TRUE);
  g_setenv("LANGUAGE", "fr:de_DE:en", TRUE);
  expect_prepare("de");
  g_autofree char *data = NULL;
  g_assert_true(g_file_get_contents(path, &data, NULL, NULL));
  g_assert_nonnull(strstr(data, "# Preserve this comment"));
  g_assert_nonnull(strstr(data, "unknown=original"));
  g_assert_nonnull(strstr(data, "language=de"));
  g_setenv("LANGUAGE", "zh_CN", TRUE);
  expect_prepare("de"); /* Never replace the first saved selection. */
  PumpkinConfig *config = pumpkin_config_load(NULL);
  g_assert_cmpstr(pumpkin_config_get_language(config), ==, "de");
  g_assert_true(pumpkin_config_get_start_minimized(config));
  pumpkin_config_set_language(config, "zh_CN");
  g_autoptr(GError) error = NULL;
  pumpkin_config_save(config, &error);
  g_assert_no_error(error);
  pumpkin_config_free(config);
  config = pumpkin_config_load(NULL);
  g_assert_cmpstr(pumpkin_config_get_language(config), ==, "zh_CN");
  pumpkin_config_free(config);
  g_clear_pointer(&data, g_free);
  g_assert_true(g_file_get_contents(path, &data, NULL, NULL));
  g_assert_nonnull(strstr(data, "unknown=original"));
  g_assert_nonnull(strstr(data, "# Preserve this comment"));
}

static void
test_system_and_fallback(void)
{
  write_config("[behavior]\nlanguage=system\n");
  g_setenv("LC_ALL", "C.UTF-8", TRUE);
  g_setenv("LANGUAGE", "de", TRUE);
  expect_prepare("de");
  g_setenv("LANGUAGE", "zh-Hans", TRUE);
  expect_prepare("zh_CN");
  g_setenv("LANGUAGE", "unsupported", TRUE);
  expect_prepare("en");
  g_setenv("LC_ALL", "C", TRUE);
  g_setenv("LANGUAGE", "de", TRUE);
  expect_prepare("en");
  g_autofree char *data = NULL;
  g_assert_true(g_file_get_contents(path, &data, NULL, NULL));
  g_assert_cmpstr(data, ==, "[behavior]\nlanguage=system\n");
  write_config("[behavior]\nlanguage=zh_CN\n");
  expect_prepare("zh_CN"); /* Manual choice overrides an explicit C locale. */
  write_config("[behavior]\nlanguage=invalid-id\n");
  expect_prepare("en");
  g_clear_pointer(&data, g_free);
  g_assert_true(g_file_get_contents(path, &data, NULL, NULL));
  g_assert_nonnull(strstr(data, "language=invalid-id"));
  write_config("[behavior]\nlanguage=\n");
  g_setenv("LC_ALL", "zh_CN.UTF-8", TRUE);
  g_unsetenv("LANGUAGE");
  expect_prepare("zh_CN");
  write_config(NULL);
  g_setenv("LC_ALL", "fr_FR.UTF-8", TRUE);
  expect_prepare("en");
}

static void
test_damaged_config(void)
{
  const char *broken = g_str_has_suffix(g_test_get_path(), "/value-parse-error")
    ? "[behavior]\nlanguage=de\\q\n" : "This is not an INI file\n";
  write_config(broken);
  g_autoptr(GError) error = NULL;
  g_autofree char *id = pumpkin_language_prepare(&error);
  g_assert_nonnull(error);
  g_assert_cmpstr(id, ==, "en");
  g_clear_error(&error);
  PumpkinConfig *config = pumpkin_config_load(&error);
  g_assert_nonnull(error);
  g_clear_error(&error);
  pumpkin_config_set_language(config, "de");
  pumpkin_config_save(config, &error);
  g_assert_nonnull(error);
  pumpkin_config_free(config);
  g_autofree char *data = NULL;
  g_assert_true(g_file_get_contents(path, &data, NULL, NULL));
  g_assert_cmpstr(data, ==, broken);
}

static void
test_save_failure(void)
{
  write_config(NULL);
  g_assert_cmpint(g_mkdir(path, 0700), ==, 0); /* A directory cannot become an INI file. */
  g_autoptr(GError) error = NULL;
  g_autofree char *id = pumpkin_language_prepare(&error);
  g_assert_nonnull(error);
  g_assert_cmpstr(id, ==, "en");
  g_assert_true(g_file_test(path, G_FILE_TEST_IS_DIR));
  g_assert_cmpint(g_rmdir(path), ==, 0);
  g_autofree char *dir = g_path_get_dirname(path);
  g_autofree char *servers = g_build_filename(dir, "servers", NULL);
  g_rmdir(servers);
  g_assert_cmpint(g_rmdir(dir), ==, 0);
  g_assert_true(g_file_set_contents(dir, "obstruction", -1, NULL));
  g_clear_error(&error);
  g_clear_pointer(&id, g_free);
  g_setenv("LC_ALL", "C.UTF-8", TRUE);
  g_setenv("LANGUAGE", "de", TRUE);
  id = pumpkin_language_prepare(&error);
  g_assert_nonnull(error);
  g_assert_cmpstr(id, ==, "en"); /* Unreadable path is not a first-run profile. */
  g_assert_cmpint(g_remove(dir), ==, 0);
}

static void
test_language_save_rollback(void)
{
  const char *original = "[behavior]\nlanguage=unsupported-id\n[custom]\nunknown=original\n";
  write_config(original);
  PumpkinConfig *config = pumpkin_config_load(NULL);
  g_assert_cmpint(g_remove(path), ==, 0);
  g_assert_cmpint(g_mkdir(path, 0700), ==, 0);
  g_autoptr(GError) error = NULL;
  g_assert_false(pumpkin_config_save_language(config, "de", &error));
  g_assert_nonnull(error);
  g_assert_cmpstr(pumpkin_config_get_language(config), ==, "unsupported-id");
  g_assert_cmpint(g_rmdir(path), ==, 0);
  write_config(original);
  g_clear_error(&error);
  g_assert_true(pumpkin_config_save_language(config, "de", &error));
  g_assert_no_error(error);
  g_assert_cmpstr(pumpkin_config_get_language(config), ==, "de");
  pumpkin_config_free(config);
  g_autofree char *data = NULL;
  g_assert_true(g_file_get_contents(path, &data, NULL, NULL));
  g_assert_nonnull(strstr(data, "unknown=original"));
}

static void
test_first_write_failure(void)
{
#ifdef G_OS_UNIX
  if (!g_test_subprocess()) {
    g_test_trap_subprocess(NULL, 5000000, 0);
    g_test_trap_assert_passed();
    return;
  }
  const char *original = "[custom]\nunknown=original\n";
  write_config(original);
  g_setenv("LC_ALL", "C.UTF-8", TRUE);
  g_setenv("LANGUAGE", "de", TRUE);
  /* Force an atomic-write error even when the test runs as root. */
  struct rlimit limit = { 1, 1 };
  g_assert_cmpint(setrlimit(RLIMIT_FSIZE, &limit), ==, 0);
  signal(SIGXFSZ, SIG_IGN);
  g_autoptr(GError) error = NULL;
  g_autofree char *id = pumpkin_language_prepare(&error);
  g_assert_nonnull(error);
  g_assert_cmpstr(id, ==, "de");
  g_autofree char *data = NULL;
  g_assert_true(g_file_get_contents(path, &data, NULL, NULL));
  g_assert_cmpstr(data, ==, original);
#else
  g_test_skip("Requires a Unix file-size resource limit");
#endif
}

int
main(int argc, char **argv)
{
  profile = g_dir_make_tmp("pumpkin-language-test-XXXXXX", NULL);
  g_assert_nonnull(profile);
  g_setenv("HOME", profile, TRUE);
  g_setenv("XDG_CONFIG_HOME", profile, TRUE);
  g_setenv("XDG_DATA_HOME", profile, TRUE);
  path = g_build_filename(profile, "smashed-pumpkin", "config.ini", NULL);
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/language/detection", test_detection);
  g_test_add_func("/language/first-launch", test_first_launch);
  g_test_add_func("/language/system-and-fallback", test_system_and_fallback);
  g_test_add_func("/language/damaged-config", test_damaged_config);
  g_test_add_func("/language/value-parse-error", test_damaged_config);
  g_test_add_func("/language/first-write-failure", test_first_write_failure);
  g_test_add_func("/language/save-failure", test_save_failure);
  g_test_add_func("/language/save-rollback", test_language_save_rollback);
  int result = g_test_run();
  g_remove(path);
  g_autofree char *dir = g_path_get_dirname(path);
  g_rmdir(dir);
  g_autofree char *servers = g_build_filename(dir, "servers", NULL);
  g_rmdir(servers);
  g_rmdir(dir);
  g_rmdir(profile);
  g_free(path);
  g_free(profile);
  return result;
}
