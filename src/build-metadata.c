#include "build-metadata.h"

#include <stdio.h>
#include <string.h>

/* Only parse the old, locale-independent YMD metadata grammar. DMY/MDY labels
 * saved by older versions are opaque: inferring their timezone or date order
 * from today's UI preferences would corrupt the build identity. The returned
 * date holds the original components; unqualified legacy dates are wall time,
 * not UTC instants. */
static GDateTime *
parse_build_time(const char *metadata, gboolean *is_utc, gboolean *has_time)
{
  if (metadata == NULL || *metadata == '\0') return NULL;
  const char *text = g_str_has_prefix(metadata, "Build ") ? metadata + 6 : metadata;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, end = 0;
  *is_utc = FALSE;
  *has_time = TRUE;

  if (sscanf(text, "%4d-%2d-%2d %2d:%2d UTC%n", &year, &month, &day, &hour, &minute, &end) == 5 &&
      end > 0 && text[end] == '\0') {
    *is_utc = TRUE;
  } else {
    end = 0;
    if (!(sscanf(text, "%4d-%2d-%2d %2d:%2d%n", &year, &month, &day, &hour, &minute, &end) == 5 &&
          end > 0 && text[end] == '\0')) {
      end = 0;
      hour = minute = 0;
      if (!(sscanf(text, "%4d-%2d-%2d%n", &year, &month, &day, &end) == 3 &&
            end > 0 && text[end] == '\0')) return NULL;
      *has_time = FALSE;
    }
  }
  if (!g_date_valid_dmy(day, month, year) || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    return NULL;
  }
  return g_date_time_new_utc(year, month, day, hour, minute, 0.0);
}

char *
pumpkin_build_metadata_from_unix(gint64 unix_time)
{
  g_autoptr(GDateTime) utc = g_date_time_new_from_unix_utc(unix_time);
  if (utc == NULL) return NULL;
  g_autofree char *timestamp = g_date_time_format(utc, "%Y-%m-%d %H:%M UTC");
  /* Stable metadata grammar, deliberately not a translation entry. */
  return g_strdup_printf("Build %s", timestamp);
}

gboolean
pumpkin_build_metadata_equal(const char *left, const char *right)
{
  if (g_strcmp0(left, right) == 0) return TRUE;
  gboolean left_utc = FALSE, right_utc = FALSE, left_time = FALSE, right_time = FALSE;
  g_autoptr(GDateTime) left_date = parse_build_time(left, &left_utc, &left_time);
  g_autoptr(GDateTime) right_date = parse_build_time(right, &right_utc, &right_time);
  return left_date != NULL && right_date != NULL &&
    left_utc == right_utc && left_time == right_time &&
    g_date_time_equal(left_date, right_date);
}

char *
pumpkin_build_metadata_format(const char *metadata, const char *date_time_format)
{
  if (metadata == NULL || *metadata == '\0') return NULL;
  gboolean is_utc = FALSE, has_time = FALSE;
  g_autoptr(GDateTime) date = parse_build_time(metadata, &is_utc, &has_time);
  if (date != NULL) {
    g_autoptr(GDateTime) display_date = is_utc ? g_date_time_to_local(date) : g_date_time_ref(date);
    g_autofree char *formatted = g_date_time_format(display_date,
      date_time_format != NULL ? date_time_format : "%Y-%m-%d %H:%M");
    /* Only these presentation strings should become gettext entries later. */
    return g_strdup_printf("Build %s", formatted);
  }
  if (g_str_has_prefix(metadata, "Stable ")) {
    return g_strdup_printf("Stable %s", metadata + 7);
  }
  if (g_str_has_prefix(metadata, "Nightly ")) {
    return g_strdup_printf("Nightly %s", metadata + 8);
  }
  return g_strdup(metadata);
}
