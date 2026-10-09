#include "settings.h"

#include <string.h>

typedef struct {
    const char *start;
    const char *end;
} span;

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* Text editors add a byte order mark, carriage returns and spaces. */
static bool is_padding(char c) {
    return (unsigned char)c <= ' ' || (unsigned char)c >= 0x7F;
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
    return len == strlen(expected) && memcmp(name.start, expected, len) == 0;
}

static void copy(char out[SETTINGS_VALUE_CAP], span value) {
    size_t len = (size_t)(value.end - value.start);
    if (len < SETTINGS_VALUE_CAP) {
        memcpy(out, value.start, len);
        out[len] = '\0';
    }
}

bool settings_utc_offset(const char *text, size_t len, int *minutes) {
    if (len != 6 || (text[0] != '+' && text[0] != '-') || text[3] != ':' || !is_digit(text[1]) || !is_digit(text[2]) ||
        !is_digit(text[4]) || !is_digit(text[5])) {
        return false;
    }
    int hours = (text[1] - '0') * 10 + (text[2] - '0');
    int mins = (text[4] - '0') * 10 + (text[5] - '0');
    if (hours > 14 || mins > 59) {
        return false;
    }
    *minutes = (text[0] == '-' ? -1 : 1) * (hours * 60 + mins);
    return true;
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
            } else if (is_named(name, "utc_offset")) {
                settings_utc_offset(value.start, (size_t)(value.end - value.start), &out->utc_offset_minutes);
            }
        }
        line = line_end < end ? line_end + 1 : end;
    }
}
