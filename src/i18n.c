#include "i18n.h"

#include <locale.h>
#include "language.h"

#ifdef LC_MESSAGES
#define MESSAGE_CATEGORY LC_MESSAGES
#else
/* The Windows CRT does not expose LC_MESSAGES. */
#define MESSAGE_CATEGORY LC_ALL
#endif

static const char *session_language;

const char *
pumpkin_i18n_get_language(void)
{
  return session_language;
}

void
pumpkin_i18n_init_language(const char *language)
{
  /* This is startup-only: do not mutate the environment once workers exist. */
  session_language = pumpkin_language_from_index(pumpkin_language_index(language));
  if (g_str_equal(session_language, "system")) session_language = "en";
  g_setenv("LANGUAGE", session_language, TRUE);
  pumpkin_i18n_init();

  const char *messages = setlocale(MESSAGE_CATEGORY, NULL);
  if (!g_str_equal(session_language, "en") &&
      (messages == NULL || g_str_has_prefix(messages, "C") || g_str_equal(messages, "POSIX"))) {
#ifdef HAVE_PRIVATE_MESSAGE_LOCALE
    const char *root = g_getenv("SMASHED_PUMPKIN_MESSAGE_LOCALEDIR");
    if (root == NULL || !g_path_is_absolute(root)) root = MESSAGE_LOCALE_ROOT;
    g_autofree char *old_path = g_strdup(g_getenv("LOCPATH"));
    g_autofree char *path = old_path != NULL && *old_path != '\0'
      ? g_strconcat(root, G_SEARCHPATH_SEPARATOR_S, old_path, NULL) : g_strdup(root);
    g_setenv("LOCPATH", path, TRUE);
    if (setlocale(MESSAGE_CATEGORY, "smashed.UTF-8") == NULL) {
      if (old_path != NULL) g_setenv("LOCPATH", old_path, TRUE);
      else g_unsetenv("LOCPATH");
      setlocale(MESSAGE_CATEGORY, "en_US.UTF-8");
    }
#else
    /* On libintl platforms, an available non-C locale enables LANGUAGE. */
    setlocale(MESSAGE_CATEGORY, "en_US.UTF-8");
#endif
  }
  setlocale(LC_NUMERIC, "C");
}

void
pumpkin_i18n_startup(void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *language = pumpkin_language_prepare(&error);
  if (error != NULL) g_printerr("Language configuration: %s\n", error->message);
  pumpkin_i18n_init_language(language);
}

void
pumpkin_i18n_init(void)
{
  /* Respect the process environment for messages, character handling and time.
   * An invalid system locale leaves the existing C locale as the safe fallback.
   * Wire formats, JSON, commands and machine-readable numbers must use dots. */
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");

  const char *locale_dir = g_getenv("SMASHED_PUMPKIN_LOCALEDIR");
  if (locale_dir == NULL || !g_path_is_absolute(locale_dir)) {
    locale_dir = LOCALEDIR;
  }
  bindtextdomain(GETTEXT_PACKAGE, locale_dir);
  bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
  textdomain(GETTEXT_PACKAGE);
}
