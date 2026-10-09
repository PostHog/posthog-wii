#include "play.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "atlas.h"
#include "gfx.h"

/* Colors from PostHog's product UI. */
enum {
    COLOR_PAGE = 0xF3F4EF,
    COLOR_FAR = 0xEEEFE9,
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

    /* The near skyline changes its kind of chart every CHAPTER_BARS bars. */
    CHAPTER_BARS = 18,
    FUNNEL_STEPS = 6,
    TREND_WOBBLE = 24,
    TABLE_ROWS = 6,
    TABLE_PITCH = BAR_WIDTH + 4,

    FAR_PITCH = 24,
    FAR_WIDTH = 14,
    FAR_MIN_HEIGHT = 120,
    FAR_HEIGHT_RANGE = 130,
    FAR_SCROLL_DIVISOR = 8,

    /* Distances are pixels of track. Hogzilla enters once per lap. */
    HOGZILLA_SCALE = 3,
    HOGZILLA_LAP = 20000,
    HOGZILLA_ENTERS_AT = 4000,
    HOGZILLA_SCROLL_DIVISOR = 6,
    HOGZILLA_TICKS_PER_FRAME = 6,

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

typedef enum { SKYLINE_BARS, SKYLINE_FUNNEL, SKYLINE_TREND, SKYLINE_TABLE, SKYLINE_KINDS } skyline_kind;

typedef void column_fn(float x, unsigned index);

/* Draws a row of columns that scrolls slower than the ground. A column
   looks the same as it moves because its shape comes from its index. */
static void draw_columns(const runner *game, float width, unsigned scroll_divisor, unsigned pitch, column_fn *column) {
    unsigned scrolled = (unsigned)game->distance / scroll_divisor;
    unsigned first = scrolled / pitch;
    for (unsigned i = 0; (float)(i * pitch) < width + (float)pitch; i++) {
        column((float)(i * pitch) - (float)(scrolled % pitch), first + i);
    }
}

static void far_column(float x, unsigned index) {
    unsigned height = FAR_MIN_HEIGHT + index * HASH_MULTIPLIER % FAR_HEIGHT_RANGE;
    gfx_rect(x, (float)(GROUND_Y - height), FAR_WIDTH, (float)height, COLOR_FAR);
}

/* The run starts in a bar chart, because chapter 0 hashes to SKYLINE_BARS. */
static void near_column(float x, unsigned index) {
    unsigned place = index % CHAPTER_BARS;
    unsigned noise = index * HASH_MULTIPLIER % BAR_HEIGHT_RANGE;
    unsigned height = BAR_MIN_HEIGHT + noise;
    switch (((index / CHAPTER_BARS) * HASH_MULTIPLIER >> 16) % SKYLINE_KINDS) {
    case SKYLINE_FUNNEL:
        height = BAR_MIN_HEIGHT + BAR_HEIGHT_RANGE - place % FUNNEL_STEPS * (BAR_HEIGHT_RANGE / FUNNEL_STEPS);
        break;
    case SKYLINE_TREND:
        height = BAR_MIN_HEIGHT + place * BAR_HEIGHT_RANGE / CHAPTER_BARS + noise % TREND_WOBBLE;
        break;
    case SKYLINE_TABLE:
        for (unsigned row = 0; row < TABLE_ROWS - place % TABLE_ROWS; row++) {
            gfx_rect(x, (float)(GROUND_Y - (row + 1) * TABLE_PITCH), BAR_WIDTH, BAR_WIDTH, COLOR_BARS);
        }
        return;
    default:
        break;
    }
    gfx_rect(x, (float)(GROUND_Y - height), BAR_WIDTH, (float)height, COLOR_BARS);
}

/* Hogzilla walks the way Max runs, but slower, so he drifts from the right
   edge to the left one. */
static void draw_hogzilla(const runner *game, float width, unsigned tick) {
    unsigned into_lap = (unsigned)game->distance % HOGZILLA_LAP;
    if (into_lap < HOGZILLA_ENTERS_AT) {
        return;
    }
    float x = width - (float)(into_lap - HOGZILLA_ENTERS_AT) / HOGZILLA_SCROLL_DIVISOR;
    if (x > -(float)(ATLAS_CELL * HOGZILLA_SCALE)) {
        gfx_frame(x, (float)(GROUND_Y - RUNNER_MAX_FEET * HOGZILLA_SCALE),
                  ATLAS_HOGZILLA_FIRST + tick / HOGZILLA_TICKS_PER_FRAME % ATLAS_HOGZILLA_COUNT, HOGZILLA_SCALE);
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
    draw_columns(game, width, FAR_SCROLL_DIVISOR, FAR_PITCH, far_column);
    draw_columns(game, width, BAR_SCROLL_DIVISOR, BAR_PITCH, near_column);
    draw_hogzilla(game, width, tick);
    draw_ground(game, width);
    for (size_t i = 0; i < game->item_count; i++) {
        gfx_frame(game->items[i].x, game->items[i].y, item_frame(&game->items[i], tick), 1);
    }
    gfx_frame(RUNNER_MAX_X, game->max_y,
              MAX_FRAMES[game->animation].first + runner_frame(game, MAX_FRAMES[game->animation].count), 1);

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
