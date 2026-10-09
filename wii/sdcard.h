#ifndef SDCARD_H
#define SDCARD_H

#include "settings.h"

/* Where the settings file is, from the top of the SD card. */
#define SDCARD_SETTINGS_FILE "apps/posthog-wii/settings.txt"

/* Reads the settings file from the SD card. Without a card or a file, out
   keeps the values it had. */
void sdcard_read_settings(settings *out);

#endif
