#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytes.h"
#include "check.h"
#include "cursor.h"
#include "events.h"
#include "http_request.h"
#include "playlog.h"
#include "settings.h"
#include "sha1.h"
#include "summary.h"
#include "ticks.h"
#include "upload.h"
#include "vff.h"

enum {
    VFF_HEADER_BYTE_ORDER = 0x04,
    VFF_HEADER_VERSION = 0x06,
    VFF_HEADER_VOLUME_SIZE = 0x08,
    VFF_HEADER_CLUSTER_SIZE = 0x0C,
    VFF_HEADER_SIZE = 0x20,
    VFF_BYTE_ORDER = 0xFEFF,
    VFF_VERSION = 0x0100,
    VFF_CLUSTER_SIZE_UNIT = 16,

    FAT_ENTRY_SIZE = 2,
    FAT_CHAIN_END = 0xFFFF,
    FIRST_DATA_CLUSTER = 2,
    ROOT_ENTRIES = 128,

    DIRENT_SIZE = 0x20,
    DIRENT_NAME_SIZE = 11,
    DIRENT_ATTRIBUTES = 11,
    DIRENT_FIRST_CLUSTER = 26,
    DIRENT_FILE_SIZE = 28,
    DIRENT_DELETED = 0xE5,
    ATTR_NONE = 0x00,
    ATTR_VOLUME = 0x08,
    ATTR_LONG_NAME = 0x0F,
    ATTR_DIRECTORY = 0x10,

    CDBFILE_CONSOLE_ID = 0x08,
    CDBFILE_DESCRIPTION = 0x14,
    CDBFILE_SIZE = 0x400,
    RECORD_MAGIC = 0x52495F35,
    RECORD_ATTACHMENT_TYPE = 0x128,
    RECORD_ATTACHMENT_OFFSET = 0x12C,
    RECORD_ATTACHMENT_SIZE = 0x130,
    ATTACHMENT_TYPE_PLAY_LOG = 3,
    LOG_MAGIC = 0x30335F30,
    LOG_HEADER_SIZE = 8,

    ENTRY_SIZE = 0x88,
    ENTRY_IN_USE = 0x00,
    ENTRY_TITLE = 0x0C,
    ENTRY_START_TICKS = 0x60,
    ENTRY_END_TICKS = 0x68,
    ENTRY_GAME_CODE = 0x70,
    ENTRY_MAKER_CODE = 0x74,
    TITLE_LINES = 2,
    TITLE_LINE_SIZE = 42,
    UTF16_UNIT_SIZE = 2,
};

enum {
    IMAGE_SIZE = 0x280000,
    CLUSTER_SIZE = 0x200,
    FAT_OFFSET = VFF_HEADER_SIZE,
    FAT_SIZE = IMAGE_SIZE / CLUSTER_SIZE * FAT_ENTRY_SIZE,
    ROOT_OFFSET = FAT_OFFSET + (2 * FAT_SIZE),
    DATA_OFFSET = ROOT_OFFSET + (ROOT_ENTRIES * DIRENT_SIZE),
    ENTRIES_PER_CLUSTER = CLUSTER_SIZE / DIRENT_SIZE,

    ATTACHMENT_OFFSET = 0x220,
    ATTACHMENT_SIZE = 0x668,
    ATTACHMENT_PAST_THE_FILE = 0xFFFFFF,
    LOG_SIZE = CDBFILE_SIZE + ATTACHMENT_OFFSET + ATTACHMENT_SIZE,
    LOG_CLUSTERS = 7,

    YEAR_DIR_HEAD = 2,
    YEAR_DIR_TAIL = 9,
    LOG_DIR = 5,
    COLLECT_CAP = 16,
    JSON_VALUE_CAP = 64,

    /* 2022-11-15 21:44:24 console local time. */
    START_SECONDS = 721863864,
    UNIX_SECONDS_AT_2000 = 946684800,
    MS_PER_SECOND = 1000,
    UUID_TIME_LOW_RANGE = 0x10000,
    SECONDS_PER_HOUR = 3600,
    HTTP_SERVER_ERROR = 500,
};

static const uint16_t LOG_FILE[LOG_CLUSTERS] = {20, 30, 21, 40, 22, 41, 23};
static const uint16_t MAIL_FILE[LOG_CLUSTERS] = {50, 51, 52, 53, 54, 55, 56};

