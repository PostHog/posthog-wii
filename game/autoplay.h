#ifndef AUTOPLAY_H
#define AUTOPLAY_H

#include <stdbool.h>

#include "runner.h"

/* A player that clears every fire: it holds jump from just before a row of
   fires until the row has passed. The tests use it to prove that every
   track can be survived, and the debug build uses it to play unattended. */
bool autoplay_jump(const runner *r);

#endif
