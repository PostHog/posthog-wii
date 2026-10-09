#include "sha1.h"

#include <string.h>

enum { BLOCK_SIZE = 64, LENGTH_FIELD_SIZE = 8 };

static uint32_t rotate_left(uint32_t x, int n) {
    return x << n | x >> (32 - n);
}

static void compress(uint32_t state[5], const uint8_t block[BLOCK_SIZE]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        const uint8_t *p = block + 4 * i;
        w[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = rotate_left(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t next = rotate_left(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotate_left(b, 30);
        b = a;
        a = next;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1(const uint8_t *data, size_t len, uint8_t out[SHA1_SIZE]) {
    uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    size_t whole = len / BLOCK_SIZE * BLOCK_SIZE;
    for (size_t offset = 0; offset < whole; offset += BLOCK_SIZE) {
        compress(state, data + offset);
    }

    uint8_t tail[2 * BLOCK_SIZE] = {0};
    size_t rest = len - whole;
    memcpy(tail, data + whole, rest);
    tail[rest] = 0x80;
    size_t tail_len = rest < BLOCK_SIZE - LENGTH_FIELD_SIZE ? BLOCK_SIZE : 2 * BLOCK_SIZE;
    uint64_t bit_count = (uint64_t)len * 8;
    for (int i = 0; i < LENGTH_FIELD_SIZE; i++) {
        tail[tail_len - 1 - (size_t)i] = (uint8_t)(bit_count >> (8 * i));
    }
    for (size_t offset = 0; offset < tail_len; offset += BLOCK_SIZE) {
        compress(state, tail + offset);
    }

    for (int i = 0; i < 5; i++) {
        out[4 * i] = (uint8_t)(state[i] >> 24);
        out[4 * i + 1] = (uint8_t)(state[i] >> 16);
        out[4 * i + 2] = (uint8_t)(state[i] >> 8);
        out[4 * i + 3] = (uint8_t)state[i];
    }
}
