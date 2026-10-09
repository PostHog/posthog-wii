#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "events.h"
#include "playlog.h"
#include "sha1.h"
#include "summary.h"
#include "ticks.h"

enum {
    IMAGE_SIZE = 0x280000,
    CLUSTER_SIZE = 0x200,
    FAT_OFFSET = 0x20,
    FAT_SIZE = IMAGE_SIZE / CLUSTER_SIZE * 2,
    ROOT_OFFSET = FAT_OFFSET + 2 * FAT_SIZE,
    DIRENT_SIZE = 0x20,
    DATA_OFFSET = ROOT_OFFSET + 128 * DIRENT_SIZE,
    ENTRIES_PER_CLUSTER = CLUSTER_SIZE / DIRENT_SIZE,

    CDBFILE_SIZE = 0x400,
    ATTACHMENT_OFFSET = 0x220,
    ATTACHMENT_SIZE = 0x668,
    ATTACHMENT_SIZE_WITHOUT_HEADER = 0x558,
    LOG_SIZE = CDBFILE_SIZE + ATTACHMENT_OFFSET + ATTACHMENT_SIZE,
    LOG_CLUSTERS = 7,
    ENTRY_SIZE = 0x88,

    ATTR_VOLUME = 0x08,
    ATTR_LONG_NAME = 0x0F,
    ATTR_DIRECTORY = 0x10,
    DELETED = 0xE5,

    YEAR_DIR_HEAD = 2,
    YEAR_DIR_TAIL = 9,
    LOG_DIR = 5,
    COLLECT_CAP = 16,
};

static const unsigned LOG_FILE[LOG_CLUSTERS] = {20, 30, 21, 40, 22, 41, 23};
static const unsigned MAIL_FILE[LOG_CLUSTERS] = {50, 51, 52, 53, 54, 55, 56};

static const uint64_t CONSOLE_ID = 0x0123456789ABCDEFull;
/* 2022-11-15 21:44:24 console local time. */
static const uint64_t START = (uint64_t)TICKS_PER_SECOND * 721863864;
static const uint64_t SECOND = TICKS_PER_SECOND;

static const uint16_t MARIO_KART[] = {'M', 'a', 'r', 'i', 'o', ' ', 'K', 'a', 'r', 't', ' ', 'W', 'i', 'i', 0};
static const uint16_t SUMABURA[] = {0x30B9, 0x30DE, 0x30D6, 0x30E9, 0};
static const uint16_t X[] = {'X', 0};

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
} collected;

static void collect(void *ctx, const playlog_session *session) {
    collected *found = ctx;
    if (found->count < COLLECT_CAP) {
        found->items[found->count] = *session;
    }
    found->count++;
}

static void put_be(uint8_t *out, uint64_t value, int size) {
    for (int i = 0; i < size; i++) {
        out[i] = (uint8_t)(value >> (8 * (size - 1 - i)));
    }
}

static void put_le(uint8_t *out, uint32_t value, int size) {
    for (int i = 0; i < size; i++) {
        out[i] = (uint8_t)(value >> (8 * i));
    }
}

static void put_entry(uint8_t *entry, entry_spec spec) {
    const uint16_t *lines[2] = {spec.line1, spec.line2};
    entry[0] = spec.in_use;
    for (int line = 0; line < 2; line++) {
        for (int i = 0; lines[line] && lines[line][i]; i++) {
            put_be(entry + 0x0C + line * 42 + i * 2, lines[line][i], 2);
        }
    }
    put_be(entry + 0x60, spec.start, 8);
    put_be(entry + 0x68, spec.end, 8);
    memcpy(entry + 0x70, spec.code, 4);
    memcpy(entry + 0x74, "01", 2);
}

