#include "summary.h"

#include <stdio.h>

#include "playlog.h"
#include "ticks.h"

enum { SECONDS_PER_MINUTE = 60, MINUTES_PER_HOUR = 60, SECONDS_PER_HOUR = SECONDS_PER_MINUTE * MINUTES_PER_HOUR };

void session_summary(const playlog_session *session, char out[SESSION_SUMMARY_CAP]) {
    char local[ISO_TIME_CAP];
    unix_ms_to_iso(ticks_to_unix_ms(session->start_ticks), local);
    unsigned long long seconds = ticks_to_seconds(session->end_ticks - session->start_ticks);
    (void)snprintf(out, SESSION_SUMMARY_CAP, "%.10s %.8s  %llu:%02llu:%02llu  %s  %s", local, local + ISO_TIME_OF_DAY,
                   seconds / SECONDS_PER_HOUR, seconds / SECONDS_PER_MINUTE % MINUTES_PER_HOUR,
                   seconds % SECONDS_PER_MINUTE, session->game_code, session->title);
}
