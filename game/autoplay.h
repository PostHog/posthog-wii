#ifndef AUTOPLAY_H
#define AUTOPLAY_H

#include <stdbool.h>

#include "runner.h"

/* Says whether to hold jump on this tick. It holds jump from just before a
   row of fires until the row has passed, which clears every fire. */
bool autoplay_jump(const runner *game);

#endif
