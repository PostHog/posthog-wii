#include "sdcard.h"

#include <stdio.h>

#include <fat.h>
#include <gccore.h>
#include <sdcard/wiisd_io.h>

enum { SETTINGS_CAP = 1024 };

/* Reads up to cap bytes of a file on the card. Returns 0 without a card or a file. */
static size_t read_file(const char *path, char *out, size_t cap) {
    /* libfat mounts a card that reports no write support as read-only, so a
       power cut during a run cannot damage the card. */
    static DISC_INTERFACE card;
    card = __io_wiisd;
    card.features &= ~(u32)FEATURE_MEDIUM_CANWRITE;
    if (!fatMountSimple("sd", &card)) {
        return 0;
    }
    size_t len = 0;
    FILE *file = fopen(path, "rb");
    if (file) {
        len = fread(out, 1, cap, file);
        fclose(file);
    }
    fatUnmount("sd");
    return len;
}

void sdcard_read_settings(settings *out) {
    static char text[SETTINGS_CAP];
    settings_parse(out, text, read_file("sd:/" SDCARD_SETTINGS_FILE, text, sizeof text));
}

size_t sdcard_read_roots(char *out, size_t cap) {
    return read_file("sd:/" SDCARD_ROOTS_FILE, out, cap);
}