static const uint8_t VFF_MAGIC[] = {'V', 'F', 'F', ' '};
static const uint8_t CDBFILE_MAGIC[] = {'C', 'D', 'B', 'F', 'I', 'L', 'E', 2};
static const char LOG_DESCRIPTION[] = "playtimelog";
static const char MAIL_DESCRIPTION[] = "mail";
static const uint8_t MAKER_CODE[] = {'0', '1'};

static const uint64_t CONSOLE_ID = 0x0123456789ABCDEFULL;
static const uint64_t START = (uint64_t)TICKS_PER_SECOND * START_SECONDS;
static const uint64_t SECOND = TICKS_PER_SECOND;

static const uint16_t MARIO_KART[] = {'M', 'a', 'r', 'i', 'o', ' ', 'K', 'a', 'r', 't', ' ', 'W', 'i', 'i', 0};
static const uint16_t SUMABURA[] = {0x30B9, 0x30DE, 0x30D6, 0x30E9, 0};
static const uint16_t LETTER_X[] = {'X', 0};

typedef struct {
    const char *code;
    const uint16_t *line1;
    const uint16_t *line2;
    uint64_t start;
    uint64_t end;
    bool in_use;
} entry_spec;

typedef struct {
    playlog_session items[COLLECT_CAP];
    int count;
    bool read_whole;
} collected;

static void collect(void *ctx, const playlog_session *session) {
    collected *found = ctx;
    if (found->count < COLLECT_CAP) {
        found->items[found->count] = *session;
    }
    found->count++;
}

static void put_le16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> CHAR_BIT);
}

static void put_le32(uint8_t *out, uint32_t value) {
    put_le16(out, (uint16_t)value);
    put_le16(out + sizeof(uint16_t), (uint16_t)(value >> (CHAR_BIT * sizeof(uint16_t))));
}

static void put_entry(uint8_t *entry, entry_spec spec) {
    const uint16_t *lines[TITLE_LINES] = {spec.line1, spec.line2};
    entry[ENTRY_IN_USE] = (uint8_t)spec.in_use;
    for (size_t line = 0; line < TITLE_LINES; line++) {
        uint8_t *title_line = entry + ENTRY_TITLE + (line * TITLE_LINE_SIZE);
        for (size_t unit = 0; lines[line] && lines[line][unit]; unit++) {
            put_be(title_line + (unit * UTF16_UNIT_SIZE), lines[line][unit], UTF16_UNIT_SIZE);
        }
    }
    put_be(entry + ENTRY_START_TICKS, spec.start, sizeof spec.start);
    put_be(entry + ENTRY_END_TICKS, spec.end, sizeof spec.end);
    memcpy(entry + ENTRY_GAME_CODE, spec.code, 4);
    memcpy(entry + ENTRY_MAKER_CODE, MAKER_CODE, sizeof MAKER_CODE);
}

static void build_log(uint8_t log[LOG_SIZE], const entry_spec *entries, size_t count) {
    memset(log, 0, LOG_SIZE);
    memcpy(log, CDBFILE_MAGIC, sizeof CDBFILE_MAGIC);
    put_be(log + CDBFILE_CONSOLE_ID, CONSOLE_ID, sizeof CONSOLE_ID);
    memcpy(log + CDBFILE_DESCRIPTION, LOG_DESCRIPTION, sizeof LOG_DESCRIPTION);

    uint8_t *record = log + CDBFILE_SIZE;
    put_be(record, RECORD_MAGIC, 4);
    put_be(record + RECORD_ATTACHMENT_TYPE, ATTACHMENT_TYPE_PLAY_LOG, 4);
    put_be(record + RECORD_ATTACHMENT_OFFSET, ATTACHMENT_OFFSET, 4);
    put_be(record + RECORD_ATTACHMENT_SIZE, ATTACHMENT_SIZE, 4);

    uint8_t *attachment = record + ATTACHMENT_OFFSET;
    put_be(attachment, LOG_MAGIC, 4);
    attachment += LOG_HEADER_SIZE;
    for (size_t i = 0; i < count; i++) {
        put_entry(attachment + (i * ENTRY_SIZE), entries[i]);
    }
}

static uint8_t *cluster(uint8_t *image, uint16_t number) {
    return image + DATA_OFFSET + ((size_t)(number - FIRST_DATA_CLUSTER) * CLUSTER_SIZE);
}

