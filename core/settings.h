#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

enum { SETTINGS_VALUE_CAP = 128 };

typedef struct {
    char host[SETTINGS_VALUE_CAP];    /* scheme, name and port, without a trailing slash */
    char api_key[SETTINGS_VALUE_CAP]; /* the PostHog project token */
    int utc_offset_minutes;           /* console local time minus UTC */
} settings;

/* Reads the settings file: one "name = value" per line, with the names host,
   api_key and utc_offset. Any other line changes nothing, and so does a value
   that is too long or an offset that settings_utc_offset rejects. A field
   that no line sets keeps the value it had. */
void settings_parse(settings *out, const char *text, size_t len);

/* Reads an offset written as a sign, two digits, a colon and two digits.
   Returns false and leaves minutes alone for any other text. */
bool settings_utc_offset(const char *text, size_t len, int *minutes);

#endif
