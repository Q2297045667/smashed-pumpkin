#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

/* Stable persisted IDs, independent of translated labels and catalog presence. */
gboolean pumpkin_language_is_valid(const char *id);
guint pumpkin_language_index(const char *id);
const char *pumpkin_language_from_index(guint index);
const char *pumpkin_language_detect(const char *const *preferences);

/* Startup only, before GTK or threads. Preserve manual selections and unrelated
 * INI keys. Return an owned effective ID even when reading/saving reports an error.
 * Missing preferences are detected and saved once; system is resolved each time. */
char *pumpkin_language_prepare(GError **error);

G_END_DECLS
