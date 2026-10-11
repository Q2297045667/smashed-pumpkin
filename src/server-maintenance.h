#pragma once

#include "server.h"

G_BEGIN_DECLS

void pumpkin_server_stop_async(PumpkinServer *server, GCancellable *cancellable,
                               GAsyncReadyCallback callback, gpointer user_data);
gboolean pumpkin_server_stop_finish(PumpkinServer *server, GAsyncResult *result,
                                    GError **error);

/* The caller must prevent server starts until this operation has finished. */
void pumpkin_binary_install_async(const char *source_path, const char *destination_path,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback, gpointer user_data);
gboolean pumpkin_binary_install_finish(GAsyncResult *result, GError **error);

G_END_DECLS
