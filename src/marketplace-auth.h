#pragma once

#include <glib.h>

/* NULL means the authenticated account endpoint confirmed the token. */
const char *pumpkin_marketplace_auth_error(guint status, const char *body);
gboolean pumpkin_marketplace_auth_token_is_safe(const char *token);
