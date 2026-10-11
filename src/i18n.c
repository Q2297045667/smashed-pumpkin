#include "i18n.h"

#include <locale.h>

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
