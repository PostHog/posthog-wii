#ifndef SDCARD_H
#define SDCARD_H

#include <stddef.h>

#include "settings.h"

/* Where the files are, from the top of the SD card. */
#define SDCARD_SETTINGS_FILE "apps/posthog-wii/settings.txt"
#define SDCARD_ROOTS_FILE "apps/posthog-wii/roots.pem"

/* Reads the settings file from the SD card. Without a card or a file, out
   keeps the values it had. */
void sdcard_read_settings(settings *out);

/* Reads the owner's extra root certificates, as PEM text, into out. Returns
   the number of bytes read, which is 0 without a card or a file. */
size_t sdcard_read_roots(char *out, size_t cap);

#endif
