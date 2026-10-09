#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "events.h"
#include "playlog.h"
#include "send.h"
#include "summary.h"

enum { SESSIONS_PER_REQUEST = 50 };

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

static void usage(void) {
    fputs("usage: wii2posthog <cdb.vff> [--utc-offset +HH:MM] --dry-run\n"
          "       wii2posthog <cdb.vff> [--utc-offset +HH:MM] --host <url> --api-key <key>\n",
          stderr);
}

static bool parse_utc_offset(const char *text, int *minutes) {
    int hours = 0, mins = 0;
    char sign = 0, end = 0;
    if (sscanf(text, "%c%2d:%2d%c", &sign, &hours, &mins, &end) != 3 || (sign != '+' && sign != '-') ||
        hours > 14 || mins > 59) {
        return false;
    }
    *minutes = (sign == '-' ? -1 : 1) * (hours * 60 + mins);
    return true;
}

static bool parse_options(int argc, char **argv, options *opts) {
    for (int i = 1; i < argc; i++) {
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
        } else if (strcmp(arg, "--utc-offset") == 0 && value && parse_utc_offset(value, &opts->utc_offset_minutes)) {
            i++;
        } else if (arg[0] != '-' && !opts->path) {
            opts->path = arg;
        } else {
            return false;
        }
    }
    return opts->path && (opts->dry_run || (opts->host && opts->api_key));
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    uint8_t *data = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        rewind(file);
        if (size > 0 && (data = malloc((size_t)size)) != NULL) {
            *len = fread(data, 1, (size_t)size, file);
        }
    }
    fclose(file);
    return data;
}

static void collect(void *ctx, const playlog_session *session) {
    session_list *list = ctx;
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 256;
        list->items = realloc(list->items, list->cap * sizeof *list->items);
        if (!list->items) {
            perror("wii2posthog");
            exit(1);
        }
    }
    list->items[list->count++] = *session;
}

static int by_start(const void *a, const void *b) {
    uint64_t left = ((const playlog_session *)a)->start_ticks;
    uint64_t right = ((const playlog_session *)b)->start_ticks;
    return (left > right) - (left < right);
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
        fprintf(stderr, "wii2posthog: cannot read %s\n", opts.path);
        return 1;
    }

    session_list sessions = {0};
    playlog_each_session(image, image_len, collect, &sessions);
    free(image);
    if (sessions.count > 0) {
        qsort(sessions.items, sessions.count, sizeof *sessions.items, by_start);
    }
    for (size_t i = 0; i < sessions.count; i++) {
        char line[SESSION_SUMMARY_CAP];
        session_summary(&sessions.items[i], line);
        fprintf(stderr, "%s\n", line);
    }
    fprintf(stderr, "sessions found: %zu\n", sessions.count);

    events_config config = {
        .api_key = opts.api_key ? opts.api_key : "phc_dry_run",
        .utc_offset_minutes = opts.utc_offset_minutes,
    };
    static char json[EVENTS_JSON_CAP(SESSIONS_PER_REQUEST)];
    for (size_t sent = 0; sent < sessions.count; sent += SESSIONS_PER_REQUEST) {
        size_t count = sessions.count - sent < SESSIONS_PER_REQUEST ? sessions.count - sent : SESSIONS_PER_REQUEST;
        size_t json_len = events_batch_json(&config, sessions.items + sent, count, json, sizeof json);
        if (opts.dry_run) {
            puts(json);
            continue;
        }
        long status = send_batch(opts.host, json, json_len);
        if (status != 200) {
            fprintf(stderr, "wii2posthog: %s answered %ld after %zu sessions\n", opts.host, status, sent);
            return 1;
        }
        fprintf(stderr, "sent %zu of %zu sessions\n", sent + count, sessions.count);
    }
    free(sessions.items);
    return 0;
}