static uint8_t *fat_entry(uint8_t *image, uint16_t number) {
    return image + FAT_OFFSET + ((size_t)number * FAT_ENTRY_SIZE);
}

static void link_chain(uint8_t *image, const uint16_t *chain, size_t count) {
    for (size_t i = 0; i < count; i++) {
        put_le16(fat_entry(image, chain[i]), (uint16_t)(i + 1 < count ? chain[i + 1] : FAT_CHAIN_END));
    }
}

static uint8_t *dirent_at(uint8_t *directory, size_t index) {
    return directory + (index * DIRENT_SIZE);
}

static void put_dirent(uint8_t *slot, const char *name, uint8_t attributes) {
    memset(slot, ' ', DIRENT_NAME_SIZE);
    for (size_t i = 0; name[i] != '\0'; i++) {
        slot[i] = (uint8_t)name[i];
    }
    slot[DIRENT_ATTRIBUTES] = attributes;
}

static void put_directory(uint8_t *slot, const char *name, uint16_t first_cluster) {
    put_dirent(slot, name, ATTR_DIRECTORY);
    put_le16(slot + DIRENT_FIRST_CLUSTER, first_cluster);
}

static void put_file(uint8_t *slot, const char *name, uint8_t attributes, const uint16_t *chain) {
    put_dirent(slot, name, attributes);
    put_le16(slot + DIRENT_FIRST_CLUSTER, chain[0]);
    put_le32(slot + DIRENT_FILE_SIZE, LOG_SIZE);
}

static void put_deleted(uint8_t *slot) {
    put_file(slot, "REMOVED", ATTR_NONE, LOG_FILE);
    slot[0] = DIRENT_DELETED;
}

static void store_file(uint8_t *image, const uint16_t *chain, const uint8_t *data) {
    for (size_t i = 0; i < LOG_CLUSTERS; i++) {
        size_t offset = i * CLUSTER_SIZE;
        size_t len = LOG_SIZE - offset < CLUSTER_SIZE ? LOG_SIZE - offset : CLUSTER_SIZE;
        memcpy(cluster(image, chain[i]), data + offset, len);
    }
    link_chain(image, chain, LOG_CLUSTERS);
}

/* The log sits at /2022/LOG/<file>. Its clusters are out of order, and the
   year directory spans two clusters that are not adjacent. Every entry that
   a reader must skip points at the log file, so a reader that follows one
   reports duplicate sessions. */
static uint8_t *build_image(const uint8_t *log) {
    uint8_t *image = calloc(1, IMAGE_SIZE);
    memcpy(image, VFF_MAGIC, sizeof VFF_MAGIC);
    put_be(image + VFF_HEADER_BYTE_ORDER, VFF_BYTE_ORDER, 2);
    put_be(image + VFF_HEADER_VERSION, VFF_VERSION, 2);
    put_be(image + VFF_HEADER_VOLUME_SIZE, IMAGE_SIZE, 4);
    put_be(image + VFF_HEADER_CLUSTER_SIZE, CLUSTER_SIZE / VFF_CLUSTER_SIZE_UNIT, 2);

    uint8_t *root = image + ROOT_OFFSET;
    put_file(dirent_at(root, 0), "LABEL", ATTR_VOLUME, LOG_FILE);
    put_deleted(dirent_at(root, 1));
    put_directory(dirent_at(root, 2), "2022", YEAR_DIR_HEAD);
    put_file(dirent_at(root, 3), "MAIL", ATTR_NONE, MAIL_FILE);

    uint8_t *year_head = cluster(image, YEAR_DIR_HEAD);
    put_directory(dirent_at(year_head, 0), ".", YEAR_DIR_HEAD);
    put_directory(dirent_at(year_head, 1), "..", YEAR_DIR_HEAD);
    for (size_t i = 2; i < ENTRIES_PER_CLUSTER; i++) {
        put_deleted(dirent_at(year_head, i));
    }
    uint8_t *year_tail = cluster(image, YEAR_DIR_TAIL);
    put_file(dirent_at(year_tail, 0), "LONGNAME", ATTR_LONG_NAME, LOG_FILE);
    put_directory(dirent_at(year_tail, 1), "LOG", LOG_DIR);
    link_chain(image, (const uint16_t[]){YEAR_DIR_HEAD, YEAR_DIR_TAIL}, 2);

    put_file(cluster(image, LOG_DIR), "2B06C4C3000", ATTR_NONE, LOG_FILE);
    link_chain(image, (const uint16_t[]){LOG_DIR}, 1);
    store_file(image, LOG_FILE, log);

    uint8_t mail[LOG_SIZE];
    memcpy(mail, log, LOG_SIZE);
    memcpy(mail + CDBFILE_DESCRIPTION, MAIL_DESCRIPTION, sizeof MAIL_DESCRIPTION);
    store_file(image, MAIL_FILE, mail);
    return image;
}

