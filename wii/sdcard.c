#include "sdcard.h"

#include <stdio.h>

#include <fat.h>
#include <gccore.h>
#include <sdcard/wiisd_io.h>

enum { FILE_CAP = 1024 };

void sdcard_read_settings(settings *out) {
    /* libfat mounts a card that reports no write support as read-only, so a
       power cut during a run cannot damage the card. */
    static DISC_INTERFACE card;
    card = __io_wiisd;
    card.features &= ~(u32)FEATURE_MEDIUM_CANWRITE;
    if (!fatMountSimple("sd", &card)) {
        return;
    }
    FILE *file = fopen("sd:/" SDCARD_SETTINGS_FILE, "rb");
    if (file) {
        static char text[FILE_CAP];
        settings_parse(out, text, fread(text, 1, sizeof text, file));
        fclose(file);
    }
    fatUnmount("sd");
}
