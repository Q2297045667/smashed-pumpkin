#pragma once

#include <gio/gio.h>

typedef struct {
  gint64 id;
  char *public_id;
  char *name;
  char *author;
  char *version;
  char *file;
  char *updated_at;
} PumpkinPluginMetadata;

void pumpkin_plugin_metadata_free(PumpkinPluginMetadata *metadata);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PumpkinPluginMetadata, pumpkin_plugin_metadata_free)

/* Filename-based reads prefer ppm's live metadata over legacy sidecars. */
PumpkinPluginMetadata *pumpkin_plugin_store_lookup(const char *directory, const char *file);
PumpkinPluginMetadata *pumpkin_plugin_store_find(const char *directory, gint64 id,
                                               const char *public_id, GError **error);
gboolean pumpkin_plugin_store_validate(const char *directory, GError **error);
gboolean pumpkin_plugin_store_record(const char *directory,
                                    const PumpkinPluginMetadata *metadata, GError **error);
gboolean pumpkin_plugin_store_rename(const char *directory, const char *from,
                                    const char *to, GError **error);
gboolean pumpkin_plugin_store_remove(const char *directory, const char *file, GError **error);
