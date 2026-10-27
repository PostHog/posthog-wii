#include "settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* A UTC offset is written "+HH:MM" or "-HH:MM". */
enum {
    OFFSET_TEXT_LEN = 6,
    OFFSET_SIGN = 0,
    OFFSET_HOURS = 1,
    OFFSET_COLON = 3,
    OFFSET_MINUTES = 4,
    MAX_OFFSET_HOURS = 14,
};

enum { MINUTES_PER_HOUR = 60, DECIMAL_BASE = 10, ASCII_DELETE = 0x7F };

typedef struct {
    const char *start;
    const char *end;
} span;

static bool is_digit(char character) {
    return (character >= '0' && character <= '9') != 0;
}

/* Text editors add a byte order mark, carriage returns and spaces. */
static bool is_padding(char character) {
    return ((unsigned char)character <= ' ' || (unsigned char)character >= ASCII_DELETE) != 0;
}

static bool is_two_digits(const char *text) {
    return (is_digit(text[0]) && is_digit(text[1])) != 0;
}

static int two_digit_value(const char *text) {
    return ((text[0] - '0') * DECIMAL_BASE) + (text[1] - '0');
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

bool settings_utc_offset(const char *text, size_t len, int *minutes) {
    if (len != OFFSET_TEXT_LEN || (text[OFFSET_SIGN] != '+' && text[OFFSET_SIGN] != '-') || text[OFFSET_COLON] != ':' ||
        !is_two_digits(text + OFFSET_HOURS) || !is_two_digits(text + OFFSET_MINUTES)) {
        return false;
    }
    int hours = two_digit_value(text + OFFSET_HOURS);
    int mins = two_digit_value(text + OFFSET_MINUTES);
    if (hours > MAX_OFFSET_HOURS || mins >= MINUTES_PER_HOUR) {
        return false;
    }
    *minutes = (text[OFFSET_SIGN] == '-' ? -1 : 1) * ((hours * MINUTES_PER_HOUR) + mins);
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
