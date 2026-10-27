#ifndef EVENTS_H
#define EVENTS_H

#include <stddef.h>

#include "playlog.h"

typedef struct {
    const char *api_key;
    int utc_offset_minutes; /* console local time minus UTC */
} events_config;

/* Enough for the body of session_count sessions. A session is two events,
   and each byte of a title takes up to six bytes as a JSON escape. */
#define EVENTS_JSON_CAP(session_count) (((size_t)(session_count) * 2 * ((6 * PLAYLOG_TITLE_CAP) + 512)) + 256)

/* Writes the body of a PostHog /batch request, with a "game started" and a
   "game ended" event per session. Returns the body length, or 0 when out_cap
   is below EVENTS_JSON_CAP(count). A resend repeats every event ID and
   session ID. */
size_t events_batch_json(const events_config *config, const playlog_session *sessions, size_t count, char *out,
                         size_t out_cap);

#endif
