#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <gccore.h>
#include <wiiuse/wpad.h>

#include "autoplay.h"
#include "gfx.h"
#include "http.h"
#include "http_request.h"
#include "play.h"
#include "runner.h"

/* The entry point of the debug build. The game plays by itself on a fixed
   track and posts what it showed to DEBUG_HOST, because nothing else can
   show a Dolphin frame to an automated check. */

enum { TRACK_SEED = 1, STOP_JUMPING_TICK = 900, RESTART_PRESS_PERIOD = 20, PATH_CAP = 64 };

static const unsigned CAPTURE_TICKS[] = {5, 200, 420, 640, 860, 960, 1200};
enum { CAPTURE_COUNT = sizeof CAPTURE_TICKS / sizeof *CAPTURE_TICKS };

/* Clears every fire until STOP_JUMPING_TICK, then runs into one, then
   presses now and then so the next run starts. */
static bool scripted_jump(const runner *game, unsigned tick) {
    if (game->phase != RUNNER_RUNNING) {
        return tick % RESTART_PRESS_PERIOD == 0;
    }
    if (tick >= STOP_JUMPING_TICK) {
        return false;
    }
    return autoplay_jump(game);
}

/* Posts the frame of each capture tick, and "/done" after the last one so
   the script that waits for the frames knows the set is complete. */
static void post_captures(unsigned tick) {
    for (size_t i = 0; i < CAPTURE_COUNT; i++) {
        if (CAPTURE_TICKS[i] != tick) {
            continue;
        }
        gfx_picture shown = gfx_displayed();
        char path[PATH_CAP];
        int path_len = snprintf(path, sizeof path, "/shot?tick=%u&width=%u&height=%u", tick, shown.width, shown.height);
        if (path_len >= 0 && (size_t)path_len < sizeof path) {
            const http_request frame = {
                .host = DEBUG_HOST,
                .path = path,
                .content_type = "application/octet-stream",
                .body = shown.pixels,
                .body_len = shown.len,
            };
            http_post(&frame, NULL, 0);
        }
        if (i == CAPTURE_COUNT - 1) {
            const http_request done = {.host = DEBUG_HOST, .path = "/done", .content_type = "text/plain", .body = ""};
            http_post(&done, NULL, 0);
        }
    }
}

int main(void) {
    VIDEO_Init();
    WPAD_Init();
    play_scripted(TRACK_SEED, scripted_jump, post_captures);
    exit(0);
}
