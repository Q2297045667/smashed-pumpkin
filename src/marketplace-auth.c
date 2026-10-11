#include "marketplace-auth.h"
#include "vendor/cjson/cJSON.h"

#include <string.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(cJSON, cJSON_Delete)

gboolean
pumpkin_marketplace_auth_token_is_safe(const char *token)
{
  if (token == NULL || *token == '\0' || strlen(token) > 4096) return FALSE;
  for (const char *p = token; *p != '\0'; p++) {
    if (!g_ascii_isalnum(*p) && strchr("._-+/=", *p) == NULL) return FALSE;
  }
  return TRUE;
}

const char *
pumpkin_marketplace_auth_error(guint status, const char *body)
{
  if (status == 401) {
    return "Marketplace API key is invalid, expired, or not supported by the account endpoint.";
  }
  if (status == 403) {
    return "The Marketplace could not validate this key with its current permissions.";
  }
  if (status != 200) {
    return "The Marketplace account endpoint is unavailable. The API key was not saved. Free downloads do not require a key.";
  }
  /* /api/user/me returns a session JWT after successful authentication.
   * Never accept a bare HTTP 200 from a public endpoint as authentication. */
  g_autoptr(cJSON) response = body != NULL && strlen(body) <= 64 * 1024
                             ? cJSON_ParseWithOpts(body, NULL, TRUE) : NULL;
  const cJSON *token = cJSON_GetObjectItemCaseSensitive(response, "token");
  const cJSON *user = cJSON_GetObjectItemCaseSensitive(response, "user");
  guint token_fields = 0;
  guint user_fields = 0;
  const cJSON *field;
  cJSON_ArrayForEach(field, response) {
    if (g_strcmp0(field->string, "token") == 0) token_fields++;
    if (g_strcmp0(field->string, "user") == 0) user_fields++;
  }
  g_autoptr(GRegex) jwt = g_regex_new("\\A[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+\\.[A-Za-z0-9_-]+\\z", 0, 0, NULL);
  if (!cJSON_IsObject(response) || token_fields != 1 || user_fields != 1 ||
      !cJSON_IsString(token) || !cJSON_IsObject(user) ||
      !g_regex_match(jwt, token->valuestring, 0, NULL)) {
    return "The Marketplace did not confirm this API key. It was not saved. Free downloads do not require a key.";
  }
  return NULL;
}
