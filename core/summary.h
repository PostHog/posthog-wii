#ifndef SUMMARY_H
#define SUMMARY_H

#include "playlog.h"

enum { SESSION_SUMMARY_CAP = 256 };

/* Writes the start in console local time, the duration as H:MM:SS, the game
   code and the title, on one line. */
void session_summary(const playlog_session *session, char out[SESSION_SUMMARY_CAP]);

#endif
