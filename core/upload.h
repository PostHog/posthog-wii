#ifndef UPLOAD_H
#define UPLOAD_H

#include <stdbool.h>
#include <stddef.h>

#include "events.h"
#include "playlog.h"

/* Each request costs a connection, which is slow on the Wii, so a request
   carries many sessions. */
enum { UPLOAD_SESSIONS_PER_REQUEST = 100, UPLOAD_HTTP_OK = 200 };

/* Sends one HTTP POST of body to host plus path. Returns the HTTP status, or
   0 when no response arrived. */
typedef long upload_post_fn(const char *host, const char *path, const char *content_type, const void *body,
                            size_t body_len);

/* Sends sessions to PostHog a request at a time. The first request that the
   host does not accept stops the upload, and later sessions are not sent. */
typedef struct {
    /* Results. */
    size_t sent; /* sessions the host accepted */
    long status; /* UPLOAD_HTTP_OK, or the status of the request that stopped the upload */

    /* Internal state. */
    events_config config;
    const char *host;
    upload_post_fn *post;
    size_t pending_count;
    playlog_session pending[UPLOAD_SESSIONS_PER_REQUEST];
    char json[EVENTS_JSON_CAP(UPLOAD_SESSIONS_PER_REQUEST)];
} upload;

void upload_begin(upload *u, const events_config *config, const char *host, upload_post_fn *post);

void upload_add(upload *u, const playlog_session *session);

/* Sends what is left. Returns true when the host accepted every session. */
bool upload_end(upload *u);

#endif