static void build_log(uint8_t log[LOG_SIZE], bool with_header, const entry_spec *entries, int count) {
    memset(log, 0, LOG_SIZE);
    memcpy(log, "CDBFILE\x02", 8);
    put_be(log + 0x08, CONSOLE_ID, 8);
    strcpy((char *)log + 0x14, "playtimelog");

    uint8_t *record = log + CDBFILE_SIZE;
    put_be(record, 0x52495F35, 4);
    put_be(record + 0x128, 3, 4);
    put_be(record + 0x12C, ATTACHMENT_OFFSET, 4);
    put_be(record + 0x130, with_header ? ATTACHMENT_SIZE : ATTACHMENT_SIZE_WITHOUT_HEADER, 4);

    uint8_t *attachment = record + ATTACHMENT_OFFSET;
    if (with_header) {
        put_be(attachment, 0x30335F30, 4);
        attachment += 8;
    }
    for (int i = 0; i < count; i++) {
        put_entry(attachment + i * ENTRY_SIZE, entries[i]);
    }
}

static uint8_t *cluster(uint8_t *image, unsigned number) {
    return image + DATA_OFFSET + (size_t)(number - 2) * CLUSTER_SIZE;
}

static void link_chain(uint8_t *image, const unsigned *chain, int count) {
    for (int i = 0; i < count; i++) {
        put_le(image + FAT_OFFSET + chain[i] * 2, i + 1 < count ? chain[i + 1] : 0xFFFF, 2);
    }
}

static void put_dirent(uint8_t *slot, const char *name, uint8_t attributes, unsigned first_cluster, uint32_t size) {
    memset(slot, ' ', 11);
    memcpy(slot, name, strlen(name));
    slot[11] = attributes;
    put_le(slot + 26, first_cluster, 2);
    put_le(slot + 28, size, 4);
}