/* An image in memory that counts what the parse reads and can fail after a
   number of reads. */
typedef struct {
    const uint8_t *data;
    size_t len;
    size_t reads_left;
    size_t bytes_read;
} memory_image;

static bool read_memory(void *source, size_t offset, uint8_t *out, size_t len) {
    memory_image *image = source;
    CHECK(offset <= image->len && len <= image->len - offset);
    if (image->reads_left == 0) {
        return false;
    }
    image->reads_left--;
    image->bytes_read += len;
    memcpy(out, image->data + offset, len);
    return true;
}

static collected sessions_in_memory(memory_image *image) {
    collected found = {0};
    const vff_image reader = {read_memory, image, image->len};
    found.read_whole = playlog_each_session(&reader, collect, &found);
    return found;
}

static collected sessions_in(const uint8_t *image, size_t len) {
    memory_image source = {.data = image, .len = len, .reads_left = SIZE_MAX};
    return sessions_in_memory(&source);
}

static collected sessions_in_log(const entry_spec *entries, size_t count) {
    uint8_t log[LOG_SIZE];
    build_log(log, entries, count);
    uint8_t *image = build_image(log);
    collected found = sessions_in(image, IMAGE_SIZE);
    free(image);
    return found;
}

static void test_reads_sessions_from_a_fragmented_image(void) {
    const entry_spec entries[] = {
        {"RMCE", MARIO_KART, NULL, START, START + (5400 * SECOND), true},
        {"RZDE", MARIO_KART, NULL, START, START + SECOND, false},
        {"RSBJ", SUMABURA, LETTER_X, START + (6000 * SECOND), START + (6060 * SECOND), true},
    };
    collected found = sessions_in_log(entries, 3);

    CHECK(found.count == 2);
    CHECK(found.items[0].console_id == CONSOLE_ID);
    CHECK(strcmp(found.items[0].game_code, "RMCE") == 0);
    CHECK(strcmp(found.items[0].maker_code, "01") == 0);
    CHECK(strcmp(found.items[0].title, "Mario Kart Wii") == 0);
    CHECK(found.items[0].start_ticks == START);
    CHECK(found.items[0].end_ticks == START + (5400 * SECOND));
    CHECK(strcmp(found.items[1].game_code, "RSBJ") == 0);
    CHECK(strcmp(found.items[1].title, "\xE3\x82\xB9\xE3\x83\x9E\xE3\x83\x96\xE3\x83\xA9 X") == 0);
}

static void test_skips_entries_that_cannot_be_sessions(void) {
    const entry_spec entries[] = {
        {"RMCE", MARIO_KART, NULL, START + SECOND, START, true},
        {"RMCE", MARIO_KART, NULL, START, UINT64_MAX, true},
        {"\x01\x02\x03\x04", MARIO_KART, NULL, START, START + SECOND, true},
        {"RSBJ", SUMABURA, NULL, START, START + SECOND, true},
    };
    collected found = sessions_in_log(entries, 4);

    CHECK(found.count == 1);
    CHECK(strcmp(found.items[0].game_code, "RSBJ") == 0);
}

