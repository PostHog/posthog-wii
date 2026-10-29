#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "buttons.h"
#include "cursor.h"
#include "events.h"
#include "gfx.h"
#include "http.h"
#include "nand.h"
#include "noise.h"
#include "play.h"
#include "playlog.h"
#include "sdcard.h"
#include "settings.h"
#include "summary.h"
#include "ticks.h"
#include "upload.h"

enum { SETTINGS_TEXT_CAP = 1024 };

typedef struct {
    size_t found;
    playlog_session latest;
    bool sending;
    uint64_t cursor;
    upload batch;
} sync_state;

static void on_session(void *ctx, const playlog_session *session) {
    sync_state *state = ctx;
    /* A line per session would scroll the console once per session, and
       each scroll moves the whole screen through uncached memory. */
    if (state->found++ == 0 || session->start_ticks > state->latest.start_ticks) {
        state->latest = *session;
    }
    if (state->sending && session->start_ticks > state->cursor) {
        upload_add(&state->batch, session);
    }
}

/* libogc starts the time base from the console's clock, in the play log's
   ticks. */
static int64_t console_now_ms(void) {
    return ticks_to_unix_ms(gettime());
}

static void sync(void) {
    static settings config;
    static char settings_text[SETTINGS_TEXT_CAP];
    settings_parse(&config, settings_text, sdcard_read(SDCARD_SETTINGS_FILE, settings_text, sizeof settings_text));

    printf("Reading the play history on this console.\n\n");

    static sync_state state;
    state.sending = (bool)(config.host[0] != '\0' && config.api_key[0] != '\0');
    char cursor_text[CURSOR_TEXT_CAP];
    state.cursor = cursor_read(cursor_text, sdcard_read(SDCARD_CURSOR_FILE, cursor_text, sizeof cursor_text),
                               config.host, config.api_key);
    const events_config events = {.api_key = config.api_key, .now_ms = console_now_ms};
    upload_begin(&state.batch, &events, config.host, http_post);
    bool read_whole = nand_each_session(on_session, &state);
    bool delivered = upload_end(&state.batch);
    /* A failed read or upload leaves the cursor alone, so the next run sends
       the same sessions again. PostHog merges events that share an ID. A
       session that a failed read skipped can be older than the newest one
       found, and a new cursor would pass it by. */
    if (read_whole && delivered && state.batch.sent > 0) {
        sdcard_write(SDCARD_CURSOR_FILE, cursor_text,
                     cursor_write(cursor_text, config.host, config.api_key, state.latest.start_ticks));
    }
    if (!read_whole) {
        printf("%s\n", nand_failure());
    }

    printf("Sessions found: %u\n", (unsigned)state.found);
    if (state.found > 0) {
        char line[SESSION_SUMMARY_CAP];
        session_summary(&state.latest, line);
        printf("Latest session: %s\n", line);
    }
    if (!state.sending) {
        printf("The app sent nothing. Add a host and a project token to\n" SDCARD_SETTINGS_FILE " on the SD card.\n");
        return;
    }
    printf("New ones sent:  %u\n", (unsigned)state.batch.sent);
    printf("Destination:    %s\n", config.host);
    noise_report noise = noise_last();
    if (noise.readings > 0) {
        printf("Randomness:     %u of %u Wii Remote readings changed\n", noise.changed, noise.readings);
    }
    if (state.batch.status == UPLOAD_NO_ANSWER) {
        printf("%s\n", http_failure());
    } else if (state.batch.status == UPLOAD_NOT_CAPTURE) {
        printf("The host answered, but not as PostHog does. Check the host in\nthe settings file.\n");
    } else if (!delivered) {
        printf("The destination answered %ld. Run the app again to retry.\n", state.batch.status);
    }
}

/* Returns true for A and false for a button that ends the app. */
static bool wait_for_play(void) {
    for (;;) {
        WPAD_ScanPads();
        if (WPAD_ButtonsDown(WPAD_CHAN_0) & WPAD_BUTTON_A) {
            return true;
        }
        if (buttons_quit()) {
            return false;
        }
        VIDEO_WaitVSync();
    }
}

int main(void) {
    gfx_console();
    printf("\x1b[2;0H");
    WPAD_Init();
    buttons_watch();
    printf("PostHog for Wii\n\n");
    sync();
    printf("\nPress A to play. Press HOME to exit.\n");

    if (wait_for_play()) {
        play();
    }
    /* SYS_RETURNTOMENU would write state.dat to the NAND. */
    if (buttons_power_off()) {
        SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    }
    exit(0);
}