static void store_file(uint8_t *image, const unsigned *chain, const uint8_t *data) {
    for (int i = 0; i < LOG_CLUSTERS; i++) {
        size_t offset = (size_t)i * CLUSTER_SIZE;
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
    memcpy(image, "VFF ", 4);
    put_be(image + 0x04, 0xFEFF, 2);
    put_be(image + 0x06, 0x0100, 2);
    put_be(image + 0x08, IMAGE_SIZE, 4);
    put_be(image + 0x0C, CLUSTER_SIZE >> 4, 2);

    uint8_t *root = image + ROOT_OFFSET;
    put_dirent(root, "LABEL", ATTR_VOLUME, LOG_FILE[0], LOG_SIZE);
    put_dirent(root + DIRENT_SIZE, "REMOVED", 0, LOG_FILE[0], LOG_SIZE);
    root[DIRENT_SIZE] = DELETED;
    put_dirent(root + 2 * DIRENT_SIZE, "2022", ATTR_DIRECTORY, YEAR_DIR_HEAD, 0);
    put_dirent(root + 3 * DIRENT_SIZE, "MAIL", 0, MAIL_FILE[0], LOG_SIZE);

    uint8_t *year_head = cluster(image, YEAR_DIR_HEAD);
    put_dirent(year_head, ".", ATTR_DIRECTORY, YEAR_DIR_HEAD, 0);
    put_dirent(year_head + DIRENT_SIZE, "..", ATTR_DIRECTORY, YEAR_DIR_HEAD, 0);
    for (int i = 2; i < ENTRIES_PER_CLUSTER; i++) {
        put_dirent(year_head + i * DIRENT_SIZE, "REMOVED", 0, LOG_FILE[0], LOG_SIZE);
        year_head[i * DIRENT_SIZE] = DELETED;
    }
    uint8_t *year_tail = cluster(image, YEAR_DIR_TAIL);
    put_dirent(year_tail, "LONGNAME", ATTR_LONG_NAME, LOG_FILE[0], LOG_SIZE);
    put_dirent(year_tail + DIRENT_SIZE, "LOG", ATTR_DIRECTORY, LOG_DIR, 0);
    link_chain(image, (const unsigned[]){YEAR_DIR_HEAD, YEAR_DIR_TAIL}, 2);

    put_dirent(cluster(image, LOG_DIR), "2B06C4C3000", 0, LOG_FILE[0], LOG_SIZE);
    link_chain(image, (const unsigned[]){LOG_DIR}, 1);
    store_file(image, LOG_FILE, log);

    uint8_t mail[LOG_SIZE];
    memcpy(mail, log, LOG_SIZE);
    strcpy((char *)mail + 0x14, "mail");
    store_file(image, MAIL_FILE, mail);
    return image;
}

static collected sessions_in(const uint8_t *image, size_t len) {
    collected found = {0};
    playlog_each_session(image, len, collect, &found);
    return found;
}

static collected sessions_in_log(bool with_header, const entry_spec *entries, int count) {
    uint8_t log[LOG_SIZE];
    build_log(log, with_header, entries, count);
    uint8_t *image = build_image(log);
    collected found = sessions_in(image, IMAGE_SIZE);
    free(image);
    return found;
}

static void test_reads_sessions_from_a_fragmented_image(void) {
    const entry_spec entries[] = {
        {"RMCE", MARIO_KART, NULL, START, START + 5400 * SECOND, true},
        {"RZDE", MARIO_KART, NULL, START, START + SECOND, false},
        {"RSBJ", SUMABURA, X, START + 6000 * SECOND, START + 6060 * SECOND, true},
    };
    collected found = sessions_in_log(true, entries, 3);

    CHECK(found.count == 2);
    CHECK(found.items[0].console_id == CONSOLE_ID);
    CHECK(strcmp(found.items[0].game_code, "RMCE") == 0);
    CHECK(strcmp(found.items[0].maker_code, "01") == 0);
    CHECK(strcmp(found.items[0].title, "Mario Kart Wii") == 0);
    CHECK(found.items[0].start_ticks == START);
    CHECK(found.items[0].end_ticks == START + 5400 * SECOND);
    CHECK(strcmp(found.items[1].game_code, "RSBJ") == 0);
    CHECK(strcmp(found.items[1].title, "\xE3\x82\xB9\xE3\x83\x9E\xE3\x83\x96\xE3\x83\xA9 X") == 0);
}

static void test_reads_the_layout_without_a_header(void) {
    const entry_spec entries[] = {{"RMCE", MARIO_KART, NULL, START, START + SECOND, true}};
    collected found = sessions_in_log(false, entries, 1);

    CHECK(found.count == 1);
    CHECK(strcmp(found.items[0].game_code, "RMCE") == 0);
}

static void test_skips_entries_that_cannot_be_sessions(void) {
    const entry_spec entries[] = {
        {"RMCE", MARIO_KART, NULL, START + SECOND, START, true},
        {"RMCE", MARIO_KART, NULL, START, UINT64_MAX, true},
        {"\x01\x02\x03\x04", MARIO_KART, NULL, START, START + SECOND, true},
        {"RSBJ", SUMABURA, NULL, START, START + SECOND, true},
    };
    collected found = sessions_in_log(true, entries, 4);

    CHECK(found.count == 1);
    CHECK(strcmp(found.items[0].game_code, "RSBJ") == 0);
}

static void test_survives_damaged_images(void) {
    const entry_spec entries[] = {{"RMCE", MARIO_KART, NULL, START, START + SECOND, true}};
    uint8_t log[LOG_SIZE];
    build_log(log, true, entries, 1);
    uint8_t *image = build_image(log);

    const size_t truncated_lengths[] = {
        0,
        0x1F,
        FAT_OFFSET + FAT_SIZE / 2,
        ROOT_OFFSET + 2 * DIRENT_SIZE,
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
    for (int i = 2; i < ENTRIES_PER_CLUSTER; i++) {
        year_tail[i * DIRENT_SIZE] = DELETED;
    }
    put_le(image + FAT_OFFSET + YEAR_DIR_TAIL * 2, YEAR_DIR_HEAD, 2);
    CHECK(sessions_in(image, IMAGE_SIZE).count == 1);
    free(image);

    put_be(log + CDBFILE_SIZE + 0x130, 0xFFFFFF, 4);
    image = build_image(log);
    CHECK(sessions_in(image, IMAGE_SIZE).count == 0);
    free(image);
}

static void json_field(const char *json, const char *key, int occurrence, char out[64]) {
    char pattern[32];
    snprintf(pattern, sizeof pattern, "\"%s\":\"", key);
    const char *found = json;
    for (int i = 0; i <= occurrence && found; i++) {
        found = strstr(i == 0 ? found : found + 1, pattern);
    }
    out[0] = '\0';
    if (found) {
        found += strlen(pattern);
        size_t len = strcspn(found, "\"");
        memcpy(out, found, len < 63 ? len : 63);
        out[len < 63 ? len : 63] = '\0';
    }
}

static void test_builds_the_batch_body(void) {
    playlog_session session = {
        .console_id = CONSOLE_ID,
        .start_ticks = START + SECOND / 2,
        .end_ticks = START + SECOND / 2 + 11 * SECOND,
        .game_code = "HAAA",
        .maker_code = "01",
        .title = "Say \"hi\"\\\n",
    };
    events_config puerto_rico = {.api_key = "phc_test", .utc_offset_minutes = -240};
    events_config utc = {.api_key = "phc_test", .utc_offset_minutes = 0};
    char json[EVENTS_JSON_CAP(1)];
    char json_utc[EVENTS_JSON_CAP(1)];

    size_t len = events_batch_json(&puerto_rico, &session, 1, json, sizeof json);
    events_batch_json(&utc, &session, 1, json_utc, sizeof json_utc);

    static const char envelope[] = "{\"api_key\":\"phc_test\",\"historical_migration\":true,\"batch\":[";
    CHECK(len == strlen(json));
    CHECK(strncmp(json, envelope, sizeof envelope - 1) == 0);
    CHECK(strstr(json, "\"event\":\"game started\"") != NULL);
    CHECK(strstr(json, "\"event\":\"game ended\"") != NULL);
    CHECK(strstr(json, "\"timestamp\":\"2022-11-16T01:44:24.500Z\"") != NULL);
    CHECK(strstr(json, "\"timestamp\":\"2022-11-16T01:44:35.500Z\"") != NULL);
    CHECK(strstr(json, "\"game_title\":\"Say \\\"hi\\\"\\\\\\u000a\"") != NULL);
    CHECK(strstr(json, "\"title_type\":\"channel\"") != NULL);
    CHECK(strstr(json, "\"duration_seconds\":11}") != NULL);
    CHECK(strstr(json, "sent_at") == NULL);

    char started[64], ended[64], started_utc[64], session_id[64], expected_prefix[64];
    json_field(json, "uuid", 0, started);
    json_field(json, "uuid", 1, ended);
    json_field(json_utc, "uuid", 0, started_utc);
    json_field(json, "$session_id", 0, session_id);
    long long start_ms = (721863864LL + 946684800 + 240 * 60) * 1000 + 500;
    snprintf(expected_prefix, sizeof expected_prefix, "%08llx-%04llx-7", start_ms >> 16, start_ms & 0xFFFF);

    CHECK(strlen(started) == 36 && started[14] == '5');
    CHECK(strcmp(started, ended) != 0);
    CHECK(strcmp(started, started_utc) == 0);
    CHECK(strncmp(session_id, expected_prefix, strlen(expected_prefix)) == 0);

    CHECK(events_batch_json(&utc, &session, 1, json, 64) == 0);
}

static void test_summarizes_a_session(void) {
    playlog_session session = {
        .start_ticks = START,
        .end_ticks = START + 3661 * SECOND,
        .game_code = "HAAA",
        .title = "Photo Channel",
    };
    char line[SESSION_SUMMARY_CAP];
    session_summary(&session, line);

    CHECK(strcmp(line, "2022-11-15 21:44:24  1:01:01  HAAA  Photo Channel") == 0);
}

static void test_sha1_matches_the_standard_vector(void) {
    static const uint8_t expected[SHA1_SIZE] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                               0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    uint8_t digest[SHA1_SIZE];
    sha1((const uint8_t *)"abc", 3, digest);
    CHECK(memcmp(digest, expected, SHA1_SIZE) == 0);
}

int main(void) {
    test_reads_sessions_from_a_fragmented_image();
    test_reads_the_layout_without_a_header();
    test_skips_entries_that_cannot_be_sessions();
    test_survives_damaged_images();
    test_builds_the_batch_body();
    test_summarizes_a_session();
    test_sha1_matches_the_standard_vector();
    return report();
}
