#include "settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum { ASCII_DELETE = 0x7F };

typedef struct {
    const char *start;
    const char *end;
} span;

/* Text editors add a byte order mark, carriage returns and spaces. */
static bool is_padding(char character) {
    return ((unsigned char)character <= ' ' || (unsigned char)character >= ASCII_DELETE) != 0;
}

static span trimmed(const char *start, const char *end) {
    while (start < end && is_padding(*start)) {
        start++;
    }
    while (end > start && is_padding(end[-1])) {
        end--;
    }
    return (span){start, end};
}

static bool is_named(span name, const char *expected) {
    size_t len = (size_t)(name.end - name.start);
    return (len == strlen(expected) && memcmp(name.start, expected, len) == 0) != 0;
}

static void copy(char out[SETTINGS_VALUE_CAP], span value) {
    size_t len = (size_t)(value.end - value.start);
    if (len < SETTINGS_VALUE_CAP) {
        memcpy(out, value.start, len);
        out[len] = '\0';
    }
}

void settings_parse(settings *out, const char *text, size_t len) {
    const char *end = text + len;
    const char *line = text;
    while (line < end) {
        const char *line_end = memchr(line, '\n', (size_t)(end - line));
        if (!line_end) {
            line_end = end;
        }
        const char *equals = memchr(line, '=', (size_t)(line_end - line));
        if (equals) {
            span name = trimmed(line, equals);
            span value = trimmed(equals + 1, line_end);
            if (is_named(name, "host")) {
                while (value.end > value.start && value.end[-1] == '/') {
                    value.end--;
                }
                copy(out->host, value);
            } else if (is_named(name, "api_key")) {
                copy(out->api_key, value);
            }
        }
        line = line_end < end ? line_end + 1 : end;
    }
}
