#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "autoplay.h"
#include "check.h"
#include "runner.h"

enum {
    ONE_SECOND = 60,
    HALF_SECOND = ONE_SECOND / 2,
    TWO_SECONDS = 2 * ONE_SECOND,
    LONG_PRESS_TICKS = 40,
    RISING_TICKS = 12,
    FALL_TICK_LIMIT = 200,
    LANDING_WINDOW_TICKS = 8,
    SEEDS_TRIED = 8,
    SURVIVAL_TICKS = 150 * ONE_SECOND,
};

static const float WIDTH = 854.0F;
static const float GROUND = 432.0F;
static const float REST_Y = 432.0F - (float)RUNNER_MAX_FEET;
static const float FIRE_Y = 432.0F - (float)RUNNER_FIRE_FOOT;
static const uint32_t SEED = 7;
static const float JUMPING_VY = -10.0F;

static runner running(void) {
    runner game;
    runner_init(&game, WIDTH, GROUND, SEED);
    runner_step(&game, false);
    runner_step(&game, true);
    for (int tick = 0; tick < ONE_SECOND && game.max_y < REST_Y; tick++) {
        runner_step(&game, false);
    }
    runner_step(&game, false);
    return game;
}

/* Keeps new obstacles away so a test controls what is on the track. */
static runner running_on_empty_track(void) {
    static const float no_next_obstacle = 1e9F;
    runner game = running();
    game.item_count = 0;
    game.gap = no_next_obstacle;
    return game;
}

static float peak_height(runner *game, int ticks_held) {
    float peak = 0.0F;
    for (int tick = 0; tick < TWO_SECONDS; tick++) {
        runner_step(game, tick < ticks_held);
        peak = fmaxf(peak, REST_Y - game->max_y);
    }
    return peak;
}

static void test_waits_for_a_fresh_press_then_runs_and_scores(void) {
    runner game;
    runner_init(&game, WIDTH, GROUND, SEED);
    for (int tick = 0; tick < HALF_SECOND; tick++) {
        runner_step(&game, true);
    }
    CHECK(game.phase == RUNNER_READY);
    CHECK(game.animation == RUNNER_IDLE);

    runner_step(&game, false);
    runner_step(&game, true);
    CHECK(game.phase == RUNNER_RUNNING);
    for (int tick = 0; tick < TWO_SECONDS; tick++) {
        runner_step(&game, false);
    }
    CHECK(game.score > 50 && game.score < 80);
    CHECK(game.animation == RUNNER_RUN);
}

static void test_a_long_press_jumps_higher_than_a_tap(void) {
    runner held = running_on_empty_track();
    runner tapped = held;
    float held_peak = peak_height(&held, LONG_PRESS_TICKS);
    float tapped_peak = peak_height(&tapped, 2);
    const float high_jump = 120.0F;
    const float low_jump = 15.0F;

    CHECK(held_peak > high_jump);
    CHECK(tapped_peak > low_jump && tapped_peak < held_peak / 2);
    CHECK(held.max_y == REST_Y && tapped.max_y == REST_Y);
}

static void test_jumps_once_more_in_the_air_and_no_further(void) {
    runner game = running_on_empty_track();
    for (int tick = 0; tick < RISING_TICKS; tick++) {
        runner_step(&game, true);
    }
    runner_step(&game, false);
    runner_step(&game, true);
    CHECK(game.vy < JUMPING_VY);

    runner_step(&game, false);
    float before = game.vy;
    runner_step(&game, true);
    CHECK(game.vy > before);
    CHECK(game.animation == RUNNER_AIR);
}

static void test_a_press_just_before_landing_jumps_on_landing(void) {
    static const float almost_landed = 12.0F;
    runner game = running_on_empty_track();
    runner_step(&game, true);
    runner_step(&game, false);
    runner_step(&game, true);
    runner_step(&game, false);

    int ticks = 0;
    while (REST_Y - game.max_y > almost_landed && ticks++ < FALL_TICK_LIMIT) {
        runner_step(&game, false);
    }
    CHECK(game.vy > 0.0F);
    runner_step(&game, true);
    CHECK(game.vy >= 0.0F);

    bool jumped_again = false;
    for (int tick = 0; tick < LANDING_WINDOW_TICKS; tick++) {
        runner_step(&game, true);
        jumped_again = ((jumped_again || game.vy < JUMPING_VY) != 0);
    }
    CHECK(jumped_again);
}

