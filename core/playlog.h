#ifndef PLAYLOG_H
#define PLAYLOG_H

#include <stddef.h>
#include <stdint.h>

enum { PLAYLOG_TITLE_CAP = 128 };

typedef struct {
    uint64_t console_id;
    uint64_t start_ticks;
    uint64_t end_ticks;
    char game_code[5];
    char maker_code[3];
    char title[PLAYLOG_TITLE_CAP]; /* UTF-8, as the Wii Menu shows it */
} playlog_session;

typedef void playlog_session_fn(void *ctx, const playlog_session *session);

/* Calls fn once per play session recorded in a Message Board image
   (cdb.vff), in directory order. Every session has end_ticks >= start_ticks
   and a year from 2000 through 2099. See ticks.h for the tick unit. */
void playlog_each_session(const uint8_t *image, size_t image_len,
                          playlog_session_fn *fn, void *ctx);

#endif
