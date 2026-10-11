#pragma once

#include <gio/gio.h>

typedef struct {
  char *name;
  char *path;
  gboolean configured;
  gboolean recognized;
  gboolean managed;
  gboolean size_known;
  guint64 size;
  gint64 modified;
} PumpkinWorldInfo;

void pumpkin_world_info_free(gpointer data);
GPtrArray *pumpkin_world_inventory_load(const char *data_dir, GCancellable *cancellable, GError **error);
void pumpkin_world_inventory_async(const char *data_dir, GCancellable *cancellable,
                                  GAsyncReadyCallback callback, gpointer data);
GPtrArray *pumpkin_world_inventory_finish(GAsyncResult *result, GError **error);
void pumpkin_world_remove_async(const char *data_dir, const char *path,
                               GAsyncReadyCallback callback, gpointer data);
gboolean pumpkin_world_remove_finish(GAsyncResult *result, GError **error);
void pumpkin_world_import_async(const char *data_dir, const char *source,
                               GAsyncReadyCallback callback, gpointer data);
char *pumpkin_world_import_finish(GAsyncResult *result, GError **error);
