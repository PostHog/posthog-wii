#ifndef SHA1_H
#define SHA1_H

#include <stddef.h>
#include <stdint.h>

enum { SHA1_SIZE = 20 };

void sha1(const uint8_t *data, size_t len, uint8_t out[SHA1_SIZE]);

#endif
