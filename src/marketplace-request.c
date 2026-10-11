#include "marketplace-request.h"

#include <libsoup/soup.h>

typedef struct {
  GTask *task;
  SoupMessage *message;
  GCancellable *io_cancel;
  GCancellable *caller_cancel;
  gulong cancel_handler;
  GSource *deadline;
  gboolean completed;
} MarketplaceRequest;

static void
cancel_request(GCancellable *caller, gpointer user_data)
{
  (void)caller;
  g_cancellable_cancel(G_CANCELLABLE(user_data));
}

static void
request_free(MarketplaceRequest *request)
{
  g_source_destroy(request->deadline);
  g_source_unref(request->deadline);
  if (request->caller_cancel != NULL) {
    g_cancellable_disconnect(request->caller_cancel, request->cancel_handler);
  }
  g_clear_object(&request->caller_cancel);
  g_object_unref(request->io_cancel);
  g_object_unref(request->message);
  g_object_unref(request->task);
  g_free(request);
}

static gboolean
request_timed_out(gpointer user_data)
{
  MarketplaceRequest *request = user_data;
  request->completed = TRUE;
  g_task_return_new_error(request->task, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                          "The Marketplace did not respond in time. Please try again.");
  g_cancellable_cancel(request->io_cancel);
  return G_SOURCE_REMOVE;
}

static void
request_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
  MarketplaceRequest *request = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GBytes) bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
  if (!request->completed) {
    g_source_destroy(request->deadline);
    request->completed = TRUE;
    guint status = soup_message_get_status(request->message);
    if (error != NULL) {
      g_task_return_error(request->task, g_steal_pointer(&error));
    } else if (status != SOUP_STATUS_OK) {
      g_task_return_new_error(request->task, G_IO_ERROR, G_IO_ERROR_FAILED,
                              "Marketplace request failed (HTTP %u). Please try again.", status);
    } else if (bytes == NULL || g_bytes_get_size(bytes) == 0) {
      g_task_return_new_error(request->task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                              "The Marketplace returned an empty response.");
    } else {
      g_task_return_pointer(request->task, g_steal_pointer(&bytes), (GDestroyNotify)g_bytes_unref);
    }
  }
  request_free(request);
}

void
pumpkin_marketplace_request_async(const char *url,
                                  guint timeout_ms,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
  GTask *task = g_task_new(NULL, cancellable, callback, user_data);
  g_task_set_source_tag(task, pumpkin_marketplace_request_async);
  SoupMessage *message = soup_message_new("GET", url);
  if (message == NULL) {
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                            "Could not create the Marketplace request.");
    g_object_unref(task);
    return;
  }

  MarketplaceRequest *request = g_new0(MarketplaceRequest, 1);
  request->task = task;
  request->message = message;
  request->io_cancel = g_cancellable_new();
  if (cancellable != NULL) {
    request->caller_cancel = g_object_ref(cancellable);
    request->cancel_handler = g_cancellable_connect(cancellable, G_CALLBACK(cancel_request),
                                                    g_object_ref(request->io_cancel), g_object_unref);
  }
  request->deadline = g_timeout_source_new(MAX(timeout_ms, 1u));
  g_source_set_callback(request->deadline, request_timed_out, request, NULL);
  g_source_attach(request->deadline, g_task_get_context(task));

  g_autoptr(SoupSession) session = soup_session_new_with_options(
    "user-agent", "Smashed Pumpkin", "timeout", 15u, NULL);
  soup_message_headers_append(soup_message_get_request_headers(message), "Accept", "application/json");
  soup_session_send_and_read_async(session, message, G_PRIORITY_DEFAULT,
                                   request->io_cancel, request_done, request);
}

GBytes *
pumpkin_marketplace_request_finish(GAsyncResult *result, GError **error)
{
  g_return_val_if_fail(g_task_is_valid(result, NULL), NULL);
  g_return_val_if_fail(g_async_result_is_tagged(result, pumpkin_marketplace_request_async), NULL);
  return g_task_propagate_pointer(G_TASK(result), error);
}
