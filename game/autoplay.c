#include "autoplay.h"

/* Distances from the left edge of Max's cell to the left edge of a fire's
   cell. The press comes LEAD_TICKS of travel before the fire is close, which
   puts the top of the jump over the row. */
static const float PASSED = -60.0f;
static const float CLOSE = 40.0f;
static const float LEAD_TICKS = 10.0f;

bool autoplay_jump(const runner *r) {
    for (size_t i = 0; i < r->item_count; i++) {
        float ahead = r->items[i].x - (float)RUNNER_MAX_X;
        if (r->items[i].kind == RUNNER_FIRE && ahead > PASSED && ahead < CLOSE + r->speed * LEAD_TICKS) {
            return true;
        }
    }
    return false;
}
