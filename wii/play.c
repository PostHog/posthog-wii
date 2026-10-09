#include "play.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "atlas.h"
#include "gfx.h"
#include "runner.h"
#include "settings.h"

#ifdef DEBUG_HOST
#include <stdio.h>

#include "http.h"
#endif

/* Colors from PostHog's product UI. */
enum {
    COLOR_PAGE = 0xF3F4EF,
    COLOR_BARS = 0xE8E9E2,
    COLOR_GROUND = 0xDADBD2,
    COLOR_MUTED = 0xABAD9F,
    COLOR_INK = 0x111111,
};

enum {
    GROUND_Y = 432,
    OUTLINE = 3,

    BAR_PITCH = 40,
    BAR_WIDTH = 28,
    BAR_MIN_HEIGHT = 40,
    BAR_HEIGHT_RANGE = 150,
    BAR_SCROLL_DIVISOR = 4,

    DASH_PITCH = 64,
    DASH_WIDTH = 18,
    DASH_Y = GROUND_Y + 16,

    DIGIT_COLUMNS = 3,
    DIGIT_ROWS = 5,
    DIGIT_PIXEL = 4,
    DIGIT_PITCH = (DIGIT_COLUMNS + 1) * DIGIT_PIXEL,
    SCORE_MARGIN = 32,

    FIRE_TICKS_PER_FRAME = 3,
};

/* Each digit is five rows of three pixels, top row first, left pixel in the
   highest bit. */
static const uint8_t DIGITS[10][DIGIT_ROWS] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

static const struct {
    unsigned first, count;
} MAX_FRAMES[RUNNER_ANIMATION_COUNT] = {
    [RUNNER_IDLE] = {ATLAS_IDLE_FIRST, ATLAS_IDLE_COUNT},
    [RUNNER_RUN] = {ATLAS_WALK_FIRST, ATLAS_WALK_COUNT},
    [RUNNER_AIR] = {ATLAS_FALL_FIRST, ATLAS_FALL_COUNT},
    [RUNNER_FALLEN] = {ATLAS_DEATH_FIRST, ATLAS_DEATH_COUNT},
};

static bool jump_held(void) {
    u32 held = WPAD_ButtonsHeld(WPAD_CHAN_0);
    return (held & (WPAD_BUTTON_A | WPAD_BUTTON_B | WPAD_BUTTON_UP | WPAD_BUTTON_2)) != 0;
}

/* Draws value with its last digit ending at right. */
static void draw_number(float right, float y, uint32_t value, uint32_t rgb) {
    float x = right - DIGIT_COLUMNS * DIGIT_PIXEL;
    do {
        const uint8_t *rows = DIGITS[value % 10];
        for (int row = 0; row < DIGIT_ROWS; row++) {
            for (int column = 0; column < DIGIT_COLUMNS; column++) {
                if (rows[row] & (1 << (DIGIT_COLUMNS - 1 - column))) {
                    gfx_rect(x + (float)(column * DIGIT_PIXEL), y + (float)(row * DIGIT_PIXEL), DIGIT_PIXEL, DIGIT_PIXEL,
                             rgb);
                }
            }
        }
        x -= DIGIT_PITCH;
        value /= 10;
    } while (value > 0);
}

/* A bar chart on the horizon that scrolls slower than the ground. Each bar
   keeps its height because the height comes from the bar's number. */
static void draw_bars(const runner *r, float width) {
    unsigned scrolled = (unsigned)r->distance / BAR_SCROLL_DIVISOR;
    unsigned first = scrolled / BAR_PITCH;
    for (unsigned i = 0; (float)(i * BAR_PITCH) < width + BAR_PITCH; i++) {
        unsigned height = BAR_MIN_HEIGHT + (first + i) * 2654435761u % BAR_HEIGHT_RANGE;
        gfx_rect((float)(i * BAR_PITCH) - (float)(scrolled % BAR_PITCH), (float)(GROUND_Y - height), BAR_WIDTH,
                 (float)height, COLOR_BARS);
    }
}

