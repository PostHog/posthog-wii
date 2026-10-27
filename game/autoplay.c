#include "autoplay.h"
#include "runner.h"
#include <stddef.h>

/* Distances from the left edge of Max's cell to the left edge of a fire's
   cell. The press comes LEAD_TICKS of travel before the fire is close, which
   puts the top of the jump over the row. */
static const float PASSED = -60.0F;
static const float CLOSE = 40.0F;
static const float LEAD_TICKS = 10.0F;

bool autoplay_jump(const runner *game) {
    for (size_t i = 0; i < game->item_count; i++) {
        float ahead = game->items[i].x - (float)RUNNER_MAX_X;
        if (game->items[i].kind == RUNNER_FIRE && ahead > PASSED && ahead < CLOSE + (game->speed * LEAD_TICKS)) {
            return true;
        }
    }
    return false;
}
