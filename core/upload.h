#ifndef UPLOAD_H
#define UPLOAD_H

#include <stdbool.h>
#include <stddef.h>

#include "events.h"
#include "http_request.h"
#include "playlog.h"

/* A request carries many sessions because each request opens a connection,
   which is slow on the Wii. */
enum { UPLOAD_SESSIONS_PER_REQUEST = 100 };

/* UPLOAD_NO_ANSWER is what a post function returns when no response
   arrived. UPLOAD_NOT_CAPTURE is a 200 from something other than PostHog
   capture, such as a proxy or a Wi-Fi login page. */
enum { UPLOAD_NO_ANSWER = 0, UPLOAD_HTTP_OK = 200, UPLOAD_NOT_CAPTURE = -1 };

/* The text that PostHog capture's answer contains when it took a request. */
#define UPLOAD_OK_MARK "\"status\":\"Ok\""

/* The first request that the host rejects stops the upload, and the later
   sessions stay unsent. */
typedef struct {
    size_t sent; /* sessions the host accepted */
    /* UPLOAD_HTTP_OK, or the UPLOAD_NO_ANSWER, UPLOAD_NOT_CAPTURE or HTTP
       status that stopped the upload. */
    long status;

    /* Private. */
    events_config config;
    const char *host;
    http_post_fn *post;
    size_t pending_count;
    playlog_session pending[UPLOAD_SESSIONS_PER_REQUEST];
    char json[EVENTS_JSON_CAP(UPLOAD_SESSIONS_PER_REQUEST)];
} upload;

void upload_begin(upload *job, const events_config *config, const char *host, http_post_fn *post);

void upload_add(upload *job, const playlog_session *session);

/* Sends what is left. Returns true when the host accepted every session. */
bool upload_end(upload *job);

#endif
