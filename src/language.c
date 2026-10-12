#include "language.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

static const char *const language_ids[] = { "system", "en", "zh_CN", "de" };

gboolean
pumpkin_language_is_valid(const char *id)
{
  for (guint i = 0; i < G_N_ELEMENTS(language_ids); i++) {
    if (g_strcmp0(id, language_ids[i]) == 0) return TRUE;
  }
  return FALSE;
}

guint
pumpkin_language_index(const char *id)
{
  for (guint i = 0; i < G_N_ELEMENTS(language_ids); i++) {
    if (g_strcmp0(id, language_ids[i]) == 0) return i;
  }
  return 1; /* Unknown persisted values display the safe English fallback. */
}

const char *
pumpkin_language_from_index(guint index)
{
  return index < G_N_ELEMENTS(language_ids) ? language_ids[index] : "en";
}

const char *
pumpkin_language_detect(const char *const *preferences)
{
  for (const char *const *p = preferences; p != NULL && *p != NULL; p++) {
    g_autofree char *tag = g_ascii_strdown(*p, -1);
    for (char *c = tag; *c != '\0'; c++) {
      if (*c == '-') *c = '_';
      if (*c == '.' || *c == '@') { *c = '\0'; break; }
    }
    if (g_str_equal(tag, "c") || g_str_equal(tag, "posix")) return "en";
    if (g_str_equal(tag, "en") || g_str_has_prefix(tag, "en_")) return "en";
    if (g_str_equal(tag, "de") || g_str_has_prefix(tag, "de_")) return "de";
    /* Do not silently treat Traditional Chinese as Simplified Chinese. */
    if (g_str_equal(tag, "zh_cn") || g_str_equal(tag, "zh_sg") ||
        g_str_equal(tag, "zh_hans") || g_str_has_prefix(tag, "zh_hans_")) return "zh_CN";
  }
  return "en";
}

static const char *
detect_environment(void)
{
  const char *messages = g_getenv("LC_ALL");
  if (messages == NULL || *messages == '\0') messages = g_getenv("LC_MESSAGES");
  if (messages == NULL || *messages == '\0') messages = g_getenv("LANG");
  /* An explicit POSIX/C locale requests source English, even with LANGUAGE set.
   * C.UTF-8 is a usable character locale, not an explicit language selection. */
  if (g_strcmp0(messages, "C") == 0 || g_strcmp0(messages, "POSIX") == 0) return "en";
  const char *language = g_getenv("LANGUAGE");
  if (language != NULL && *language != '\0') {
    g_auto(GStrv) preferences = g_strsplit(language, ":", -1);
    return pumpkin_language_detect((const char *const *)preferences);
  }
  if (messages != NULL && *messages != '\0') {
    const char *preferences[] = { messages, NULL };
    return pumpkin_language_detect(preferences);
  }
#ifdef G_OS_WIN32
  WCHAR name[LOCALE_NAME_MAX_LENGTH];
  if (GetUserDefaultLocaleName(name, G_N_ELEMENTS(name)) > 0) {
    g_autofree char *tag = g_utf16_to_utf8((const gunichar2 *)name, -1, NULL, NULL, NULL);
    const char *preferences[] = { tag, NULL };
    return pumpkin_language_detect(preferences);
  }
#endif
  return "en";
}

char *
pumpkin_language_prepare(GError **error)
{
  g_autofree char *path = g_build_filename(g_get_user_config_dir(), "smashed-pumpkin", "config.ini", NULL);
  g_autoptr(GKeyFile) key = g_key_file_new();
  g_autoptr(GError) load_error = NULL;
  if (!g_key_file_load_from_file(key, path, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                 &load_error) &&
      !g_error_matches(load_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
    g_propagate_error(error, g_steal_pointer(&load_error));
    return g_strdup("en");
  }
  g_autoptr(GError) value_error = NULL;
  g_autofree char *saved = g_key_file_get_string(key, "behavior", "language", &value_error);
  if (value_error != NULL &&
      !g_error_matches(value_error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) &&
      !g_error_matches(value_error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
    g_propagate_error(error, g_steal_pointer(&value_error));
    return g_strdup("en");
  }
  if (saved != NULL && *saved != '\0') {
    if (g_str_equal(saved, "system")) return g_strdup(detect_environment());
    return g_strdup(pumpkin_language_is_valid(saved) ? saved : "en");
  }

  const char *detected = detect_environment();
  g_key_file_set_string(key, "behavior", "language", detected);
  g_autofree char *dir = g_path_get_dirname(path);
  if (g_mkdir_with_parents(dir, 0700) != 0) {
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                "Cannot create language configuration directory: %s", g_strerror(errno));
  } else {
    g_autofree char *data = g_key_file_to_data(key, NULL, NULL);
    g_file_set_contents_full(path, data, -1, G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE,
                             0600, error);
  }
  return g_strdup(detected);
}
