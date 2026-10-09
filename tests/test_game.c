#include <math.h>

#include "autoplay.h"
#include "check.h"
#include "runner.h"

static const float WIDTH = 854.0f;
static const float GROUND = 432.0f;
static const float REST_Y = 432.0f - (float)RUNNER_MAX_FEET;
static const float FIRE_Y = 432.0f - (float)RUNNER_FIRE_FOOT;

static runner running(void) {
    runner r;
    runner_init(&r, WIDTH, GROUND, 7);
    runner_step(&r, false);
    runner_step(&r, true);
    for (int i = 0; i < 60 && r.max_y < REST_Y; i++) {
        runner_step(&r, false);
    }
    runner_step(&r, false);
    return r;
}

/* Keeps new obstacles away so a test controls what is on the track. */
static runner running_on_empty_track(void) {
    runner r = running();
    r.item_count = 0;
    r.gap = 1e9f;
    return r;
}

static float peak_height(runner *r, int ticks_held) {
    float peak = 0.0f;
    for (int i = 0; i < 120; i++) {
        runner_step(r, i < ticks_held);
        peak = fmaxf(peak, REST_Y - r->max_y);
    }
    return peak;
}

static void test_waits_for_a_fresh_press_then_runs_and_scores(void) {
    runner r;
    runner_init(&r, WIDTH, GROUND, 7);
    for (int i = 0; i < 30; i++) {
        runner_step(&r, true);
    }
    CHECK(r.phase == RUNNER_READY);
    CHECK(r.animation == RUNNER_IDLE);

    runner_step(&r, false);
    runner_step(&r, true);
    CHECK(r.phase == RUNNER_RUNNING);
    for (int i = 0; i < 120; i++) {
        runner_step(&r, false);
    }
    CHECK(r.score > 50 && r.score < 80);
    CHECK(r.animation == RUNNER_RUN);
}

static void test_a_long_press_jumps_higher_than_a_tap(void) {
    runner held = running_on_empty_track();
    runner tapped = held;
    float held_peak = peak_height(&held, 40);
    float tapped_peak = peak_height(&tapped, 2);

    CHECK(held_peak > 120.0f);
    CHECK(tapped_peak > 15.0f && tapped_peak < held_peak * 0.5f);
    CHECK(held.max_y == REST_Y && tapped.max_y == REST_Y);
}

static void test_jumps_once_more_in_the_air_and_no_further(void) {
    runner r = running_on_empty_track();
    for (int i = 0; i < 12; i++) {
        runner_step(&r, true);
    }
    runner_step(&r, false);
    runner_step(&r, true);
    CHECK(r.vy < -10.0f);

    runner_step(&r, false);
    float before = r.vy;
    runner_step(&r, true);
    CHECK(r.vy > before);
    CHECK(r.animation == RUNNER_AIR);
}

static void test_a_press_just_before_landing_jumps_on_landing(void) {
    runner r = running_on_empty_track();
    runner_step(&r, true);
    runner_step(&r, false);
    runner_step(&r, true);
    runner_step(&r, false);

    int ticks = 0;
    while (REST_Y - r.max_y > 12.0f && ticks++ < 200) {
        runner_step(&r, false);
    }
    CHECK(r.vy > 0.0f);
    runner_step(&r, true);
    CHECK(r.vy >= 0.0f);

    bool jumped_again = false;
    for (int i = 0; i < 8; i++) {
        runner_step(&r, true);
        jumped_again = jumped_again || r.vy < -10.0f;
    }
    CHECK(jumped_again);
}

static void test_a_fire_ends_the_run_and_a_press_starts_the_next(void) {
    runner r = running_on_empty_track();
    r.items[r.item_count++] = (runner_item){RUNNER_FIRE, 300.0f, FIRE_Y, 0};

    for (int i = 0; i < 120 && r.phase == RUNNER_RUNNING; i++) {
        runner_step(&r, false);
    }
    CHECK(r.phase == RUNNER_OVER);
    CHECK(r.animation == RUNNER_FALLEN);
    uint32_t first_score = r.score;
    CHECK(r.best == first_score && first_score > 0);

    runner_step(&r, true);
    CHECK(r.phase == RUNNER_OVER);
    for (int i = 0; i < 60; i++) {
        runner_step(&r, false);
    }
    CHECK(runner_frame(&r, 21) == 20);

    runner_step(&r, true);
    CHECK(r.phase == RUNNER_RUNNING);
    CHECK(r.score == 0 && r.item_count == 0);
    CHECK(r.best == first_score);
}

static void test_a_jump_clears_a_fire(void) {
    runner r = running_on_empty_track();
    r.items[r.item_count++] = (runner_item){RUNNER_FIRE, 220.0f, FIRE_Y, 0};

    for (int i = 0; i < 120; i++) {
        runner_step(&r, i >= 4 && i < 40);
    }
    CHECK(r.phase == RUNNER_RUNNING);
    CHECK(r.item_count == 0);
}

static void test_a_pickup_adds_points_and_missing_one_costs_nothing(void) {
    runner r = running_on_empty_track();
    r.items[r.item_count++] = (runner_item){RUNNER_PICKUP, 200.0f, REST_Y + 40.0f, 0};
    r.items[r.item_count++] = (runner_item){RUNNER_PICKUP, 400.0f, REST_Y - 150.0f, 0};
    uint32_t before = r.score;

    for (int i = 0; i < 40; i++) {
        runner_step(&r, false);
    }
    CHECK(r.item_count == 1);
    CHECK(r.score >= before + 50 && r.score < before + 50 + 40);

    for (int i = 0; i < 120; i++) {
        runner_step(&r, false);
    }
    CHECK(r.phase == RUNNER_RUNNING);
    CHECK(r.item_count == 0);
}

static void test_every_generated_track_can_be_survived(void) {
    for (uint32_t seed = 1; seed <= 8; seed++) {
        runner r;
        runner_init(&r, WIDTH, GROUND, seed);
        runner_step(&r, false);
        runner_step(&r, true);
        runner_step(&r, false);
        uint32_t ticks = 0;
        while (r.phase == RUNNER_RUNNING && ticks < 9000) {
            runner_step(&r, autoplay_jump(&r));
            ticks++;
        }
        CHECK(ticks == 9000);
        CHECK(r.speed > 10.9f);
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
