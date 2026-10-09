#ifndef SDCARD_H
#define SDCARD_H

#include <stddef.h>

/* Where the files are, from the top of the SD card. */
#define SDCARD_SETTINGS_FILE "apps/posthog-wii/settings.txt"
#define SDCARD_ROOTS_FILE "apps/posthog-wii/roots.pem"
#define SDCARD_CURSOR_FILE "apps/posthog-wii/sent.txt"

/* Reads up to cap bytes of a file. Returns the number of bytes read, which
   is 0 without a card or a file. */
size_t sdcard_read(const char *file, char *out, size_t cap);

/* Replaces SDCARD_CURSOR_FILE. It is the one file the app writes. Without
   a card that takes writes, nothing happens. */
void sdcard_write_cursor(const char *text, size_t len);

#endif
