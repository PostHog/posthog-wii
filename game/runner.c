#include "runner.h"

/* Speeds are pixels per tick. */
static const float JUMP_SPEED = 14.5f;
static const float AIR_JUMP_SPEED = 12.5f;
static const float MAX_FALL_SPEED = 18.0f;
/* Gravity is weakest while the button is held on the way up, so a long
   press jumps high, a tap jumps low, and every jump comes down fast. */
static const float GRAVITY_HELD = 0.75f;
static const float GRAVITY_FALLING = 1.15f;
static const float GRAVITY_RELEASED = 2.4f;

static const float START_SPEED = 5.0f;
static const float MAX_SPEED = 11.0f;
static const float ACCELERATION = 0.0012f;

/* One held jump clears a row of fires only above these speeds. */
static const float TWO_FIRES_SPEED = 7.0f;
static const float THREE_FIRES_SPEED = 9.0f;
static const float FIRE_SPACING = 44.0f;

/* The stretch after a group of fires, in ticks of travel. The shortest one
   leaves time to land from a full jump and react. */
static const float MIN_GAP_TICKS = 42.0f;
static const float MAX_GAP_TICKS = 85.0f;
static const float SPAWN_MARGIN = 20.0f;

static const float PICKUP_MIN_HEIGHT = 60.0f;
static const float PICKUP_HEIGHT_RANGE = 70.0f;

/* Hit areas inside the sprite cell: left, top, right, bottom. Max's and the
   fire's are smaller than the art, so a near miss is a miss. */
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

static uint32_t next_random(runner *r) {
    r->random ^= r->random << 13;
    r->random ^= r->random >> 17;
    r->random ^= r->random << 5;
    return r->random;
}

static float random_fraction(runner *r) {
    return (float)(next_random(r) & 0xFFFF) / 65535.0f;
}

static float rest_y(const runner *r) {
    return r->ground_y - (float)RUNNER_MAX_FEET;
}

static bool grounded(const runner *r) {
    return r->max_y >= rest_y(r);
}

static void add_item(runner *r, runner_item_kind kind, float x, float y) {
    if (r->item_count < RUNNER_ITEM_CAP) {
        r->items[r->item_count++] = (runner_item){kind, x, y, next_random(r)};
    }
}

static void remove_item(runner *r, size_t index) {
    r->items[index] = r->items[--r->item_count];
}

static void spawn_group(runner *r) {
    uint32_t most = r->speed >= THREE_FIRES_SPEED ? 3 : r->speed >= TWO_FIRES_SPEED ? 2 : 1;
    uint32_t fires = 1 + next_random(r) % most;
    float x = r->width + SPAWN_MARGIN;
    for (uint32_t i = 0; i < fires; i++) {
        add_item(r, RUNNER_FIRE, x + (float)i * FIRE_SPACING, r->ground_y - (float)RUNNER_FIRE_FOOT);
    }

    float group_width = (float)(fires - 1) * FIRE_SPACING + (float)RUNNER_CELL;
    float open = r->speed * (MIN_GAP_TICKS + random_fraction(r) * (MAX_GAP_TICKS - MIN_GAP_TICKS));
    r->gap = group_width + open;
    if (next_random(r) % PICKUP_ODDS == 0) {
        float height = PICKUP_MIN_HEIGHT + random_fraction(r) * PICKUP_HEIGHT_RANGE;
        add_item(r, RUNNER_PICKUP, x + group_width + open / 2, r->ground_y - height - PICKUP_BOX.bottom);
    }
}

static bool touches_max(const runner *r, const runner_item *item) {
    const box *b = item->kind == RUNNER_FIRE ? &FIRE_BOX : &PICKUP_BOX;
    return item->x + b->left < (float)RUNNER_MAX_X + MAX_BOX.right && item->x + b->right > (float)RUNNER_MAX_X + MAX_BOX.left &&
           item->y + b->top < r->max_y + MAX_BOX.bottom && item->y + b->bottom > r->max_y + MAX_BOX.top;
}

static void fall(runner *r, float gravity) {
    r->vy += gravity;
    if (r->vy > MAX_FALL_SPEED) {
        r->vy = MAX_FALL_SPEED;
    }
    r->max_y += r->vy;
    if (grounded(r)) {
        r->max_y = rest_y(r);
        r->vy = 0;
    }
}

