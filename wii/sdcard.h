#ifndef SDCARD_H
#define SDCARD_H

#include <stdbool.h>
#include <stddef.h>

/* Where the files are, from the top of the SD card. */
#define SDCARD_SETTINGS_FILE "apps/posthog-wii/settings.txt"
#define SDCARD_ROOTS_FILE "apps/posthog-wii/roots.pem"
#define SDCARD_CURSOR_FILE "apps/posthog-wii/sent.txt"
#define SDCARD_SEED_FILE "apps/posthog-wii/seed.bin"

/* Reads up to cap bytes of a file. Returns the number of bytes read, which
   is 0 without a card or a file. */
size_t sdcard_read(const char *file, void *out, size_t cap);

/* Replaces a file. Returns true when every byte reached the card. The app
   writes SDCARD_CURSOR_FILE and SDCARD_SEED_FILE and no other file. */
bool sdcard_write(const char *file, const void *data, size_t len);

#endif
