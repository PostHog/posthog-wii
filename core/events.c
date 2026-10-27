#include "events.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bytes.h"
#include "playlog.h"
#include "sha1.h"
#include "ticks.h"

enum {
    UUID_SIZE = 16,
    UUID_TIME_SIZE = 6,
    LABEL_CAP = 16,
    DISTINCT_ID_BYTES = 6,
    FORMATTED_CAP = 64,

    /* RFC 9562, section 4.1 and 4.2. */
    UUID_VERSION_BYTE = 6,
    UUID_VARIANT_BYTE = 8,
    UUID_VERSION_NAME_SHA1 = 5,
    UUID_VERSION_TIME_ORDERED = 7,

    /* The fields of a session that its digest covers, in order. */
    DIGEST_CONSOLE_ID = UUID_SIZE,
    DIGEST_GAME_CODE = DIGEST_CONSOLE_ID + sizeof(uint64_t),
    DIGEST_START_TICKS = DIGEST_GAME_CODE + PLAYLOG_GAME_CODE_LEN,
    DIGEST_LABEL = DIGEST_START_TICKS + sizeof(uint64_t),
    DIGEST_NAME_SIZE = DIGEST_LABEL + LABEL_CAP,
};

static const unsigned NIBBLE_BITS = 4;
static const unsigned NIBBLE_MASK = 0x0F;
static const unsigned UUID_VARIANT_MASK = 0x3F;
static const unsigned UUID_VARIANT_RFC = 0x80;

/* The byte counts of the hex groups that hyphens separate. */
static const uint8_t UUID_GROUP_SIZES[] = {4, 2, 2, 2, 6};

/* Changing this value changes every event UUID, so a resend after the
   change duplicates events that are already in PostHog. */
static const uint8_t UUID_NAMESPACE[UUID_SIZE] = {
    0x08, 0x3e, 0x7d, 0x78, 0xb1, 0x1f, 0x4c, 0xad, 0xa1, 0xfc, 0x96, 0x9a, 0xd6, 0xd0, 0x21, 0xce,
};

typedef struct {
    char *out;
    size_t cap;
    size_t len; /* counts past cap, which marks an overflow */
} text_builder;

static void put_char(text_builder *body, char character) {
    if (body->len < body->cap) {
        body->out[body->len] = character;
    }
    body->len++;
}

static void put(text_builder *body, const char *text) {
    for (; *text; text++) {
        put_char(body, *text);
    }
}

__attribute__((format(printf, 2, 3))) static void putf(text_builder *body, const char *format, ...) {
    char text[FORMATTED_CAP];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(text, sizeof text, format, args);
    va_end(args);
    put(body, text);
}

static void put_json_string(text_builder *body, const char *text) {
    put_char(body, '"');
    for (const unsigned char *byte = (const unsigned char *)text; *byte; byte++) {
        if (*byte == '"' || *byte == '\\') {
            put_char(body, '\\');
            put_char(body, (char)*byte);
        } else if (*byte < ' ') {
            putf(body, "\\u%04x", (unsigned)*byte);
        } else {
            put_char(body, (char)*byte);
        }
    }
    put_char(body, '"');
}

static void put_hex(text_builder *body, const uint8_t *data, size_t len) {
    static const char DIGITS[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        put_char(body, DIGITS[data[i] >> NIBBLE_BITS]);
        put_char(body, DIGITS[data[i] & NIBBLE_MASK]);
    }
}

/* The digest covers only fields read from the console, so the same session
   gets the same digest whatever UTC offset the user configures. */
static void session_digest(const playlog_session *session, const char *label, uint8_t digest[SHA1_SIZE]) {
    uint8_t name[DIGEST_NAME_SIZE] = {0};
    size_t label_len = strlen(label);
    memcpy(name, UUID_NAMESPACE, UUID_SIZE);
    put_be(name + DIGEST_CONSOLE_ID, session->console_id, sizeof session->console_id);
    memcpy(name + DIGEST_GAME_CODE, session->game_code, PLAYLOG_GAME_CODE_LEN);
    put_be(name + DIGEST_START_TICKS, session->start_ticks, sizeof session->start_ticks);
    memcpy(name + DIGEST_LABEL, label, label_len < LABEL_CAP ? label_len : LABEL_CAP);
    sha1(name, sizeof name, digest);
}

