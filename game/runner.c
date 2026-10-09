#include "runner.h"

/* Speeds are pixels per tick. */
static const float JUMP_SPEED = 14.5f;
static const float DOUBLE_JUMP_SPEED = 12.5f;
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

static const float FLAG_MIN_HEIGHT = 60.0f;
static const float FLAG_HEIGHT_RANGE = 70.0f;

/* Rows of the sprite cell that touch the ground. */
static const float MAX_FEET = 74.0f;
static const float FIRE_FOOT = 72.0f;

/* Hit areas inside the sprite cell: left, top, right, bottom. Max's and the
   fire's are smaller than the art, so a near miss is a miss. */
typedef struct {
    float left, top, right, bottom;
} box;
static const box MAX_BOX = {22, 30, 58, 72};
static const box FIRE_BOX = {18, 12, 60, 72};
static const box FLAG_BOX = {0, 0, 31, 21};

enum {
    MAX_JUMPS = 2,
    /* A press this many ticks before landing still jumps on landing. */
    JUMP_BUFFER_TICKS = 8,
    /* A press right after a crash is usually a late jump, not a restart. */
    RESTART_LOCK_TICKS = 40,
    FLAG_POINTS = 50,
    PIXELS_PER_POINT = 10,
    FLAG_ODDS = 3,
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
    return r->ground_y - MAX_FEET;
}

static void set_animation(runner *r, runner_animation animation) {
    r->animation_ticks = animation == r->animation ? r->animation_ticks + 1 : 0;
    r->animation = animation;
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
        add_item(r, RUNNER_FIRE, x + (float)i * FIRE_SPACING, r->ground_y - FIRE_FOOT);
    }

    float group_width = (float)(fires - 1) * FIRE_SPACING + (float)RUNNER_CELL;
    float open = r->speed * (MIN_GAP_TICKS + random_fraction(r) * (MAX_GAP_TICKS - MIN_GAP_TICKS));
    r->gap = group_width + open;
    if (next_random(r) % FLAG_ODDS == 0) {
        float height = FLAG_MIN_HEIGHT + random_fraction(r) * FLAG_HEIGHT_RANGE;
        add_item(r, RUNNER_FLAG, x + group_width + open / 2, r->ground_y - height - FLAG_BOX.bottom);
    }
}

static bool touches_max(const runner *r, const runner_item *item) {
    const box *b = item->kind == RUNNER_FIRE ? &FIRE_BOX : &FLAG_BOX;
    return item->x + b->left < (float)RUNNER_MAX_X + MAX_BOX.right && item->x + b->right > (float)RUNNER_MAX_X + MAX_BOX.left &&
           item->y + b->top < r->max_y + MAX_BOX.bottom && item->y + b->bottom > r->max_y + MAX_BOX.top;
}

static void fall(runner *r, float gravity) {
    r->vy += gravity;
    if (r->vy > MAX_FALL_SPEED) {
        r->vy = MAX_FALL_SPEED;
    }
    r->max_y += r->vy;
    if (r->max_y >= rest_y(r)) {
        r->max_y = rest_y(r);
        r->vy = 0;
        r->jumps_used = 0;
    }
}

static void jump_if_asked(runner *r) {
    bool grounded = r->max_y >= rest_y(r);
    if (grounded && r->ticks_since_press <= JUMP_BUFFER_TICKS) {
        r->vy = -JUMP_SPEED;
        r->jumps_used = 1;
    } else if (!grounded && r->ticks_since_press == 0 && r->jumps_used < MAX_JUMPS) {
        r->vy = -DOUBLE_JUMP_SPEED;
        r->jumps_used = MAX_JUMPS;
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

static void end_run(runner *r) {
    r->phase = RUNNER_OVER;
    r->over_ticks = 0;
    if (r->score > r->best) {
        r->best = r->score;
    }
    set_animation(r, RUNNER_FALLEN);
}

static void run(runner *r, bool jump) {
    jump_if_asked(r);
    fall(r, r->vy >= 0 ? GRAVITY_FALLING : jump ? GRAVITY_HELD : GRAVITY_RELEASED);
    scroll(r);

    for (size_t i = r->item_count; i-- > 0;) {
        if (!touches_max(r, &r->items[i])) {
            continue;
        }
        if (r->items[i].kind == RUNNER_FIRE) {
            end_run(r);
            return;
        }
        r->bonus += FLAG_POINTS;
        remove_item(r, i);
    }
    r->score = (uint32_t)r->distance / PIXELS_PER_POINT + r->bonus;
    set_animation(r, r->max_y >= rest_y(r) ? RUNNER_RUN : RUNNER_AIR);
}

static void start_run(runner *r) {
    r->phase = RUNNER_RUNNING;
    r->item_count = 0;
    r->distance = 0;
    r->score = 0;
    r->bonus = 0;
    r->max_y = rest_y(r);
    r->vy = 0;
    r->jumps_used = 0;
    r->speed = START_SPEED;
    r->gap = r->width / 2;
}

void runner_init(runner *r, float width, float ground_y, uint32_t seed) {
    *r = (runner){
        .width = width,
        .ground_y = ground_y,
        .max_y = ground_y - MAX_FEET,
        /* A button still held from the previous screen is not a press. */
        .jump_held = true,
        .ticks_since_press = JUMP_BUFFER_TICKS + 1,
        .random = seed ? seed : 1,
    };
}

void runner_step(runner *r, bool jump) {
    bool pressed = jump && !r->jump_held;
    r->jump_held = jump;
    r->ticks_since_press = pressed ? 0 : r->ticks_since_press + 1;

    if (r->phase == RUNNER_READY && !pressed) {
        set_animation(r, RUNNER_IDLE);
    } else if (r->phase == RUNNER_OVER && !(pressed && r->over_ticks > RESTART_LOCK_TICKS)) {
        r->over_ticks++;
        fall(r, GRAVITY_FALLING);
        set_animation(r, RUNNER_FALLEN);
    } else {
        if (r->phase != RUNNER_RUNNING) {
            start_run(r);
        }
        run(r, jump);
    }
}

unsigned runner_frame(const runner *r, unsigned frame_count) {
    switch (r->animation) {
    case RUNNER_RUN:
        return (unsigned)r->distance / RUN_PIXELS_PER_FRAME % frame_count;
    case RUNNER_AIR:
        return r->animation_ticks / AIR_TICKS_PER_FRAME % frame_count;
    case RUNNER_FALLEN: {
        unsigned frame = r->animation_ticks / FALLEN_TICKS_PER_FRAME;
        return frame < frame_count ? frame : frame_count - 1;
    }
    default:
        return r->animation_ticks / IDLE_TICKS_PER_FRAME % frame_count;
    }
}
