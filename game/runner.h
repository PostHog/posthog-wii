#ifndef RUNNER_H
#define RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An endless runner. Max runs in place while fires and pickups scroll
   toward him. A fire ends the run and a pickup adds to the score. */

enum {
    RUNNER_CELL = 80,  /* every sprite is drawn in a square cell of this size */
    RUNNER_MAX_X = 96, /* the left edge of Max's cell */
    /* The row of a cell that stands on the ground, for Max and for a fire. */
    RUNNER_MAX_FEET = 74,
    RUNNER_FIRE_FOOT = 72,
    RUNNER_ITEM_CAP = 12,
};

typedef enum { RUNNER_READY, RUNNER_RUNNING, RUNNER_OVER } runner_phase;

typedef enum {
    RUNNER_IDLE,
    RUNNER_RUN,
    RUNNER_AIR,
    RUNNER_FALLEN,
    RUNNER_ANIMATION_COUNT,
} runner_animation;

typedef enum { RUNNER_FIRE, RUNNER_PICKUP } runner_item_kind;

typedef struct {
    runner_item_kind kind;
    float x, y;       /* top-left corner of the sprite cell */
    uint32_t variant; /* any number; the caller picks a picture from it */
} runner_item;

typedef struct {
    /* What to draw and what a player can see. Coordinates are pixels with y
       pointing down. Max's cell has its top-left corner at RUNNER_MAX_X,
       max_y. */
    runner_phase phase;
    float max_y;
    runner_animation animation;
    uint32_t animation_ticks;
    runner_item items[RUNNER_ITEM_CAP];
    size_t item_count;
    float distance; /* pixels scrolled in this run */
    float speed;    /* pixels scrolled per tick */
    uint32_t score, best;

    /* Internal state. */
    float width, ground_y, vy, gap;
    bool jump_held, air_jump_used;
    uint32_t ticks_since_press, bonus, random;
} runner;

void runner_init(runner *r, float width, float ground_y, uint32_t seed);

/* Advances one tick of 1/60 s. jump is true while the jump button is held.
   A press starts a run, and after a run ends a press starts the next one. */
void runner_step(runner *r, bool jump);

/* The frame to draw from a strip of frame_count frames of r->animation. */
unsigned runner_frame(const runner *r, unsigned frame_count);

#endif
