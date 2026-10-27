#ifndef PLAYLOG_H
#define PLAYLOG_H

#include <stddef.h>
#include <stdint.h>

enum { PLAYLOG_TITLE_CAP = 128, PLAYLOG_GAME_CODE_LEN = 4, PLAYLOG_MAKER_CODE_LEN = 2 };

typedef struct {
    uint64_t console_id;
    uint64_t start_ticks;
    uint64_t end_ticks;
    char game_code[PLAYLOG_GAME_CODE_LEN + 1];
    char maker_code[PLAYLOG_MAKER_CODE_LEN + 1];
    char title[PLAYLOG_TITLE_CAP]; /* UTF-8, as the Wii Menu shows it */
} playlog_session;

typedef void playlog_session_fn(void *ctx, const playlog_session *session);

/* Calls on_session for each play session in a Message Board image (cdb.vff).
   Each session has end_ticks >= start_ticks and a year from 2000 through
   2099. */
void playlog_each_session(const uint8_t *image, size_t image_len, playlog_session_fn *on_session, void *ctx);

#endif
