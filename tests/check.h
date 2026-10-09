#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>

static int failures;

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); \
            failures++;                                                                   \
        }                                                                                 \
    } while (0)

static int report(void) {
    if (failures) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    puts("ok");
    return 0;
}

#endif
