#include "playlog.h"

#include <stdbool.h>
#include <string.h>

#include "bytes.h"
#include "ticks.h"
#include "vff.h"

/* The System Menu keeps one Message Board file per day that it calls
   "playtimelog". The file is a CDBFILE header, then a board record whose
   first attachment holds up to ten sessions.

   Sources: https://wiibrew.org/wiki//title/00000001/00000002/data/play_rec.dat
   for the session fields, and the Riiwind parser
   (https://github.com/DeekyJay/riiwind) for the file around them. */
enum {
    LOG_FILE_CAP = 0x4000,

    CDBFILE_CONSOLE_ID = 0x08,
    CDBFILE_DESCRIPTION = 0x14,
    CDBFILE_SIZE = 0x400,

    RECORD_MAGIC = 0x52495F35, /* "RI_5" */
    RECORD_ATTACHMENT_TYPE = 0x128,
    RECORD_ATTACHMENT_OFFSET = 0x12C,
    RECORD_ATTACHMENT_SIZE = 0x130,
    ATTACHMENT_TYPE_PLAY_LOG = 3,

    LOG_MAGIC = 0x30335F30, /* "03_0" */
    LOG_HEADER_SIZE = 8,
    /* System menus before the sent-message log wrote the entries with no
       magic in front. The attachment size identifies that layout. */
    LOG_SIZE_WITHOUT_HEADER = 0x558,

    ENTRY_COUNT = 10,
    ENTRY_SIZE = 0x88,
    ENTRY_IN_USE = 0x00,
    ENTRY_TITLE = 0x0C,
    ENTRY_START_TICKS = 0x60,
    ENTRY_END_TICKS = 0x68,
    ENTRY_GAME_CODE = 0x70,
    ENTRY_MAKER_CODE = 0x74,

    TITLE_LINES = 2,
    TITLE_LINE_UNITS = 21,
    UTF8_MAX_PER_UNIT = 3,
};

/* A corrupt entry can hold any tick value. The century limit keeps every
   later time computation inside four-digit years. */
static const uint64_t MAX_TICKS = (uint64_t)TICKS_PER_SECOND * 3155760000u;

_Static_assert(TITLE_LINES * (TITLE_LINE_UNITS * UTF8_MAX_PER_UNIT + 1) <= PLAYLOG_TITLE_CAP,
               "a decoded title always fits");

typedef struct {
    playlog_session_fn *fn;
    void *ctx;
} sink;

static const char CDBFILE_MAGIC[] = "CDBFILE";
static const char LOG_DESCRIPTION[] = "playtimelog";

static bool has_bytes(bytes b, size_t offset, const char *expected, size_t len) {
    bytes found = bytes_at(b, offset, len);
    return found.len == len && memcmp(found.data, expected, len) == 0;
}

static bool is_printable(bytes b) {
    for (size_t i = 0; i < b.len; i++) {
        if (b.data[i] < 0x21 || b.data[i] > 0x7E) {
            return false;
        }
    }
    return b.len != 0;
}

static char *put_utf8(char *out, uint32_t code_point) {
    if (code_point < 0x80) {
        *out++ = (char)code_point;
    } else if (code_point < 0x800) {
        *out++ = (char)(0xC0 | code_point >> 6);
        *out++ = (char)(0x80 | (code_point & 0x3F));
    } else if (code_point < 0x10000) {
        *out++ = (char)(0xE0 | code_point >> 12);
        *out++ = (char)(0x80 | (code_point >> 6 & 0x3F));
        *out++ = (char)(0x80 | (code_point & 0x3F));
    } else {
        *out++ = (char)(0xF0 | code_point >> 18);
        *out++ = (char)(0x80 | (code_point >> 12 & 0x3F));
        *out++ = (char)(0x80 | (code_point >> 6 & 0x3F));
        *out++ = (char)(0x80 | (code_point & 0x3F));
    }
    return out;
}

