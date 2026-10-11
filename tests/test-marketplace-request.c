#include "marketplace-request.h"

#include <libsoup/soup.h>
#include <string.h>

typedef struct {
  SoupServer *server;
  char *base_url;
  GPtrArray *paused;
} Fixture;

typedef struct {
  GMainLoop *loop;
  GBytes *bytes;
  GError *error;
  guint callbacks;
} Result;

static void
serve_request(SoupServer *server, SoupServerMessage *message, const char *path,
               GHashTable *query, gpointer user_data)
{
  (void)server;
  (void)query;
  Fixture *fixture = user_data;
  if (g_str_equal(path, "/stall")) {
    soup_server_message_pause(message);
    g_ptr_array_add(fixture->paused, g_object_ref(message));
    return;
  }
  guint status = g_str_equal(path, "/error") ? SOUP_STATUS_SERVICE_UNAVAILABLE : SOUP_STATUS_OK;
  soup_server_message_set_status(message, status, NULL);
  const char *body = g_str_equal(path, "/empty") ? "" : "[]";
  soup_server_message_set_response(message, "application/json", SOUP_MEMORY_COPY, body, strlen(body));
}

static void
fixture_setup(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  fixture->server = soup_server_new(NULL, NULL);
  fixture->paused = g_ptr_array_new_with_free_func(g_object_unref);
  soup_server_add_handler(fixture->server, NULL, serve_request, fixture, NULL);
  g_autoptr(GError) error = NULL;
  g_assert_true(soup_server_listen_local(fixture->server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error));
  g_assert_no_error(error);
  GSList *uris = soup_server_get_uris(fixture->server);
  fixture->base_url = g_uri_to_string(uris->data);
  g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);
}

static void
fixture_teardown(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  for (guint i = 0; i < fixture->paused->len; i++) {
    SoupServerMessage *message = g_ptr_array_index(fixture->paused, i);
    soup_server_message_set_status(message, SOUP_STATUS_SERVICE_UNAVAILABLE, NULL);
    soup_server_message_unpause(message);
  }
  soup_server_disconnect(fixture->server);
  g_ptr_array_unref(fixture->paused);
  g_object_unref(fixture->server);
  g_free(fixture->base_url);
  while (g_main_context_iteration(NULL, FALSE)) {
  }
}

static void
request_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
  (void)source;
  Result *state = user_data;
  ++state->callbacks;
  state->bytes = pumpkin_marketplace_request_finish(result, &state->error);
  g_main_loop_quit(state->loop);
}

static gboolean
test_guard(gpointer user_data)
{
  (void)user_data;
  g_error("Marketplace request failed to complete within the test deadline");
  return G_SOURCE_REMOVE;
}

static void
run_request(const char *url, guint timeout_ms, GCancellable *cancel, Result *result)
{
  result->loop = g_main_loop_new(NULL, FALSE);
  guint guard = g_timeout_add_seconds(5, test_guard, NULL);
  pumpkin_marketplace_request_async(url, timeout_ms, cancel, request_finished, result);
  g_main_loop_run(result->loop);
  g_source_remove(guard);
  g_main_loop_unref(result->loop);
  g_assert_cmpuint(result->callbacks, ==, 1);
}

static void
test_success(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  g_autofree char *url = g_strconcat(fixture->base_url, "ok", NULL);
  Result result = {0};
  run_request(url, 1000, NULL, &result);
  g_assert_no_error(result.error);
  g_assert_cmpmem(g_bytes_get_data(result.bytes, NULL), g_bytes_get_size(result.bytes), "[]", 2);
  g_bytes_unref(result.bytes);
}

static void
test_http_error(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  g_autofree char *url = g_strconcat(fixture->base_url, "error", NULL);
  Result result = {0};
  run_request(url, 1000, NULL, &result);
  g_assert_null(result.bytes);
  g_assert_error(result.error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_nonnull(strstr(result.error->message, "503"));
  g_clear_error(&result.error);
}

static void
test_empty_response(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  g_autofree char *url = g_strconcat(fixture->base_url, "empty", NULL);
  Result result = {0};
  run_request(url, 1000, NULL, &result);
  g_assert_null(result.bytes);
  g_assert_error(result.error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error(&result.error);
}

static void
test_deadline_and_retry(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  g_autofree char *url = g_strconcat(fixture->base_url, "stall", NULL);
  Result result = {0};
  gint64 started = g_get_monotonic_time();
  run_request(url, 80, NULL, &result);
  g_assert_null(result.bytes);
  g_assert_error(result.error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
  g_assert_cmpint(g_get_monotonic_time() - started, <, 2 * G_USEC_PER_SEC);
  g_clear_error(&result.error);
  test_success(fixture, NULL);
  g_assert_cmpuint(result.callbacks, ==, 1);
}

static gboolean
cancel_pending(gpointer user_data)
{
  g_cancellable_cancel(G_CANCELLABLE(user_data));
  return G_SOURCE_REMOVE;
}

static void
test_cancel(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  g_autofree char *url = g_strconcat(fixture->base_url, "stall", NULL);
  g_autoptr(GCancellable) cancel = g_cancellable_new();
  g_timeout_add(30, cancel_pending, cancel);
  Result result = {0};
  run_request(url, 1000, cancel, &result);
  g_assert_null(result.bytes);
  g_assert_error(result.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error(&result.error);
}

static void
test_invalid_url(Fixture *fixture, gconstpointer user_data)
{
  (void)fixture;
  (void)user_data;
  Result result = {0};
  run_request("not a URL", 1000, NULL, &result);
  g_assert_null(result.bytes);
  g_assert_error(result.error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_clear_error(&result.error);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add("/marketplace-request/success", Fixture, NULL, fixture_setup, test_success, fixture_teardown);
  g_test_add("/marketplace-request/http-error", Fixture, NULL, fixture_setup, test_http_error, fixture_teardown);
  g_test_add("/marketplace-request/empty", Fixture, NULL, fixture_setup, test_empty_response, fixture_teardown);
  g_test_add("/marketplace-request/deadline-and-retry", Fixture, NULL, fixture_setup, test_deadline_and_retry, fixture_teardown);
  g_test_add("/marketplace-request/cancel", Fixture, NULL, fixture_setup, test_cancel, fixture_teardown);
  g_test_add("/marketplace-request/invalid-url", Fixture, NULL, fixture_setup, test_invalid_url, fixture_teardown);
  return g_test_run();
}