static void test_survives_damaged_images(void) {
    const entry_spec entries[] = {{"RMCE", MARIO_KART, NULL, START, START + SECOND, true}};
    uint8_t log[LOG_SIZE];
    build_log(log, entries, 1);
    uint8_t *image = build_image(log);

    const size_t truncated_lengths[] = {
        0,
        0x1F,
        FAT_OFFSET + (FAT_SIZE / 2),
        ROOT_OFFSET + (2 * DIRENT_SIZE),
        DATA_OFFSET + 0x100,
        (size_t)(cluster(image, LOG_FILE[3]) - image),
        (size_t)(cluster(image, LOG_FILE[6]) - image) + 0x10,
    };
    for (size_t i = 0; i < sizeof truncated_lengths / sizeof *truncated_lengths; i++) {
        /* An exact-size copy lets the address sanitizer see a read past the end. */
        size_t len = truncated_lengths[i];
        uint8_t *copy = malloc(len ? len : 1);
        memcpy(copy, image, len);
        CHECK(sessions_in(copy, len).count <= 1);
        free(copy);
    }

    /* A directory with no end marker whose last cluster links back to its
       first one. The walk must still return. */
    uint8_t *year_tail = cluster(image, YEAR_DIR_TAIL);
    for (size_t i = 2; i < ENTRIES_PER_CLUSTER; i++) {
        dirent_at(year_tail, i)[0] = DIRENT_DELETED;
    }
    put_le16(fat_entry(image, YEAR_DIR_TAIL), YEAR_DIR_HEAD);
    CHECK(sessions_in(image, IMAGE_SIZE).count == 1);
    free(image);

    put_be(log + CDBFILE_SIZE + RECORD_ATTACHMENT_SIZE, ATTACHMENT_PAST_THE_FILE, 4);
    image = build_image(log);
    CHECK(sessions_in(image, IMAGE_SIZE).count == 0);
    free(image);
}

static uint8_t *one_session_image(void) {
    const entry_spec entries[] = {{"RMCE", MARIO_KART, NULL, START, START + SECOND, true}};
    uint8_t log[LOG_SIZE];
    build_log(log, entries, 1);
    return build_image(log);
}

static void test_reads_only_the_directories_and_the_file_heads(void) {
    uint8_t *image = one_session_image();
    memory_image source = {.data = image, .len = IMAGE_SIZE, .reads_left = SIZE_MAX};

    CHECK(sessions_in_memory(&source).count == 1);
    /* The image holds two files. Its directories and the FAT entries that
       lead to them fit in a few clusters. */
    CHECK(source.bytes_read <= VFF_HEADER_SIZE + (2 * LOG_SIZE) + (8 * CLUSTER_SIZE));
    free(image);
}

static void test_a_failed_read_gives_fewer_sessions_and_says_so(void) {
    uint8_t *image = one_session_image();
    memory_image whole = {.data = image, .len = IMAGE_SIZE, .reads_left = SIZE_MAX};
    collected all = sessions_in_memory(&whole);
    CHECK(all.count == 1 && all.read_whole);

    for (size_t reads = 0; reads < SIZE_MAX - whole.reads_left; reads++) {
        memory_image failing = {.data = image, .len = IMAGE_SIZE, .reads_left = reads};
        collected found = sessions_in_memory(&failing);
        CHECK(found.count <= 1 && !found.read_whole);
    }
    free(image);
}

typedef struct {
    char text[JSON_VALUE_CAP];
} json_value;

/* Returns the string value of the field named key at its occurrence-th
   appearance in json, counting from 0. A missing field gives "". */
static json_value json_string(const char *json, int occurrence, const char *key) {
    json_value value = {0};
    char pattern[JSON_VALUE_CAP];
    (void)snprintf(pattern, sizeof pattern, "\"%s\":\"", key);
    const char *found = json;
    for (int i = 0; i <= occurrence && found; i++) {
        found = strstr(i == 0 ? found : found + 1, pattern);
    }
    if (found) {
        found += strlen(pattern);
        size_t len = strcspn(found, "\"");
        memcpy(value.text, found, len < sizeof value.text ? len : sizeof value.text - 1);
    }
    return value;
}

/* 2022-11-15 22:44:24 console local time, an hour after START. */
static int64_t console_clock(void) {
    return ((int64_t)START_SECONDS + UNIX_SECONDS_AT_2000 + SECONDS_PER_HOUR) * MS_PER_SECOND;
}