static char *put_title_line(char *out, bytes line) {
    for (size_t i = 0; i < TITLE_LINE_UNITS; i++) {
        uint32_t unit = be16_at(line, i * 2);
        if (unit == 0) {
            break;
        }
        uint32_t low = i + 1 < TITLE_LINE_UNITS ? be16_at(line, (i + 1) * 2) : 0;
        if (unit >= 0xD800 && unit < 0xDC00 && low >= 0xDC00 && low < 0xE000) {
            unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
            i++;
        } else if (unit >= 0xD800 && unit < 0xE000) {
            unit = 0xFFFD;
        }
        out = put_utf8(out, unit);
    }
    return out;
}

static void decode_title(bytes entry, char title[PLAYLOG_TITLE_CAP]) {
    char *out = title;
    for (size_t line = 0; line < TITLE_LINES; line++) {
        char *line_start = out;
        if (line_start != title) {
            *out++ = ' ';
        }
        char *line_end = put_title_line(out, bytes_at(entry, ENTRY_TITLE + line * TITLE_LINE_UNITS * 2, TITLE_LINE_UNITS * 2));
        out = line_end == out ? line_start : line_end;
    }
    *out = '\0';
}

static void copy_code(char *out, bytes code) {
    if (is_printable(code)) {
        memcpy(out, code.data, code.len);
        out[code.len] = '\0';
    } else {
        out[0] = '\0';
    }
}

static bytes log_entries(bytes file) {
    /* The description match includes the terminator, so a longer
       description that starts with the same text does not match. */
    if (!has_bytes(file, 0, CDBFILE_MAGIC, sizeof CDBFILE_MAGIC - 1) ||
        !has_bytes(file, CDBFILE_DESCRIPTION, LOG_DESCRIPTION, sizeof LOG_DESCRIPTION) ||
        file.len <= CDBFILE_SIZE) {
        return (bytes){0};
    }
    bytes record = bytes_at(file, CDBFILE_SIZE, file.len - CDBFILE_SIZE);
    if (be32_at(record, 0) != RECORD_MAGIC || be32_at(record, RECORD_ATTACHMENT_TYPE) != ATTACHMENT_TYPE_PLAY_LOG) {
        return (bytes){0};
    }
    bytes log = bytes_at(record, be32_at(record, RECORD_ATTACHMENT_OFFSET), be32_at(record, RECORD_ATTACHMENT_SIZE));
    if (log.len == LOG_SIZE_WITHOUT_HEADER) {
        return bytes_at(log, 0, ENTRY_COUNT * ENTRY_SIZE);
    }
    if (be32_at(log, 0) != LOG_MAGIC) {
        return (bytes){0};
    }
    return bytes_at(log, LOG_HEADER_SIZE, ENTRY_COUNT * ENTRY_SIZE);
}

static void parse_file(void *ctx, const uint8_t *head, size_t head_len) {
    const sink *out = ctx;
    bytes file = {head, head_len};
    bytes entries = log_entries(file);

    for (size_t i = 0; i * ENTRY_SIZE < entries.len; i++) {
        bytes entry = bytes_at(entries, i * ENTRY_SIZE, ENTRY_SIZE);
        playlog_session session = {
            .console_id = be64_at(file, CDBFILE_CONSOLE_ID),
            .start_ticks = be64_at(entry, ENTRY_START_TICKS),
            .end_ticks = be64_at(entry, ENTRY_END_TICKS),
        };
        copy_code(session.game_code, bytes_at(entry, ENTRY_GAME_CODE, 4));
        copy_code(session.maker_code, bytes_at(entry, ENTRY_MAKER_CODE, 2));
        if (u8_at(entry, ENTRY_IN_USE) == 0 || session.game_code[0] == '\0' ||
            session.end_ticks < session.start_ticks || session.end_ticks >= MAX_TICKS) {
            continue;
        }
        decode_title(entry, session.title);
        out->fn(out->ctx, &session);
    }
}

void playlog_each_session(const uint8_t *image, size_t image_len,
                          playlog_session_fn *fn, void *ctx) {
    uint8_t scratch[LOG_FILE_CAP];
    sink out = {fn, ctx};
    vff_each_file(image, image_len, scratch, sizeof scratch, parse_file, &out);
}
