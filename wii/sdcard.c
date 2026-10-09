#include "sdcard.h"

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

static FILE *open_file(const char *file, const char *mode) {
    char path[FILENAME_MAX];
    snprintf(path, sizeof path, "sd:/%s", file);
    return fopen(path, mode);
}

size_t sdcard_read(const char *file, void *out, size_t cap) {
    if (!mount(false)) {
        return 0;
    }
    size_t len = 0;
    FILE *stream = open_file(file, "rb");
    if (stream) {
        len = fread(out, 1, cap, stream);
        fclose(stream);
    }
    unmount();
    return len;
}

bool sdcard_write(const char *file, const void *data, size_t len) {
    if (!mount(true)) {
        return false;
    }
    bool written = false;
    FILE *stream = open_file(file, "wb");
    if (stream) {
        written = fwrite(data, 1, len, stream) == len;
        /* The close sends the last bytes to the card, so it can fail too. */
        written = fclose(stream) == 0 && written;
    }
    unmount();
    return written;
}
