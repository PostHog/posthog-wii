#include "sha1.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bytes.h"

/* FIPS 180-4, sections 5 and 6.1. */
enum {
    BLOCK_SIZE = 64,
    LENGTH_FIELD_SIZE = 8,
    WORD_SIZE = 4,
    BLOCK_WORDS = BLOCK_SIZE / WORD_SIZE,
    STATE_WORDS = SHA1_SIZE / WORD_SIZE,
    ROUNDS = 80,
    ROUNDS_PER_STAGE = 20,
};

static const unsigned WORD_BITS = WORD_SIZE * CHAR_BIT;
static const unsigned SCHEDULE_ROTATION = 1;
static const unsigned A_ROTATION = 5;
static const unsigned B_ROTATION = 30;
static const uint8_t PADDING_MARK = 0x80;

/* How far back each word of the message schedule reaches. */
static const size_t SCHEDULE_TAPS[] = {3, 8, 14, 16};

static const uint32_t STAGE_CONSTANTS[] = {0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6};
static const uint32_t INITIAL_STATE[STATE_WORDS] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

static uint32_t rotate_left(uint32_t word, unsigned count) {
    return word << count | word >> (WORD_BITS - count);
}

static void compress(uint32_t state[STATE_WORDS], const uint8_t block[BLOCK_SIZE]) {
    uint32_t schedule[ROUNDS];
    for (size_t i = 0; i < BLOCK_WORDS; i++) {
        schedule[i] = be32_at((bytes){block, BLOCK_SIZE}, WORD_SIZE * i);
    }
    for (size_t i = BLOCK_WORDS; i < ROUNDS; i++) {
        uint32_t mixed = 0;
        for (size_t tap = 0; tap < sizeof SCHEDULE_TAPS / sizeof SCHEDULE_TAPS[0]; tap++) {
            mixed ^= schedule[i - SCHEDULE_TAPS[tap]];
        }
        schedule[i] = rotate_left(mixed, SCHEDULE_ROTATION);
    }

    uint32_t word_a = state[0];
    uint32_t word_b = state[1];
    uint32_t word_c = state[2];
    uint32_t word_d = state[3];
    uint32_t word_e = state[4];
    for (int i = 0; i < ROUNDS; i++) {
        int stage = i / ROUNDS_PER_STAGE;
        uint32_t mixed = word_b ^ word_c ^ word_d;
        if (stage == 0) {
            mixed = (word_b & word_c) | (~word_b & word_d);
        } else if (stage == 2) {
            mixed = (word_b & word_c) | (word_b & word_d) | (word_c & word_d);
        }
        uint32_t next = rotate_left(word_a, A_ROTATION) + mixed + word_e + STAGE_CONSTANTS[stage] + schedule[i];
        word_e = word_d;
        word_d = word_c;
        word_c = rotate_left(word_b, B_ROTATION);
        word_b = word_a;
        word_a = next;
    }
    state[0] += word_a;
    state[1] += word_b;
    state[2] += word_c;
    state[3] += word_d;
    state[4] += word_e;
}

void sha1(const uint8_t *data, size_t len, uint8_t out[SHA1_SIZE]) {
    uint32_t state[STATE_WORDS];
    memcpy(state, INITIAL_STATE, sizeof state);

    size_t whole = len / BLOCK_SIZE * BLOCK_SIZE;
    for (size_t offset = 0; offset < whole; offset += BLOCK_SIZE) {
        compress(state, data + offset);
    }

    uint8_t tail[2 * BLOCK_SIZE] = {0};
    size_t rest = len - whole;
    memcpy(tail, data + whole, rest);
    tail[rest] = PADDING_MARK;
    size_t tail_len = rest < BLOCK_SIZE - LENGTH_FIELD_SIZE ? BLOCK_SIZE : 2 * BLOCK_SIZE;
    put_be(tail + tail_len - LENGTH_FIELD_SIZE, (uint64_t)len * CHAR_BIT, LENGTH_FIELD_SIZE);
    for (size_t offset = 0; offset < tail_len; offset += BLOCK_SIZE) {
        compress(state, tail + offset);
    }

    for (size_t i = 0; i < STATE_WORDS; i++) {
        put_be(out + (WORD_SIZE * i), state[i], WORD_SIZE);
    }
}
