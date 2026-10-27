#ifndef CHECK_H
#define CHECK_H

#include <stdbool.h>
#include <stdio.h>

static int failures;

static void check(bool passed, const char *file, int line, const char *condition) {
    if (!passed) {
        (void)fprintf(stderr, "%s:%d: CHECK(%s) failed\n", file, line, condition);
        failures++;
    }
}

#define CHECK(condition) check((condition), __FILE__, __LINE__, #condition)

static int report(void) {
    if (failures > 0) {
        (void)fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    (void)puts("ok");
    return 0;
}

#endif
