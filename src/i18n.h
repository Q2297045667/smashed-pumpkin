#pragma once

#include "config.h"
#include <glib/gi18n-lib.h>

G_BEGIN_DECLS

/* Call once at process startup, before GTK, templates or worker threads.
 * GTK callers must first call gtk_disable_setlocale() so GTK cannot restore a
 * locale-dependent decimal separator. This is not a live language-switch API.
 * SMASHED_PUMPKIN_LOCALEDIR may point to an absolute development/test directory;
 * normal installations use the configured LOCALEDIR. */
void pumpkin_i18n_init(void);

/* Resolve/persist preferences before GTK, then apply them for this process.
 * Errors leave a usable fallback and never overwrite a damaged configuration. */
void pumpkin_i18n_startup(void);
void pumpkin_i18n_init_language(const char *language);
const char *pumpkin_i18n_get_language(void);

G_END_DECLS