static void put_uuid(text_builder *body, uint8_t uuid[UUID_SIZE], unsigned version) {
    uuid[UUID_VERSION_BYTE] = (uint8_t)((uuid[UUID_VERSION_BYTE] & NIBBLE_MASK) | version << NIBBLE_BITS);
    uuid[UUID_VARIANT_BYTE] = (uint8_t)((uuid[UUID_VARIANT_BYTE] & UUID_VARIANT_MASK) | UUID_VARIANT_RFC);
    put_char(body, '"');
    size_t offset = 0;
    for (size_t group = 0; group < sizeof UUID_GROUP_SIZES; group++) {
        if (group > 0) {
            put_char(body, '-');
        }
        put_hex(body, uuid + offset, UUID_GROUP_SIZES[group]);
        offset += UUID_GROUP_SIZES[group];
    }
    put_char(body, '"');
}

static void put_event_uuid(text_builder *body, const playlog_session *session, const char *label) {
    uint8_t digest[SHA1_SIZE];
    session_digest(session, label, digest);
    put_uuid(body, digest, UUID_VERSION_NAME_SHA1);
}

/* PostHog builds a session only from a UUIDv7 session ID, and its date
   filters drop a session whose embedded time is more than three days from
   its events. The ID embeds the session start in console local time. That
   is within a day of the event time, and it does not change with the
   configured UTC offset. */
static void put_session_id(text_builder *body, const playlog_session *session) {
    uint8_t digest[SHA1_SIZE];
    session_digest(session, "session", digest);
    uint8_t uuid[UUID_SIZE];
    put_be(uuid, (uint64_t)ticks_to_unix_ms(session->start_ticks, 0), UUID_TIME_SIZE);
    memcpy(uuid + UUID_TIME_SIZE, digest, UUID_SIZE - UUID_TIME_SIZE);
    put_uuid(body, uuid, UUID_VERSION_TIME_ORDERED);
}

static void put_distinct_id(text_builder *body, uint64_t console_id) {
    uint8_t name[UUID_SIZE + sizeof console_id];
    memcpy(name, UUID_NAMESPACE, UUID_SIZE);
    put_be(name + UUID_SIZE, console_id, sizeof console_id);
    uint8_t digest[SHA1_SIZE];
    sha1(name, sizeof name, digest);
    put(body, "\"wii-");
    put_hex(body, digest, DISTINCT_ID_BYTES);
    put_char(body, '"');
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

static void put_event(text_builder *body, const events_config *config, const playlog_session *session, bool ended) {
    uint64_t ticks = ended ? session->end_ticks : session->start_ticks;
    char timestamp[ISO_TIME_CAP];
    unix_ms_to_iso(ticks_to_unix_ms(ticks, config->utc_offset_minutes), timestamp);

    put(body, ended ? "{\"event\":\"game ended\"" : "{\"event\":\"game started\"");
    put(body, ",\"distinct_id\":");
    put_distinct_id(body, session->console_id);
    put(body, ",\"timestamp\":");
    put_json_string(body, timestamp);
    put(body, ",\"uuid\":");
    put_event_uuid(body, session, ended ? "ended" : "started");
    put(body, ",\"properties\":{\"$session_id\":");
    put_session_id(body, session);
    put(body, ",\"$lib\":\"posthog-wii\",\"$geoip_disable\":true,\"game_code\":");
    put_json_string(body, session->game_code);
    put(body, ",\"maker_code\":");
    put_json_string(body, session->maker_code);
    put(body, ",\"game_title\":");
    put_json_string(body, session->title);
    put(body, ",\"title_type\":");
    put_json_string(body, title_type(session->game_code[0]));
    if (ended) {
        putf(body, ",\"duration_seconds\":%llu",
             (unsigned long long)ticks_to_seconds(session->end_ticks - session->start_ticks));
    }
    put(body, "}}");
}

size_t events_batch_json(const events_config *config, const playlog_session *sessions, size_t count, char *out,
                         size_t out_cap) {
    text_builder body = {out, out_cap, 0};
    put(&body, "{\"api_key\":");
    put_json_string(&body, config->api_key);
    /* The body has no sent_at field. PostHog shifts every timestamp by the
       gap between sent_at and its own clock, which would move old sessions. */
    put(&body, ",\"historical_migration\":true,\"batch\":[");
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            put_char(&body, ',');
        }
        put_event(&body, config, &sessions[i], false);
        put_char(&body, ',');
        put_event(&body, config, &sessions[i], true);
    }
    put(&body, "]}");
    if (body.len >= body.cap) {
        return 0;
    }
    out[body.len] = '\0';
    return body.len;
}