static void test_builds_the_batch_body(void) {
    const playlog_session session = {
        .console_id = CONSOLE_ID,
        .start_ticks = START + (SECOND / 2),
        .end_ticks = START + (SECOND / 2) + (11 * SECOND),
        .game_code = "HAAA",
        .maker_code = "01",
        .title = "Say \"hi\"\\\n",
    };
    const events_config config = {.api_key = "phc_test", .now_ms = console_clock};
    char json[EVENTS_JSON_CAP(1)];

    size_t len = events_batch_json(&config, &session, 1, json, sizeof json);

    static const char envelope[] = "{\"api_key\":\"phc_test\",\"sent_at\":\"2022-11-15T22:44:24.000Z\","
                                   "\"historical_migration\":true,\"batch\":[";
    CHECK(len == strlen(json));
    CHECK(strncmp(json, envelope, sizeof envelope - 1) == 0);
    CHECK(strstr(json, "\"event\":\"game started\"") != NULL);
    CHECK(strstr(json, "\"event\":\"game ended\"") != NULL);
    CHECK(strstr(json, "\"timestamp\":\"2022-11-15T21:44:24.500Z\"") != NULL);
    CHECK(strstr(json, "\"timestamp\":\"2022-11-15T21:44:35.500Z\"") != NULL);
    CHECK(strstr(json, "\"game_title\":\"Say \\\"hi\\\"\\\\\\u000a\"") != NULL);
    CHECK(strstr(json, "\"title_type\":\"channel\"") != NULL);
    CHECK(strstr(json, "\"duration_seconds\":11}") != NULL);

    json_value started = json_string(json, 0, "uuid");
    json_value ended = json_string(json, 1, "uuid");
    json_value session_id = json_string(json, 0, "$session_id");
    uint64_t local_start_ms = (((uint64_t)START_SECONDS + UNIX_SECONDS_AT_2000) * MS_PER_SECOND) + (MS_PER_SECOND / 2);
    char expected_prefix[JSON_VALUE_CAP];
    (void)snprintf(expected_prefix, sizeof expected_prefix, "%08llx-%04llx-7",
                   (unsigned long long)(local_start_ms / UUID_TIME_LOW_RANGE),
                   (unsigned long long)(local_start_ms % UUID_TIME_LOW_RANGE));

    CHECK(strlen(started.text) == 36 && started.text[14] == '5');
    CHECK(strcmp(started.text, ended.text) != 0);
    CHECK(strncmp(session_id.text, expected_prefix, strlen(expected_prefix)) == 0);

    CHECK(events_batch_json(&config, &session, 1, json, 64) == 0);
}

typedef struct {
    int request; /* counted from 1, and 0 for none */
    long status;
} planned_failure;

static const planned_failure NO_FAILURE = {0};

static struct {
    int requests;
    planned_failure failure;
    bool to_batch_path;
    size_t last_len;
    char last_end[3];
} posted;

static long record_post(const http_request *request, char *answer, size_t answer_cap) {
    posted.requests++;
    posted.to_batch_path = strcmp(request->path, "/batch/") == 0;
    posted.last_len = request->body_len;
    memcpy(posted.last_end, request->body_len >= 2 ? (const char *)request->body + request->body_len - 2 : "??", 2);
    if (posted.requests == posted.failure.request) {
        return posted.failure.status;
    }
    (void)snprintf(answer, answer_cap, "{%s}", UPLOAD_OK_MARK);
    return UPLOAD_HTTP_OK;
}

static bool upload_sessions(upload *batch, const playlog_session *session, int count, planned_failure failure) {
    events_config config = {.api_key = "phc_test", .now_ms = console_clock};
    memset(&posted, 0, sizeof posted);
    posted.failure = failure;
    upload_begin(batch, &config, "http://example.com", record_post);
    for (int i = 0; i < count; i++) {
        upload_add(batch, session);
    }
    return upload_end(batch);
}

static void test_uploads_in_requests_and_stops_at_the_first_failure(void) {
    static upload batch;
    playlog_session session = {.game_code = "HAAA", .maker_code = "01", .title = "Photo Channel"};

    CHECK(upload_sessions(&batch, &session, (2 * UPLOAD_SESSIONS_PER_REQUEST) + 10, NO_FAILURE));
    CHECK(posted.requests == 3);
    CHECK(posted.to_batch_path);
    CHECK(batch.sent == (2 * UPLOAD_SESSIONS_PER_REQUEST) + 10);

    const planned_failure second_request_fails = {.request = 2, .status = HTTP_SERVER_ERROR};
    CHECK(!upload_sessions(&batch, &session, 3 * UPLOAD_SESSIONS_PER_REQUEST, second_request_fails));
    CHECK(posted.requests == 2);
    CHECK(batch.sent == UPLOAD_SESSIONS_PER_REQUEST);
    CHECK(batch.status == HTTP_SERVER_ERROR);

    const planned_failure first_answer_is_not_capture = {.request = 1, .status = UPLOAD_HTTP_OK};
    CHECK(!upload_sessions(&batch, &session, UPLOAD_SESSIONS_PER_REQUEST, first_answer_is_not_capture));
    CHECK(batch.sent == 0);
    CHECK(batch.status == UPLOAD_NOT_CAPTURE);

    CHECK(upload_sessions(&batch, &session, 0, NO_FAILURE));
    CHECK(posted.requests == 0);
}

