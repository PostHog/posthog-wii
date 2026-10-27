#include "runner.h"
#include <stddef.h>
#include <stdint.h>

/* Speeds are pixels per tick. */
static const float JUMP_SPEED = 14.5F;
static const float AIR_JUMP_SPEED = 12.5F;
static const float MAX_FALL_SPEED = 18.0F;
/* Gravity is weakest while the button is held on the way up, so a long
   press jumps high, a tap jumps low, and every jump comes down fast. */
static const float GRAVITY_HELD = 0.75F;
static const float GRAVITY_FALLING = 1.15F;
static const float GRAVITY_RELEASED = 2.4F;

static const float START_SPEED = 5.0F;
static const float MAX_SPEED = 11.0F;
static const float ACCELERATION = 0.0012F;

/* One held jump clears a row of fires only above these speeds. */
static const float TWO_FIRES_SPEED = 7.0F;
static const float THREE_FIRES_SPEED = 9.0F;
static const float FIRE_SPACING = 44.0F;

/* The stretch after a group of fires, in ticks of travel. The shortest one
   leaves time to land from a full jump and react. */
static const float MIN_GAP_TICKS = 42.0F;
static const float MAX_GAP_TICKS = 85.0F;
static const float SPAWN_MARGIN = 20.0F;

static const float PICKUP_MIN_HEIGHT = 60.0F;
static const float PICKUP_HEIGHT_RANGE = 70.0F;

/* Hit areas inside the sprite cell, as left, top, right, bottom. Max's and
   the fire's are smaller than the art, so art that only touches at its
   edges does not end the run. */
typedef struct {
    float left, top, right, bottom;
} box;
static const box MAX_BOX = {22, 30, 58, 72};
static const box FIRE_BOX = {18, 12, 60, 72};
static const box PICKUP_BOX = {0, 0, 64, 64};

enum {
    /* A press this many ticks before landing still jumps on landing. */
    JUMP_BUFFER_TICKS = 8,
    /* A press right after a crash is usually a late jump, not a restart. */
    RESTART_LOCK_TICKS = 40,
    PICKUP_POINTS = 50,
    PIXELS_PER_POINT = 10,
    PICKUP_ODDS = 3,
    IDLE_TICKS_PER_FRAME = 8,
    AIR_TICKS_PER_FRAME = 3,
    FALLEN_TICKS_PER_FRAME = 3,
    RUN_PIXELS_PER_FRAME = 7,
};

/* Marsaglia's xorshift32 generator, which takes its shifts in this order. */
static const unsigned XORSHIFT_FIRST = 13U;
static const unsigned XORSHIFT_SECOND = 17U;
static const unsigned XORSHIFT_THIRD = 5U;

/* random_fraction spreads the low 16 bits of a random number over 0 to 1. */
static const uint32_t FRACTION_STEPS = 0xFFFFU;

static uint32_t next_random(runner *game) {
    game->random ^= game->random << XORSHIFT_FIRST;
    game->random ^= game->random >> XORSHIFT_SECOND;
    game->random ^= game->random << XORSHIFT_THIRD;
    return game->random;
}

static float random_fraction(runner *game) {
    return (float)(next_random(game) & FRACTION_STEPS) / (float)FRACTION_STEPS;
}

static float rest_y(const runner *game) {
    return game->ground_y - (float)RUNNER_MAX_FEET;
}

static bool grounded(const runner *game) {
    return game->max_y >= rest_y(game);
}

static void add_item(runner *game, runner_item_kind kind, float cell_x, float cell_y) {
    if (game->item_count < RUNNER_ITEM_CAP) {
        game->items[game->item_count++] = (runner_item){kind, cell_x, cell_y, next_random(game)};
    }
}

static void remove_item(runner *game, size_t index) {
    game->items[index] = game->items[--game->item_count];
}

static uint32_t most_fires(float speed) {
    if (speed >= THREE_FIRES_SPEED) {
        return 3;
    }
    if (speed >= TWO_FIRES_SPEED) {
        return 2;
    }
    return 1;
}

static void spawn_group(runner *game) {
    uint32_t fires = 1 + (next_random(game) % most_fires(game->speed));
    float group_x = game->width + SPAWN_MARGIN;
    for (uint32_t i = 0; i < fires; i++) {
        add_item(game, RUNNER_FIRE, group_x + ((float)i * FIRE_SPACING), game->ground_y - (float)RUNNER_FIRE_FOOT);
    }

    float group_width = ((float)(fires - 1) * FIRE_SPACING) + (float)RUNNER_CELL;
    float open = game->speed * (MIN_GAP_TICKS + (random_fraction(game) * (MAX_GAP_TICKS - MIN_GAP_TICKS)));
    game->gap = group_width + open;
    if (next_random(game) % PICKUP_ODDS == 0) {
        float height = PICKUP_MIN_HEIGHT + (random_fraction(game) * PICKUP_HEIGHT_RANGE);
        add_item(game, RUNNER_PICKUP, group_x + group_width + (open / 2), game->ground_y - height - PICKUP_BOX.bottom);
    }
}

