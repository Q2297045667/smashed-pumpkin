#pragma once

#include <gio/gio.h>
#include "release-channel.h"

G_BEGIN_DECLS

typedef struct _PumpkinDownload PumpkinDownload;

typedef void (*PumpkinDownloadProgress)(goffset current,
                                        goffset total,
                                        gpointer user_data);

typedef enum {
  PUMPKIN_DOWNLOAD_OK,
  PUMPKIN_DOWNLOAD_FALLBACK_USED
} PumpkinDownloadResult;

typedef struct {
  char *url;
  char *build_id;
  char *build_label;
  PumpkinDownloadResult result_code;
} PumpkinResolvedDownload;

void pumpkin_download_file_async(const char *url,
                                 const char *dest_path,
                                 GCancellable *cancellable,
                                 PumpkinDownloadProgress progress_cb,
                                 gpointer progress_data,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data);

/* The authorization value is sent as the HTTP Authorization header and is never logged. */
void pumpkin_download_file_authorized_async(const char *url,
                                            const char *dest_path,
                                            const char *authorization,
                                            GCancellable *cancellable,
                                            PumpkinDownloadProgress progress_cb,
                                            gpointer progress_data,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data);

gboolean pumpkin_download_file_finish(GAsyncResult *result, GError **error);

void pumpkin_resolve_latest_async(GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data);

void pumpkin_resolve_channel_async(PumpkinReleaseChannel channel, GCancellable *cancellable,
                                   GAsyncReadyCallback callback, gpointer user_data);

PumpkinResolvedDownload *pumpkin_resolve_latest_finish(GAsyncResult *result,
                                                       PumpkinDownloadResult *result_code,
                                                       GError **error);

void pumpkin_resolved_download_free(PumpkinResolvedDownload *resolved);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PumpkinResolvedDownload, pumpkin_resolved_download_free)

G_END_DECLS