static void test_a_fire_ends_the_run_and_a_press_starts_the_next(void) {
    const runner_item fire_ahead = {RUNNER_FIRE, 300.0F, FIRE_Y, 0};
    runner game = running_on_empty_track();
    game.items[game.item_count++] = fire_ahead;

    for (int tick = 0; tick < TWO_SECONDS && game.phase == RUNNER_RUNNING; tick++) {
        runner_step(&game, false);
    }
    CHECK(game.phase == RUNNER_OVER);
    CHECK(game.animation == RUNNER_FALLEN);
    uint32_t first_score = game.score;
    CHECK(game.best == first_score && first_score > 0);

    runner_step(&game, true);
    CHECK(game.phase == RUNNER_OVER);
    for (int tick = 0; tick < ONE_SECOND; tick++) {
        runner_step(&game, false);
    }
    CHECK(runner_frame(&game, 21) == 20);

    runner_step(&game, true);
    CHECK(game.phase == RUNNER_RUNNING);
    CHECK(game.score == 0 && game.item_count == 0);
    CHECK(game.best == first_score);
}

static void test_a_jump_clears_a_fire(void) {
    const runner_item fire_ahead = {RUNNER_FIRE, 220.0F, FIRE_Y, 0};
    const int jump_from = 4;
    const int jump_until = 40;
    runner game = running_on_empty_track();
    game.items[game.item_count++] = fire_ahead;

    for (int tick = 0; tick < TWO_SECONDS; tick++) {
        runner_step(&game, (tick >= jump_from && tick < jump_until) != 0);
    }
    CHECK(game.phase == RUNNER_RUNNING);
    CHECK(game.item_count == 0);
}

static void test_a_pickup_adds_points_and_missing_one_costs_nothing(void) {
    const runner_item pickup_in_path = {RUNNER_PICKUP, 200.0F, REST_Y + 40.0F, 0};
    const runner_item pickup_overhead = {RUNNER_PICKUP, 400.0F, REST_Y - 150.0F, 0};
    const int ticks_to_reach_first = 40;
    runner game = running_on_empty_track();
    game.items[game.item_count++] = pickup_in_path;
    game.items[game.item_count++] = pickup_overhead;
    uint32_t before = game.score;

    for (int tick = 0; tick < ticks_to_reach_first; tick++) {
        runner_step(&game, false);
    }
    CHECK(game.item_count == 1);
    CHECK(game.score >= before + 50 && game.score < before + 50 + 40);

    for (int tick = 0; tick < TWO_SECONDS; tick++) {
        runner_step(&game, false);
    }
    CHECK(game.phase == RUNNER_RUNNING);
    CHECK(game.item_count == 0);
}

static void test_every_generated_track_can_be_survived(void) {
    const float near_top_speed = 10.9F;
    for (uint32_t seed = 1; seed <= SEEDS_TRIED; seed++) {
        runner game;
        runner_init(&game, WIDTH, GROUND, seed);
        runner_step(&game, false);
        runner_step(&game, true);
        runner_step(&game, false);
        uint32_t ticks = 0;
        while (game.phase == RUNNER_RUNNING && ticks < SURVIVAL_TICKS) {
            runner_step(&game, autoplay_jump(&game));
            ticks++;
        }
        CHECK(ticks == SURVIVAL_TICKS);
        CHECK(game.speed > near_top_speed);
    }
}

int main(void) {
    test_waits_for_a_fresh_press_then_runs_and_scores();
    test_a_long_press_jumps_higher_than_a_tap();
    test_jumps_once_more_in_the_air_and_no_further();
    test_a_press_just_before_landing_jumps_on_landing();
    test_a_fire_ends_the_run_and_a_press_starts_the_next();
    test_a_jump_clears_a_fire();
    test_a_pickup_adds_points_and_missing_one_costs_nothing();
    test_every_generated_track_can_be_survived();
    return report();
}
