#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

void pumpkin_marketplace_credentials_load_async(GAsyncReadyCallback callback,
                                                gpointer user_data);
char *pumpkin_marketplace_credentials_load_finish(GAsyncResult *result,
                                                  GError **error);

void pumpkin_marketplace_credentials_store_async(const char *token,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean pumpkin_marketplace_credentials_store_finish(GAsyncResult *result,
                                                      GError **error);

void pumpkin_marketplace_credentials_clear_async(GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean pumpkin_marketplace_credentials_clear_finish(GAsyncResult *result,
                                                      GError **error);

G_END_DECLS
