#ifndef EVENTS_H
#define EVENTS_H

#include <stddef.h>

#include "playlog.h"

typedef struct {
    const char *api_key;
    int utc_offset_minutes; /* console local time minus UTC */
} events_config;

/* A buffer of this size always holds the body for session_count sessions. */
#define EVENTS_JSON_CAP(session_count) ((size_t)(session_count) * 2560 + 256)

/* Writes the PostHog /batch request body for the sessions: a "game started"
   and a "game ended" event per session. Returns the body length, or 0 when
   out_cap is below EVENTS_JSON_CAP(count). */
size_t events_batch_json(const events_config *config,
                         const playlog_session *sessions, size_t count,
                         char *out, size_t out_cap);

#endif
