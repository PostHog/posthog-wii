#include <stdio.h>
#include <stdlib.h>

#include <gccore.h>
#include <wiiuse/wpad.h>

#include "events.h"
#include "nand.h"
#include "play.h"
#include "playlog.h"
#include "send.h"
#include "settings.h"
#include "summary.h"

enum { SESSIONS_PER_REQUEST = 25, CONSOLE_MARGIN = 20 };

/* The debug build runs unattended, so it cannot wait for a button. */
#ifdef DEBUG_HOST
enum { SKIP_PROMPT = 1 };
#else
enum { SKIP_PROMPT = 0 };
#endif

typedef struct {
    playlog_session pending[SESSIONS_PER_REQUEST];
    size_t pending_count;
    size_t found;
    size_t sent;
    long last_status;
} sync_state;

static const events_config CONFIG = {
    .api_key = POSTHOG_API_KEY,
    .utc_offset_minutes = UTC_OFFSET_MINUTES,
};

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

static void flush(sync_state *state) {
    static char json[EVENTS_JSON_CAP(SESSIONS_PER_REQUEST)];
    if (state->pending_count == 0 || CONFIG.api_key[0] == '\0') {
        state->pending_count = 0;
        return;
    }
    size_t json_len = events_batch_json(&CONFIG, state->pending, state->pending_count, json, sizeof json);
    state->last_status = send_batch(POSTHOG_HOST, json, json_len);
    if (state->last_status == 200) {
        state->sent += state->pending_count;
    }
    state->pending_count = 0;
}

static void on_session(void *ctx, const playlog_session *session) {
    sync_state *state = ctx;
    char line[SESSION_SUMMARY_CAP];
    session_summary(session, line);
    printf("  %s\n", line);

    state->found++;
    state->pending[state->pending_count++] = *session;
    if (state->pending_count == SESSIONS_PER_REQUEST) {
        flush(state);
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
    printf("Play history on this console:\n\n");
    playlog_each_session(image, image_len, on_session, &state);
    flush(&state);
    free(image);

    printf("\nSessions found: %u\n", (unsigned)state.found);
    if (CONFIG.api_key[0] == '\0') {
        printf("No project token is set, so nothing was sent.\n");
        return;
    }
    printf("Sessions sent:  %u\n", (unsigned)state.sent);
    printf("Destination:    %s\n", POSTHOG_HOST);
    if (state.sent != state.found) {
        printf("The destination answered %ld. Run the app again to retry.\n", state.last_status);
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
    printf("\nPress A to play with Max. Press HOME to exit.\n");

    if (SKIP_PROMPT || wait_for_a_or_home()) {
        play();
    }
    exit(0);
}
