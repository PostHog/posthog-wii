#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "http.h"
#include "http_request.h"
#include "playlog.h"
#include "settings.h"
#include "summary.h"
#include "upload.h"

typedef struct {
    const char *path;
    const char *host;
    const char *api_key;
    int utc_offset_minutes;
    bool dry_run;
} options;

typedef struct {
    playlog_session *items;
    size_t count;
    size_t cap;
} session_list;

enum { FIRST_SESSION_CAP = 256 };

static void usage(void) {
    (void)fputs("usage: wii2posthog <cdb.vff> [--utc-offset +HH:MM] --dry-run\n"
                "       wii2posthog <cdb.vff> [--utc-offset +HH:MM] --host <url> --api-key <key>\n",
                stderr);
}

static bool parse_options(int argc, char **argv, options *opts) {
    for (int i = 1; i < argc && argv[i]; i++) {
        const char *arg = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(arg, "--dry-run") == 0) {
            opts->dry_run = true;
        } else if (strcmp(arg, "--host") == 0 && value) {
            opts->host = value;
            i++;
        } else if (strcmp(arg, "--api-key") == 0 && value) {
            opts->api_key = value;
            i++;
        } else if (strcmp(arg, "--utc-offset") == 0 && value &&
                   settings_utc_offset(value, strlen(value), &opts->utc_offset_minutes)) {
            i++;
        } else if (arg[0] != '-' && !opts->path) {
            opts->path = arg;
        } else {
            return false;
        }
    }
    return (opts->path && (opts->dry_run || (opts->host && opts->api_key))) != 0;
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    uint8_t *data = NULL;
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (size > 0 && fseek(file, 0, SEEK_SET) == 0) {
        data = malloc((size_t)size);
        if (data) {
            *len = fread(data, 1, (size_t)size, file);
        }
    }
    (void)fclose(file);
    return data;
}

static void collect(void *ctx, const playlog_session *session) {
    session_list *list = ctx;
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : FIRST_SESSION_CAP;
        playlog_session *items = realloc(list->items, cap * sizeof *items);
        if (!items) {
            perror("wii2posthog");
            exit(1);
        }
        list->items = items;
        list->cap = cap;
    }
    list->items[list->count++] = *session;
}

/* qsort sets the signature of a comparison function. */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static int by_start(const void *left_session, const void *right_session) {
    uint64_t left = ((const playlog_session *)left_session)->start_ticks;
    uint64_t right = ((const playlog_session *)right_session)->start_ticks;
    return (left > right) - (left < right);
}

/* A failed write to stdout counts as no answer, which stops the upload. */
static long print_body(const http_request *request, char *answer, size_t answer_cap) {
    if (fwrite(request->body, 1, request->body_len, stdout) != request->body_len || putchar('\n') == EOF ||
        fflush(stdout) != 0) {
        return UPLOAD_NO_ANSWER;
    }
    (void)snprintf(answer, answer_cap, "%s", UPLOAD_OK_MARK);
    return UPLOAD_HTTP_OK;
}

int main(int argc, char **argv) {
    options opts = {0};
    if (!parse_options(argc, argv, &opts)) {
        usage();
        return 2;
    }

    size_t image_len = 0;
    uint8_t *image = read_file(opts.path, &image_len);
    if (!image) {
        (void)fprintf(stderr, "wii2posthog: cannot read %s\n", opts.path);
        return 1;
    }
    session_list sessions = {0};
    playlog_each_session(image, image_len, collect, &sessions);
    free(image);
    if (sessions.count > 0) {
        qsort(sessions.items, sessions.count, sizeof *sessions.items, by_start);
    }

    events_config config = {
        .api_key = opts.dry_run ? "phc_dry_run" : opts.api_key,
        .utc_offset_minutes = opts.utc_offset_minutes,
    };
    static upload batch;
    upload_begin(&batch, &config, opts.host, opts.dry_run ? print_body : http_post);
    for (size_t i = 0; i < sessions.count; i++) {
        char line[SESSION_SUMMARY_CAP];
        session_summary(&sessions.items[i], line);
        (void)fprintf(stderr, "%s\n", line);
        upload_add(&batch, &sessions.items[i]);
    }
    bool delivered = upload_end(&batch);
    free(sessions.items);

    (void)fprintf(stderr, "sessions found: %zu\n", sessions.count);
    if (!delivered) {
        if (opts.dry_run) {
            (void)fprintf(stderr, "wii2posthog: cannot write to stdout after %zu sessions\n", batch.sent);
        } else if (batch.status == UPLOAD_NO_ANSWER) {
            (void)fprintf(stderr, "wii2posthog: no answer from %s after %zu sessions\n", opts.host, batch.sent);
        } else if (batch.status == UPLOAD_NOT_CAPTURE) {
            (void)fprintf(stderr, "wii2posthog: %s answered, but not as PostHog capture does\n", opts.host);
        } else {
            (void)fprintf(stderr, "wii2posthog: %s answered %ld after %zu sessions\n", opts.host, batch.status,
                          batch.sent);
        }
        return 1;
    }
    if (!opts.dry_run) {
        (void)fprintf(stderr, "sessions sent: %zu\n", batch.sent);
    }
    return 0;
}
