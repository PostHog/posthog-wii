#ifndef BYTES_H
#define BYTES_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t len;
} bytes;

/* A read past the end of the buffer returns 0, and a range past the end is
   empty. A parser relies on this in place of an error check per read. */
static inline bytes bytes_at(bytes buffer, size_t offset, size_t len) {
    if (offset > buffer.len || len > buffer.len - offset) {
        return (bytes){0};
    }
    return (bytes){buffer.data + offset, len};
}

static inline uint8_t u8_at(bytes buffer, size_t offset) {
    return offset < buffer.len ? buffer.data[offset] : 0;
}

static inline uint16_t be16_at(bytes buffer, size_t offset) {
    bytes pair = bytes_at(buffer, offset, 2);
    return pair.len ? (uint16_t)((unsigned)pair.data[0] << CHAR_BIT | pair.data[1]) : 0;
}

static inline uint32_t be32_at(bytes buffer, size_t offset) {
    return (uint32_t)be16_at(buffer, offset) << (2U * CHAR_BIT) | be16_at(buffer, offset + 2);
}

static inline uint64_t be64_at(bytes buffer, size_t offset) {
    return (uint64_t)be32_at(buffer, offset) << (4U * CHAR_BIT) | be32_at(buffer, offset + 4);
}

static inline uint16_t le16_at(bytes buffer, size_t offset) {
    bytes pair = bytes_at(buffer, offset, 2);
    return pair.len ? (uint16_t)((unsigned)pair.data[1] << CHAR_BIT | pair.data[0]) : 0;
}

static inline uint32_t le32_at(bytes buffer, size_t offset) {
    return (uint32_t)le16_at(buffer, offset + 2) << (2U * CHAR_BIT) | le16_at(buffer, offset);
}

static inline void put_be(uint8_t *out, uint64_t value, size_t size) {
    for (size_t i = 0; i < size; i++) {
        out[i] = (uint8_t)(value >> (CHAR_BIT * (size - 1 - i)));
    }
}

#endif
