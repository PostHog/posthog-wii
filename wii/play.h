#ifndef PLAY_H
#define PLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "runner.h"

/* Both functions need VIDEO_Init and WPAD_Init to have run, take over the
   display, and return when HOME is pressed on the Wii Remote. */

/* The game for a person: A, B, Up or 2 on the Wii Remote jumps. */
void play(void);

/* Says whether the jump button is held on this tick. */
typedef bool play_jump_fn(const runner *game, unsigned tick);

/* Called after the frame for a tick is on screen. */
typedef void play_shown_fn(unsigned tick);

/* The game on the track that seed picks, with jump deciding the button.
   shown can be NULL. */
void play_scripted(uint32_t seed, play_jump_fn *jump, play_shown_fn *shown);

#endif
