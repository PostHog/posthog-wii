#include "sdcard.h"

#include <stdbool.h>
#include <stdio.h>

#include <fat.h>
#include <gccore.h>
#include <sdcard/wiisd_io.h>

static bool mount(bool for_writing) {
    /* libfat mounts a card that reports no write support as read-only, so a
       power cut during a read cannot damage the card. */
    static DISC_INTERFACE card;
    card = __io_wiisd;
    if (!for_writing) {
        card.features &= ~(u32)FEATURE_MEDIUM_CANWRITE;
    }
    return fatMountSimple("sd", &card);
}

static void unmount(void) {
    /* newlib finds a device by name only when the name ends in a colon.
       Without one it returns the default device, whatever that is. */
    fatUnmount("sd:");
}

size_t sdcard_read(const char *file, char *out, size_t cap) {
    if (!mount(false)) {
        return 0;
    }
    size_t len = 0;
    char path[FILENAME_MAX];
    snprintf(path, sizeof path, "sd:/%s", file);
    FILE *stream = fopen(path, "rb");
    if (stream) {
        len = fread(out, 1, cap, stream);
        fclose(stream);
    }
    unmount();
    return len;
}

void sdcard_write_cursor(const char *text, size_t len) {
    if (!mount(true)) {
        return;
    }
    FILE *stream = fopen("sd:/" SDCARD_CURSOR_FILE, "wb");
    if (stream) {
        fwrite(text, 1, len, stream);
        fclose(stream);
    }
    unmount();
}