static void test_a_full_request_of_the_longest_sessions_fits_its_buffer(void) {
    static upload batch;
    playlog_session session = {.game_code = "\"\\\"\\", .maker_code = "\"\\"};
    memset(session.title, '\x01', sizeof session.title - 1);

    CHECK(upload_sessions(&batch, &session, UPLOAD_SESSIONS_PER_REQUEST, NO_FAILURE));
    CHECK(posted.last_len > 0);
    CHECK(memcmp(posted.last_end, "]}", 2) == 0);
}

static void test_summarizes_a_session(void) {
    const playlog_session session = {
        .start_ticks = START,
        .end_ticks = START + (3661 * SECOND),
        .game_code = "HAAA",
        .title = "Photo Channel",
    };
    char line[SESSION_SUMMARY_CAP];
    session_summary(&session, line);

    CHECK(strcmp(line, "2022-11-15 21:44:24  1:01:01  HAAA  Photo Channel") == 0);
}

static void test_reads_a_settings_file_saved_by_a_text_editor(void) {
    static const char text[] = "\xEF\xBB\xBFhost = http://192.168.1.20:8010/\r\n"
                               "# my project\r\n"
                               "api_key=phc_abc\r\n"
                               "color = red\r\n";
    settings parsed = {0};
    settings_parse(&parsed, text, sizeof text - 1);

    CHECK(strcmp(parsed.host, "http://192.168.1.20:8010") == 0);
    CHECK(strcmp(parsed.api_key, "phc_abc") == 0);
}

static void test_a_settings_value_that_cannot_be_used_changes_nothing(void) {
    char text[SETTINGS_VALUE_CAP * 2];
    int len = snprintf(text, sizeof text, "api_key = %0*d\n", SETTINGS_VALUE_CAP, 0);
    settings parsed = {.api_key = "phc_abc"};
    settings_parse(&parsed, text, (size_t)len);

    CHECK(strcmp(parsed.api_key, "phc_abc") == 0);
}

static void test_a_cursor_counts_only_for_its_own_host_and_token(void) {
    char text[CURSOR_TEXT_CAP];
    size_t len = cursor_write(text, "https://us.i.posthog.com", "phc_one", START);

    CHECK(cursor_read(text, len, "https://us.i.posthog.com", "phc_one") == START);
    CHECK(cursor_read(text, len, "https://us.i.posthog.com", "phc_two") == 0);
    CHECK(cursor_read(text, len, "https://eu.i.posthog.com", "phc_one") == 0);
    CHECK(cursor_read(text, len / 2, "https://us.i.posthog.com", "phc_one") == 0);
    CHECK(cursor_read(text, len - 2, "https://us.i.posthog.com", "phc_one") == 0);
}

static void test_a_cursor_past_the_largest_tick_count_is_ignored(void) {
    char text[CURSOR_TEXT_CAP];
    size_t len = cursor_write(text, "https://us.i.posthog.com", "phc_one", UINT64_MAX);
    text[len - 1] = '0';
    text[len++] = '\n';

    CHECK(cursor_read(text, len, "https://us.i.posthog.com", "phc_one") == 0);
}

static void test_sha1_matches_the_standard_vector(void) {
    static const uint8_t expected[SHA1_SIZE] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
        0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d,
    };
    uint8_t digest[SHA1_SIZE];
    sha1((const uint8_t *)"abc", 3, digest);
    CHECK(memcmp(digest, expected, SHA1_SIZE) == 0);
}

int main(void) {
    test_reads_sessions_from_a_fragmented_image();
    test_skips_entries_that_cannot_be_sessions();
    test_survives_damaged_images();
    test_reads_only_the_directories_and_the_file_heads();
    test_a_failed_read_gives_fewer_sessions_and_says_so();
    test_builds_the_batch_body();
    test_uploads_in_requests_and_stops_at_the_first_failure();
    test_a_full_request_of_the_longest_sessions_fits_its_buffer();
    test_summarizes_a_session();
    test_reads_a_settings_file_saved_by_a_text_editor();
    test_a_settings_value_that_cannot_be_used_changes_nothing();
    test_a_cursor_counts_only_for_its_own_host_and_token();
    test_a_cursor_past_the_largest_tick_count_is_ignored();
    test_sha1_matches_the_standard_vector();
    return report();
}
