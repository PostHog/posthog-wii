#include "events.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "sha1.h"
#include "ticks.h"

enum { UUID_SIZE = 16, LABEL_CAP = 16, DISTINCT_ID_BYTES = 6 };

/* Changing this value changes every event UUID, so a resend after the
   change duplicates events that are already in PostHog. */
static const uint8_t UUID_NAMESPACE[UUID_SIZE] = {0x08, 0x3e, 0x7d, 0x78, 0xb1, 0x1f, 0x4c, 0xad,
                                                 0xa1, 0xfc, 0x96, 0x9a, 0xd6, 0xd0, 0x21, 0xce};

typedef struct {
    char *out;
    size_t cap;
    size_t len; /* counts past cap, which marks an overflow */
} builder;

static void put_char(builder *b, char c) {
    if (b->len < b->cap) {
        b->out[b->len] = c;
    }
    b->len++;
}

static void put(builder *b, const char *text) {
    for (; *text; text++) {
        put_char(b, *text);
    }
}

__attribute__((format(printf, 2, 3))) static void putf(builder *b, const char *format, ...) {
    char text[64];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    put(b, text);
}

static void put_json_string(builder *b, const char *text) {
    put_char(b, '"');
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\') {
            put_char(b, '\\');
            put_char(b, (char)*p);
        } else if (*p < 0x20) {
            putf(b, "\\u%04x", (unsigned)*p);
        } else {
            put_char(b, (char)*p);
        }
    }
    put_char(b, '"');
}

static void put_be64(uint8_t *out, uint64_t value) {
    for (int i = 0; i < 8; i++) {
        out[i] = (uint8_t)(value >> (56 - 8 * i));
    }
}

/* The digest covers only fields read from the console, so the same session
   gets the same IDs whatever UTC offset the user configures. */
static void session_digest(const playlog_session *session, const char *label, uint8_t digest[SHA1_SIZE]) {
    uint8_t name[UUID_SIZE + 8 + 4 + 8 + LABEL_CAP] = {0};
    size_t label_len = strlen(label);
    memcpy(name, UUID_NAMESPACE, UUID_SIZE);
    put_be64(name + UUID_SIZE, session->console_id);
    memcpy(name + UUID_SIZE + 8, session->game_code, 4);
    put_be64(name + UUID_SIZE + 12, session->start_ticks);
    memcpy(name + UUID_SIZE + 20, label, label_len < LABEL_CAP ? label_len : LABEL_CAP);
    sha1(name, sizeof name, digest);
}

static void put_uuid(builder *b, const uint8_t uuid[UUID_SIZE]) {
    put_char(b, '"');
    for (int i = 0; i < UUID_SIZE; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            put_char(b, '-');
        }
        putf(b, "%02x", (unsigned)uuid[i]);
    }
    put_char(b, '"');
}

static void put_event_uuid(builder *b, const playlog_session *session, const char *label) {
    uint8_t digest[SHA1_SIZE];
    session_digest(session, label, digest);
    digest[6] = (uint8_t)((digest[6] & 0x0F) | 0x50);
    digest[8] = (uint8_t)((digest[8] & 0x3F) | 0x80);
    put_uuid(b, digest);
}

/* PostHog builds sessions only from UUIDv7 session IDs, and its date
   filters prune a session whose embedded time is far from its events. The
   ID therefore carries the session start time. */
static void put_session_id(builder *b, const playlog_session *session, int64_t start_ms) {
    uint8_t digest[SHA1_SIZE];
    session_digest(session, "session", digest);
    uint8_t uuid[UUID_SIZE];
    for (int i = 0; i < 6; i++) {
        uuid[i] = (uint8_t)(start_ms >> (40 - 8 * i));
    }
    memcpy(uuid + 6, digest, UUID_SIZE - 6);
    uuid[6] = (uint8_t)((uuid[6] & 0x0F) | 0x70);
    uuid[8] = (uint8_t)((uuid[8] & 0x3F) | 0x80);
    put_uuid(b, uuid);
}

static void put_distinct_id(builder *b, uint64_t console_id) {
    uint8_t name[UUID_SIZE + 8];
    memcpy(name, UUID_NAMESPACE, UUID_SIZE);
    put_be64(name + UUID_SIZE, console_id);
    uint8_t digest[SHA1_SIZE];
    sha1(name, sizeof name, digest);
    put(b, "\"wii-");
    for (int i = 0; i < DISTINCT_ID_BYTES; i++) {
        putf(b, "%02x", (unsigned)digest[i]);
    }
    put_char(b, '"');
}

/* The first letter of a game code is its system code:
   https://wiibrew.org/wiki/Title_database */
static const char *title_type(char system_code) {
    switch (system_code) {
    case 'R':
    case 'S':
    case 'D':
        return "disc";
    case 'G':
        return "gamecube";
    case 'H':
        return "channel";
    case 'W':
        return "wiiware";
    case 'C':
    case 'E':
    case 'F':
    case 'J':
    case 'L':
    case 'M':
    case 'N':
    case 'P':
    case 'Q':
    case 'X':
        return "virtual_console";
    default:
        return "other";
    }
}

static void put_event(builder *b, const events_config *config, const playlog_session *session, bool ended) {
    int64_t start_ms = ticks_to_unix_ms(session->start_ticks, config->utc_offset_minutes);
    int64_t end_ms = ticks_to_unix_ms(session->end_ticks, config->utc_offset_minutes);
    char timestamp[ISO_TIME_CAP];
    unix_ms_to_iso(ended ? end_ms : start_ms, timestamp);

    put(b, ended ? "{\"event\":\"game ended\"" : "{\"event\":\"game started\"");
    put(b, ",\"distinct_id\":");
    put_distinct_id(b, session->console_id);
    put(b, ",\"timestamp\":");
    put_json_string(b, timestamp);
    put(b, ",\"uuid\":");
    put_event_uuid(b, session, ended ? "ended" : "started");
    put(b, ",\"properties\":{\"$session_id\":");
    put_session_id(b, session, start_ms);
    put(b, ",\"$lib\":\"posthog-wii\",\"$geoip_disable\":true,\"game_code\":");
    put_json_string(b, session->game_code);
    put(b, ",\"maker_code\":");
    put_json_string(b, session->maker_code);
    put(b, ",\"game_title\":");
    put_json_string(b, session->title);
    put(b, ",\"title_type\":");
    put_json_string(b, title_type(session->game_code[0]));
    if (ended) {
        putf(b, ",\"duration_seconds\":%llu",
             (unsigned long long)ticks_to_seconds(session->end_ticks - session->start_ticks));
    }
    put(b, "}}");
}

size_t events_batch_json(const events_config *config,
                         const playlog_session *sessions, size_t count,
                         char *out, size_t out_cap) {
    builder b = {out, out_cap, 0};
    put(&b, "{\"api_key\":");
    put_json_string(&b, config->api_key);
    /* No "sent_at" field: PostHog shifts every timestamp by the gap between
       sent_at and its own clock, which would move old sessions. */
    put(&b, ",\"historical_migration\":true,\"batch\":[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            put_char(&b, ',');
        }
        put_event(&b, config, &sessions[i], false);
        put_char(&b, ',');
        put_event(&b, config, &sessions[i], true);
    }
    put(&b, "]}");
    if (b.len >= b.cap) {
        return 0;
    }
    out[b.len] = '\0';
    return b.len;
}