static void draw(const runner *r, float width, unsigned tick) {
    gfx_rect(0, 0, width, GFX_HEIGHT, COLOR_PAGE);
    draw_bars(r, width);
    gfx_rect(0, GROUND_Y, width, GFX_HEIGHT - GROUND_Y, COLOR_GROUND);
    for (unsigned i = 0; (float)(i * DASH_PITCH) < width + DASH_PITCH; i++) {
        gfx_rect((float)(i * DASH_PITCH) - (float)((unsigned)r->distance % DASH_PITCH), DASH_Y, DASH_WIDTH, OUTLINE,
                 COLOR_MUTED);
    }

    for (size_t i = 0; i < r->item_count; i++) {
        const runner_item *item = &r->items[i];
        unsigned frame = item->kind == RUNNER_FIRE
                             ? ATLAS_FIRE_FIRST + (tick / FIRE_TICKS_PER_FRAME + item->variant) % ATLAS_FIRE_COUNT
                             : ATLAS_FLAG_FIRST + item->variant % ATLAS_FLAG_COUNT;
        gfx_frame(item->x, item->y, frame, false);
    }

    unsigned frame = MAX_FRAMES[r->animation].first + runner_frame(r, MAX_FRAMES[r->animation].count);
    gfx_frame(RUNNER_MAX_X, r->max_y, frame, false);

    /* Max's sprite has a light gray contact shadow under the feet, made for
       a white page. The ground line is drawn after him so the shadow never
       lightens it. */
    gfx_rect(0, GROUND_Y, width, OUTLINE, COLOR_INK);

    draw_number(width - SCORE_MARGIN, SCORE_MARGIN, r->score, COLOR_INK);
    if (r->best > 0) {
        draw_number(width - SCORE_MARGIN, SCORE_MARGIN + (DIGIT_ROWS + 2) * DIGIT_PIXEL, r->best, COLOR_MUTED);
    }
}

#ifdef DEBUG_HOST
/* Debug builds play by themselves and post what was shown to DEBUG_HOST,
   because nothing else can show a Dolphin frame to an automated check. The
   player jumps every fire until tick 900 and then runs into one. */
static bool scripted_jump(const runner *r, unsigned tick) {
    if (r->phase != RUNNER_RUNNING) {
        return tick % 20 == 10;
    }
    for (size_t i = 0; tick < 900 && i < r->item_count; i++) {
        float ahead = r->items[i].x - RUNNER_MAX_X;
        if (r->items[i].kind == RUNNER_FIRE && ahead > -60 && ahead < 40 + r->speed * 10) {
            return true;
        }
    }
    return false;
}

static void post_displayed(unsigned tick) {
    unsigned width, height;
    const uint8_t *pixels = gfx_displayed(&width, &height);
    char path[64];
    snprintf(path, sizeof path, "/shot?tick=%u&width=%u&height=%u", tick, width, height);
    http_post(DEBUG_HOST, path, "application/octet-stream", pixels, (size_t)width * height * 2);
}
#endif

void play(void) {
    gfx_init();
    float width = gfx_width();

    runner game;
    runner_init(&game, width, GROUND_Y, (uint32_t)gettime());

    for (unsigned tick = 0;; tick++) {
        WPAD_ScanPads();
        if (WPAD_ButtonsDown(WPAD_CHAN_0) & WPAD_BUTTON_HOME) {
            return;
        }
        bool jump = jump_held();
#ifdef DEBUG_HOST
        jump = scripted_jump(&game, tick);
#endif
        runner_step(&game, jump);
        draw(&game, width, tick);
        gfx_present();
#ifdef DEBUG_HOST
        if (tick == 5 || tick == 200 || tick == 420 || tick == 640 || tick == 860 || tick == 1080 || tick == 1200) {
            post_displayed(tick);
        }
#endif
    }
}
