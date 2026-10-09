#include "summary.h"

#include <stdio.h>

#include "ticks.h"

void session_summary(const playlog_session *session, char out[SESSION_SUMMARY_CAP]) {
    char local[ISO_TIME_CAP];
    unix_ms_to_iso(ticks_to_unix_ms(session->start_ticks, 0), local);
    unsigned long long seconds = ticks_to_seconds(session->end_ticks - session->start_ticks);
    snprintf(out, SESSION_SUMMARY_CAP, "%.10s %.8s  %llu:%02llu:%02llu  %s  %s", local, local + 11, seconds / 3600,
             seconds / 60 % 60, seconds % 60, session->game_code, session->title);
}
