#ifndef SETTINGS_H
#define SETTINGS_H

#include <stddef.h>

enum { SETTINGS_VALUE_CAP = 128 };

typedef struct {
    char host[SETTINGS_VALUE_CAP];    /* scheme, name and port, without a trailing slash */
    char api_key[SETTINGS_VALUE_CAP]; /* the PostHog project token */
} settings;

/* Reads "name = value" lines for host and api_key. A line it cannot use
   changes nothing, so the field keeps the value it had. */
void settings_parse(settings *out, const char *text, size_t len);

#endif
