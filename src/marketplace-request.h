#pragma once

#include <gio/gio.h>

/* timeout_ms bounds the complete request, not just an individual socket operation. */
void pumpkin_marketplace_request_async(const char *url,
                                       guint timeout_ms,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data);
GBytes *pumpkin_marketplace_request_finish(GAsyncResult *result, GError **error);
