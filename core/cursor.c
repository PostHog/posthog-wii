#include "cursor.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "sha1.h"

enum { TARGET_BYTES = 8, TARGET_CAP = TARGET_BYTES * 2 + 1 };

/* A cursor made for one project must not hide sessions from another, so the
   text names its host and token by a hash of both. */
static void target(const char *host, const char *api_key, char out[TARGET_CAP]) {
    char both[2 * SETTINGS_VALUE_CAP + 1];
    int len = snprintf(both, sizeof both, "%s\n%s", host, api_key);
    uint8_t digest[SHA1_SIZE];
    sha1((const uint8_t *)both, len < (int)sizeof both ? (size_t)len : sizeof both - 1, digest);
    for (int i = 0; i < TARGET_BYTES; i++) {
        snprintf(out + i * 2, 3, "%02x", (unsigned)digest[i]);
    }
}

uint64_t cursor_read(const char *text, size_t len, const char *host, const char *api_key) {
    char line[CURSOR_TEXT_CAP] = "";
    memcpy(line, text, len < sizeof line - 1 ? len : sizeof line - 1);

    char expected[TARGET_CAP];
    target(host, api_key, expected);
    char found[TARGET_CAP];
    unsigned long long start_ticks;
    if (sscanf(line, "%16s %llu", found, &start_ticks) != 2 || strcmp(found, expected) != 0) {
        return 0;
    }
    return start_ticks;
}

size_t cursor_write(char out[CURSOR_TEXT_CAP], const char *host, const char *api_key, uint64_t start_ticks) {
    char name[TARGET_CAP];
    target(host, api_key, name);
    return (size_t)snprintf(out, CURSOR_TEXT_CAP, "%s %llu\n", name, (unsigned long long)start_ticks);
}