static void jump_if_asked(runner *r) {
    if (grounded(r) && r->ticks_since_press <= JUMP_BUFFER_TICKS) {
        r->vy = -JUMP_SPEED;
        r->air_jump_used = false;
    } else if (!grounded(r) && r->ticks_since_press == 0 && !r->air_jump_used) {
        r->vy = -AIR_JUMP_SPEED;
        r->air_jump_used = true;
    } else {
        return;
    }
    /* The press is used up, so it cannot also trigger a jump on landing. */
    r->ticks_since_press = JUMP_BUFFER_TICKS + 1;
}

static void scroll(runner *r) {
    if (r->speed < MAX_SPEED) {
        r->speed += ACCELERATION;
    }
    r->distance += r->speed;
    for (size_t i = r->item_count; i-- > 0;) {
        r->items[i].x -= r->speed;
        if (r->items[i].x < -(float)RUNNER_CELL) {
            remove_item(r, i);
        }
    }
    r->gap -= r->speed;
    if (r->gap <= 0) {
        spawn_group(r);
    }
}

/* Returns false when Max touched a fire. */
static bool run(runner *r, bool jump) {
    jump_if_asked(r);
    fall(r, r->vy >= 0 ? GRAVITY_FALLING : jump ? GRAVITY_HELD : GRAVITY_RELEASED);
    scroll(r);

    bool burned = false;
    for (size_t i = r->item_count; i-- > 0;) {
        if (!touches_max(r, &r->items[i])) {
            continue;
        }
        if (r->items[i].kind == RUNNER_FIRE) {
            burned = true;
        } else {
            r->bonus += PICKUP_POINTS;
            remove_item(r, i);
        }
    }
    r->score = (uint32_t)r->distance / PIXELS_PER_POINT + r->bonus;
    return !burned;
}

static void start_run(runner *r) {
    r->phase = RUNNER_RUNNING;
    r->item_count = 0;
    r->distance = 0;
    r->bonus = 0;
    r->max_y = rest_y(r);
    r->vy = 0;
    r->speed = START_SPEED;
    r->gap = r->width / 2;
}

static runner_animation animation_for(const runner *r) {
    switch (r->phase) {
    case RUNNER_READY:
        return RUNNER_IDLE;
    case RUNNER_OVER:
        return RUNNER_FALLEN;
    case RUNNER_RUNNING:
        break;
    }
    return grounded(r) ? RUNNER_RUN : RUNNER_AIR;
}

void runner_init(runner *r, float width, float ground_y, uint32_t seed) {
    *r = (runner){
        .width = width,
        .ground_y = ground_y,
        /* A button still held from the previous screen is not a press. */
        .jump_held = true,
        .random = seed ? seed : 1,
    };
    r->max_y = rest_y(r);
}

void runner_step(runner *r, bool jump) {
    bool pressed = jump && !r->jump_held;
    r->jump_held = jump;
    r->ticks_since_press = pressed ? 0 : r->ticks_since_press + 1;

    /* The fallen animation starts when a run ends, so its clock also times
       the restart lock. */
    bool restart = r->phase == RUNNER_OVER && r->animation_ticks > RESTART_LOCK_TICKS;
    if (pressed && (r->phase == RUNNER_READY || restart)) {
        start_run(r);
    }

    if (r->phase == RUNNER_OVER) {
        fall(r, GRAVITY_FALLING);
    } else if (r->phase == RUNNER_RUNNING && !run(r, jump)) {
        r->phase = RUNNER_OVER;
        if (r->score > r->best) {
            r->best = r->score;
        }
    }

    runner_animation animation = animation_for(r);
    r->animation_ticks = animation == r->animation ? r->animation_ticks + 1 : 0;
    r->animation = animation;
}

unsigned runner_frame(const runner *r, unsigned frame_count) {
    unsigned fallen_frame = r->animation_ticks / FALLEN_TICKS_PER_FRAME;
    switch (r->animation) {
    case RUNNER_RUN:
        return (unsigned)r->distance / RUN_PIXELS_PER_FRAME % frame_count;
    case RUNNER_AIR:
        return r->animation_ticks / AIR_TICKS_PER_FRAME % frame_count;
    case RUNNER_FALLEN:
        return fallen_frame < frame_count ? fallen_frame : frame_count - 1;
    case RUNNER_IDLE:
    case RUNNER_ANIMATION_COUNT:
        break;
    }
    return r->animation_ticks / IDLE_TICKS_PER_FRAME % frame_count;
}
