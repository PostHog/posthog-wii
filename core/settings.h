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

/* Reads "name = value" lines for host, api_key and utc_offset. A line it
   cannot use changes nothing, so the field keeps the value it had. */
void settings_parse(settings *out, const char *text, size_t len);

/* Reads an offset such as -04:00. Returns false, and leaves minutes alone,
   for any other form. */
bool settings_utc_offset(const char *text, size_t len, int *minutes);

#endif
