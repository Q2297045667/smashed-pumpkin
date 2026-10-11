#include "download.h"

#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <string.h>

static const char payload[] = "\0asm\1\0\0\0";

typedef struct {
  SoupServer *server;
  char *base_url;
  char *directory;
  char *destination;
} Fixture;

typedef struct {
  GMainLoop *loop;
  gboolean ok;
  GError *error;
  guint callbacks;
} Result;

static void
serve_request(SoupServer *server, SoupServerMessage *message, const char *path,
              GHashTable *query, gpointer user_data)
{
  (void)server; (void)query; (void)user_data;
  guint status = g_str_equal(path, "/401") ? SOUP_STATUS_UNAUTHORIZED :
    g_str_equal(path, "/403") ? SOUP_STATUS_FORBIDDEN :
    g_str_equal(path, "/503") ? SOUP_STATUS_SERVICE_UNAVAILABLE : SOUP_STATUS_OK;
  soup_server_message_set_status(message, status, NULL);
  soup_server_message_set_response(message, "application/octet-stream", SOUP_MEMORY_COPY,
    payload, sizeof(payload) - 1);
}

static void
fixture_setup(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  fixture->server = soup_server_new(NULL, NULL);
  soup_server_add_handler(fixture->server, NULL, serve_request, NULL, NULL);
  g_autoptr(GError) error = NULL;
  g_assert_true(soup_server_listen_local(fixture->server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error));
  g_assert_no_error(error);
  GSList *uris = soup_server_get_uris(fixture->server);
  fixture->base_url = g_uri_to_string(uris->data);
  g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);
  fixture->directory = g_dir_make_tmp("smashed-download-http-XXXXXX", &error);
  g_assert_no_error(error);
  fixture->destination = g_build_filename(fixture->directory, "download.wasm", NULL);
}

static void
fixture_teardown(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  soup_server_disconnect(fixture->server);
  g_object_unref(fixture->server);
  g_free(fixture->base_url);
  g_remove(fixture->destination);
  g_rmdir(fixture->directory);
  g_free(fixture->destination);
  g_free(fixture->directory);
  while (g_main_context_iteration(NULL, FALSE)) {}
}

static void
download_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
  (void)source;
  Result *state = user_data;
  ++state->callbacks;
  state->ok = pumpkin_download_file_finish(result, &state->error);
  g_main_loop_quit(state->loop);
}

static gboolean
test_guard(gpointer user_data)
{
  (void)user_data;
  g_error("Download failed to complete within the test deadline");
  return G_SOURCE_REMOVE;
}

static void
run_download(Fixture *fixture, const char *path, GCancellable *cancel, Result *result)
{
  g_autofree char *url = g_strconcat(fixture->base_url, path, NULL);
  result->loop = g_main_loop_new(NULL, FALSE);
  guint guard = g_timeout_add_seconds(5, test_guard, NULL);
  pumpkin_download_file_async(url, fixture->destination, cancel, NULL, NULL, download_finished, result);
  g_main_loop_run(result->loop);
  g_source_remove(guard);
  g_main_loop_unref(result->loop);
  g_assert_cmpuint(result->callbacks, ==, 1);
}

static void
test_http_status(Fixture *fixture, gconstpointer user_data)
{
  guint status = GPOINTER_TO_UINT(user_data);
  g_autofree char *path = g_strdup_printf("%u", status);
  Result result = {0};
  g_assert_true(g_file_set_contents(fixture->destination, "existing plugin", -1, NULL));
  run_download(fixture, path, NULL, &result);
  g_assert_false(result.ok);
  g_assert_error(result.error, PUMPKIN_DOWNLOAD_HTTP_ERROR, (gint)status);
  /* The UI can still classify the response when its message is translated. */
  g_free(result.error->message);
  result.error->message = g_strdup("Download fehlgeschlagen: Der Server hat die Anfrage zurückgewiesen");
  g_assert_true(g_error_matches(result.error, PUMPKIN_DOWNLOAD_HTTP_ERROR, (gint)status));
  g_assert_false(g_error_matches(result.error, PUMPKIN_DOWNLOAD_HTTP_ERROR,
    status == SOUP_STATUS_UNAUTHORIZED ? SOUP_STATUS_FORBIDDEN : SOUP_STATUS_UNAUTHORIZED));
  g_autofree char *contents = NULL;
  g_assert_true(g_file_get_contents(fixture->destination, &contents, NULL, NULL));
  g_assert_cmpstr(contents, ==, "existing plugin");
  g_clear_error(&result.error);
}

static void
test_success(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  Result result = {0};
  run_download(fixture, "ok", NULL, &result);
  g_assert_true(result.ok);
  g_assert_no_error(result.error);
  g_autofree char *contents = NULL;
  gsize length = 0;
  g_assert_true(g_file_get_contents(fixture->destination, &contents, &length, NULL));
  g_assert_cmpmem(contents, length, payload, sizeof(payload) - 1);
}

static void
test_cancel(Fixture *fixture, gconstpointer user_data)
{
  (void)user_data;
  g_autoptr(GCancellable) cancel = g_cancellable_new();
  g_cancellable_cancel(cancel);
  Result result = {0};
  run_download(fixture, "ok", cancel, &result);
  g_assert_false(result.ok);
  g_assert_error(result.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error(&result.error);
}

static void
test_message_is_not_status(void)
{
  g_autoptr(GError) error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED,
    "An unrelated message containing HTTP status 401 and HTTP status 403");
  g_assert_false(g_error_matches(error, PUMPKIN_DOWNLOAD_HTTP_ERROR, SOUP_STATUS_UNAUTHORIZED));
  g_assert_false(g_error_matches(error, PUMPKIN_DOWNLOAD_HTTP_ERROR, SOUP_STATUS_FORBIDDEN));
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add("/download-http/401", Fixture, GUINT_TO_POINTER(401), fixture_setup, test_http_status, fixture_teardown);
  g_test_add("/download-http/403", Fixture, GUINT_TO_POINTER(403), fixture_setup, test_http_status, fixture_teardown);
  g_test_add("/download-http/503", Fixture, GUINT_TO_POINTER(503), fixture_setup, test_http_status, fixture_teardown);
  g_test_add("/download-http/success", Fixture, NULL, fixture_setup, test_success, fixture_teardown);
  g_test_add("/download-http/cancel", Fixture, NULL, fixture_setup, test_cancel, fixture_teardown);
  g_test_add_func("/download-http/message-is-not-status", test_message_is_not_status);
  return g_test_run();
}
