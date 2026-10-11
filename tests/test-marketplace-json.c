#include "vendor/cjson/cJSON.h"

#include <glib.h>
#include <locale.h>
#include <string.h>

static void
test_decimal_locale(gconstpointer data)
{
  const char *const *candidates = data;
  g_autofree char *original = g_strdup(setlocale(LC_NUMERIC, NULL));
  gboolean available = FALSE;
  for (guint i = 0; candidates[i] != NULL; ++i) {
    if (setlocale(LC_NUMERIC, candidates[i]) != NULL) {
      available = TRUE;
      break;
    }
  }
  if (!available) {
    g_test_skip("Requested numeric locale is not installed on this system.");
    return;
  }

  const char *json = "[{\"id\":104,\"name\":\"Example\",\"price\":0.0,"
                     "\"values\":[12.5,-0.125,1.25e2,4e-3]}]";
  cJSON *root = cJSON_ParseWithOpts(json, NULL, TRUE);
  g_assert_nonnull(root);
  g_assert_true(cJSON_IsArray(root));
  const cJSON *plugin = cJSON_GetArrayItem(root, 0);
  const cJSON *price = cJSON_GetObjectItemCaseSensitive(plugin, "price");
  g_assert_true(cJSON_IsNumber(price));
  g_assert_cmpfloat(price->valuedouble, ==, 0.0);
  const cJSON *values = cJSON_GetObjectItemCaseSensitive(plugin, "values");
  const double expected[] = {12.5, -0.125, 125.0, 0.004};
  for (guint i = 0; i < G_N_ELEMENTS(expected); ++i) {
    const cJSON *value = cJSON_GetArrayItem(values, (int)i);
    g_assert_true(cJSON_IsNumber(value));
    g_assert_cmpfloat_with_epsilon(value->valuedouble, expected[i], 1e-9);
  }

  char *printed = cJSON_PrintUnformatted(root);
  g_assert_nonnull(printed);
  g_assert_nonnull(strstr(printed, "12.5"));
  cJSON *roundtrip = cJSON_ParseWithOpts(printed, NULL, TRUE);
  g_assert_true(cJSON_IsArray(roundtrip));
  cJSON_Delete(roundtrip);
  cJSON_free(printed);
  cJSON_Delete(root);

  /* JSON syntax still requires a decimal point, never a locale-specific comma. */
  cJSON *invalid = cJSON_ParseWithOpts("[{\"price\":12,5}]", NULL, TRUE);
  g_assert_null(invalid);
  g_assert_nonnull(setlocale(LC_NUMERIC, original));
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  static const char *const c_locales[] = {"C", NULL};
  static const char *const german_locales[] = {
    "de_DE.UTF-8", "de_DE.utf8", "German_Germany.1252", NULL};
  static const char *const french_locales[] = {
    "fr_FR.UTF-8", "fr_FR.utf8", "French_France.1252", NULL};
  g_test_add_data_func("/marketplace-json/c-locale", c_locales, test_decimal_locale);
  g_test_add_data_func("/marketplace-json/german-locale", german_locales, test_decimal_locale);
  g_test_add_data_func("/marketplace-json/french-locale", french_locales, test_decimal_locale);
  return g_test_run();
}