static bool touches_max(const runner *game, const runner_item *item) {
    const box *hit = item->kind == RUNNER_FIRE ? &FIRE_BOX : &PICKUP_BOX;
    return (item->x + hit->left < (float)RUNNER_MAX_X + MAX_BOX.right &&
            item->x + hit->right > (float)RUNNER_MAX_X + MAX_BOX.left &&
            item->y + hit->top < game->max_y + MAX_BOX.bottom && item->y + hit->bottom > game->max_y + MAX_BOX.top) !=
           0;
}

static void fall(runner *game, float gravity) {
    game->vy += gravity;
    if (game->vy > MAX_FALL_SPEED) {
        game->vy = MAX_FALL_SPEED;
    }
    game->max_y += game->vy;
    if (grounded(game)) {
        game->max_y = rest_y(game);
        game->vy = 0;
    }
}

static void jump_if_asked(runner *game) {
    if (grounded(game) && game->ticks_since_press <= JUMP_BUFFER_TICKS) {
        game->vy = -JUMP_SPEED;
        game->air_jump_used = false;
    } else if (!grounded(game) && game->ticks_since_press == 0 && !game->air_jump_used) {
        game->vy = -AIR_JUMP_SPEED;
        game->air_jump_used = true;
    } else {
        return;
    }
    /* The press is used up, so it cannot also trigger a jump on landing. */
    game->ticks_since_press = JUMP_BUFFER_TICKS + 1;
}

static void scroll(runner *game) {
    if (game->speed < MAX_SPEED) {
        game->speed += ACCELERATION;
    }
    game->distance += game->speed;
    for (size_t i = game->item_count; i-- > 0;) {
        game->items[i].x -= game->speed;
        if (game->items[i].x < -(float)RUNNER_CELL) {
            remove_item(game, i);
        }
    }
    game->gap -= game->speed;
    if (game->gap <= 0) {
        spawn_group(game);
    }
}

static float gravity_for(const runner *game, bool jump) {
    if (game->vy >= 0) {
        return GRAVITY_FALLING;
    }
    return jump ? GRAVITY_HELD : GRAVITY_RELEASED;
}

/* Returns false when Max touched a fire. */
static bool run(runner *game, bool jump) {
    jump_if_asked(game);
    fall(game, gravity_for(game, jump));
    scroll(game);

    bool burned = false;
    for (size_t i = game->item_count; i-- > 0;) {
        if (!touches_max(game, &game->items[i])) {
            continue;
        }
        if (game->items[i].kind == RUNNER_FIRE) {
            burned = true;
        } else {
            game->bonus += PICKUP_POINTS;
            remove_item(game, i);
        }
    }
    game->score = ((uint32_t)game->distance / PIXELS_PER_POINT) + game->bonus;
    return (!burned) != 0;
}

static void start_run(runner *game) {
    game->phase = RUNNER_RUNNING;
    game->item_count = 0;
    game->distance = 0;
    game->bonus = 0;
    game->max_y = rest_y(game);
    game->vy = 0;
    game->speed = START_SPEED;
    game->gap = game->width / 2;
}

static runner_animation animation_for(const runner *game) {
    switch (game->phase) {
    case RUNNER_READY:
        return RUNNER_IDLE;
    case RUNNER_OVER:
        return RUNNER_FALLEN;
    case RUNNER_RUNNING:
        break;
    }
    return grounded(game) ? RUNNER_RUN : RUNNER_AIR;
}

void runner_init(runner *game, float width, float ground_y, uint32_t seed) {
    *game = (runner){
        .width = width,
        .ground_y = ground_y,
        /* A button still held from the previous screen is not a press. */
        .jump_held = true,
        .random = seed ? seed : 1,
    };
    game->max_y = rest_y(game);
}

void runner_step(runner *game, bool jump) {
    bool pressed = (jump && !game->jump_held) != 0;
    game->jump_held = jump;
    game->ticks_since_press = pressed ? 0 : game->ticks_since_press + 1;

    /* The fallen animation starts when a run ends, so its clock also times
       the restart lock. */
    bool restart = (game->phase == RUNNER_OVER && game->animation_ticks > RESTART_LOCK_TICKS) != 0;
    if (pressed && (game->phase == RUNNER_READY || restart)) {
        start_run(game);
    }

    if (game->phase == RUNNER_OVER) {
        fall(game, GRAVITY_FALLING);
    } else if (game->phase == RUNNER_RUNNING && !run(game, jump)) {
        game->phase = RUNNER_OVER;
        if (game->score > game->best) {
            game->best = game->score;
        }
    }

    runner_animation animation = animation_for(game);
    game->animation_ticks = animation == game->animation ? game->animation_ticks + 1 : 0;
    game->animation = animation;
}

unsigned runner_frame(const runner *game, unsigned frame_count) {
    unsigned fallen_frame = game->animation_ticks / FALLEN_TICKS_PER_FRAME;
    switch (game->animation) {
    case RUNNER_RUN:
        return (unsigned)game->distance / RUN_PIXELS_PER_FRAME % frame_count;
    case RUNNER_AIR:
        return game->animation_ticks / AIR_TICKS_PER_FRAME % frame_count;
    case RUNNER_FALLEN:
        return fallen_frame < frame_count ? fallen_frame : frame_count - 1;
    case RUNNER_IDLE:
    case RUNNER_ANIMATION_COUNT:
        break;
    }
    return game->animation_ticks / IDLE_TICKS_PER_FRAME % frame_count;
}
