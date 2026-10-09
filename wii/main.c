#include <stdio.h>
#include <stdlib.h>

#include <gccore.h>
#include <wiiuse/wpad.h>

#include "events.h"
#include "http.h"
#include "nand.h"
#include "play.h"
#include "playlog.h"
#include "settings.h"
#include "summary.h"
#include "upload.h"

enum { CONSOLE_MARGIN = 20 };

typedef struct {
    size_t found;
    playlog_session latest;
    upload batch;
} sync_state;

static const events_config CONFIG = {
    .api_key = POSTHOG_API_KEY,
    .utc_offset_minutes = UTC_OFFSET_MINUTES,
};

static bool has_token(void) {
    return CONFIG.api_key[0] != '\0';
}

static void init_console(void) {
    VIDEO_Init();
    WPAD_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *framebuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(framebuffer, CONSOLE_MARGIN, CONSOLE_MARGIN, mode->fbWidth - CONSOLE_MARGIN,
                 mode->xfbHeight - CONSOLE_MARGIN, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(framebuffer);
    VIDEO_ClearFrameBuffer(mode, framebuffer, COLOR_BLACK);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }
    printf("\x1b[2;0H");
}

static void on_session(void *ctx, const playlog_session *session) {
    sync_state *state = ctx;
    /* A line per session would scroll the console once per session, and
       each scroll moves the whole screen through uncached memory. */
    if (state->found++ == 0 || session->start_ticks > state->latest.start_ticks) {
        state->latest = *session;
    }
    if (has_token()) {
        upload_add(&state->batch, session);
    }
}

static void sync(void) {
    size_t image_len = 0;
    uint8_t *image = nand_read_message_board(&image_len);
    if (!image) {
        printf("Could not read the Message Board data.\n");
        return;
    }

    static sync_state state;
    upload_begin(&state.batch, &CONFIG, POSTHOG_HOST, http_post);
    printf("Reading the play history on this console.\n\n");
    playlog_each_session(image, image_len, on_session, &state);
    bool delivered = upload_end(&state.batch);
    free(image);

    printf("Sessions found: %u\n", (unsigned)state.found);
    if (state.found > 0) {
        char line[SESSION_SUMMARY_CAP];
        session_summary(&state.latest, line);
        printf("Latest session: %s\n", line);
    }
    if (!has_token()) {
        printf("No project token is set, so nothing was sent.\n");
        return;
    }
    printf("Sessions sent:  %u\n", (unsigned)state.batch.sent);
    printf("Destination:    %s\n", POSTHOG_HOST);
    if (!delivered && state.batch.status == 0) {
        printf("The destination did not answer. Run the app again to retry.\n");
    } else if (!delivered) {
        printf("The destination answered %ld. Run the app again to retry.\n", state.batch.status);
    }
}

/* Returns true for A and false for HOME. */
static bool wait_for_a_or_home(void) {
    for (;;) {
        WPAD_ScanPads();
        u32 pressed = WPAD_ButtonsDown(WPAD_CHAN_0);
        if (pressed & (WPAD_BUTTON_A | WPAD_BUTTON_HOME)) {
            return (pressed & WPAD_BUTTON_A) != 0;
        }
        VIDEO_WaitVSync();
    }
}

int main(void) {
    init_console();
    printf("PostHog for Wii\n\n");
    sync();
    printf("\nPress A to play. Press HOME to exit.\n");

    if (wait_for_a_or_home()) {
        play();
    }
    exit(0);
}
