#include "play.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "atlas.h"
#include "gfx.h"

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
    BEST_Y = SCORE_MARGIN + (DIGIT_ROWS + 2) * DIGIT_PIXEL,

    FIRE_TICKS_PER_FRAME = 3,
};

/* Knuth's multiplicative hash constant. It spreads consecutive bar numbers
   over unrelated heights. */
static const unsigned HASH_MULTIPLIER = 2654435761u;

/* Each digit is five rows of three pixels, top row first, left pixel in the
   highest bit. */
static const uint8_t DIGITS[10][DIGIT_ROWS] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

_Static_assert((int)RUNNER_CELL == (int)ATLAS_CELL, "the game places and hits sprites in atlas cell pixels");

static const struct {
    unsigned first, count;
} MAX_FRAMES[RUNNER_ANIMATION_COUNT] = {
    [RUNNER_IDLE] = {ATLAS_IDLE_FIRST, ATLAS_IDLE_COUNT},
    [RUNNER_RUN] = {ATLAS_WALK_FIRST, ATLAS_WALK_COUNT},
    [RUNNER_AIR] = {ATLAS_FALL_FIRST, ATLAS_FALL_COUNT},
    [RUNNER_FALLEN] = {ATLAS_DEATH_FIRST, ATLAS_DEATH_COUNT},
};

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

/* A bar chart on the horizon that scrolls slower than the ground. A bar
   keeps its height as it moves because the height comes from its number. */
static void draw_bars(const runner *game, float width) {
    unsigned scrolled = (unsigned)game->distance / BAR_SCROLL_DIVISOR;
    unsigned first = scrolled / BAR_PITCH;
    for (unsigned i = 0; (float)(i * BAR_PITCH) < width + BAR_PITCH; i++) {
        unsigned height = BAR_MIN_HEIGHT + (first + i) * HASH_MULTIPLIER % BAR_HEIGHT_RANGE;
        gfx_rect((float)(i * BAR_PITCH) - (float)(scrolled % BAR_PITCH), (float)(GROUND_Y - height), BAR_WIDTH,
                 (float)height, COLOR_BARS);
    }
}

static void draw_ground(const runner *game, float width) {
    gfx_rect(0, GROUND_Y, width, GFX_HEIGHT - GROUND_Y, COLOR_GROUND);
    unsigned scrolled = (unsigned)game->distance % DASH_PITCH;
    for (unsigned i = 0; (float)(i * DASH_PITCH) < width + DASH_PITCH; i++) {
        gfx_rect((float)(i * DASH_PITCH) - (float)scrolled, DASH_Y, DASH_WIDTH, OUTLINE, COLOR_MUTED);
    }
}

static unsigned item_frame(const runner_item *item, unsigned tick) {
    if (item->kind == RUNNER_FIRE) {
        return ATLAS_FIRE_FIRST + (tick / FIRE_TICKS_PER_FRAME + item->variant) % ATLAS_FIRE_COUNT;
    }
    return ATLAS_PICKUP_FIRST + item->variant % ATLAS_PICKUP_COUNT;
}

static void draw(const runner *game, float width, unsigned tick) {
    gfx_rect(0, 0, width, GFX_HEIGHT, COLOR_PAGE);
    draw_bars(game, width);
    draw_ground(game, width);
    for (size_t i = 0; i < game->item_count; i++) {
        gfx_frame(game->items[i].x, game->items[i].y, item_frame(&game->items[i], tick));
    }
    gfx_frame(RUNNER_MAX_X, game->max_y,
              MAX_FRAMES[game->animation].first + runner_frame(game, MAX_FRAMES[game->animation].count));

    /* Max's sprite has a light gray contact shadow under the feet, made for
       a white page. The ground line is drawn after him so the shadow never
       lightens it. */
    gfx_rect(0, GROUND_Y, width, OUTLINE, COLOR_INK);

    draw_number(width - SCORE_MARGIN, SCORE_MARGIN, game->score, COLOR_INK);
    if (game->best > 0) {
        draw_number(width - SCORE_MARGIN, BEST_Y, game->best, COLOR_MUTED);
    }
}

static bool wiimote_jump(void) {
    return (WPAD_ButtonsHeld(WPAD_CHAN_0) & (WPAD_BUTTON_A | WPAD_BUTTON_B | WPAD_BUTTON_UP | WPAD_BUTTON_2)) != 0;
}

/* A NULL jump reads the Wii Remote. A NULL shown is not called. */
static void loop(uint32_t seed, play_jump_fn *jump, play_shown_fn *shown) {
    gfx_init();
    float width = gfx_width();

    runner game;
    runner_init(&game, width, GROUND_Y, seed);

    for (unsigned tick = 0;; tick++) {
        WPAD_ScanPads();
        if (WPAD_ButtonsDown(WPAD_CHAN_0) & WPAD_BUTTON_HOME) {
            return;
        }
        runner_step(&game, jump ? jump(&game, tick) : wiimote_jump());
        draw(&game, width, tick);
        gfx_present();
        if (shown) {
            shown(tick);
        }
    }
}

void play(void) {
    loop((uint32_t)gettime(), NULL, NULL);
}

void play_scripted(uint32_t seed, play_jump_fn *jump, play_shown_fn *shown) {
    loop(seed, jump, shown);
}
