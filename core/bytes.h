#ifndef BYTES_H
#define BYTES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t len;
} bytes;

/* A range or a read that leaves the buffer yields an empty range or 0. A
   truncated or corrupt image then fails the next magic check, so parsers
   need no error path per read. */
static inline bytes bytes_at(bytes b, size_t offset, size_t len) {
    if (offset > b.len || len > b.len - offset) {
        return (bytes){0};
    }
    return (bytes){b.data + offset, len};
}

static inline uint8_t u8_at(bytes b, size_t offset) {
    return offset < b.len ? b.data[offset] : 0;
}

static inline uint16_t be16_at(bytes b, size_t offset) {
    bytes s = bytes_at(b, offset, 2);
    return s.len ? (uint16_t)(s.data[0] << 8 | s.data[1]) : 0;
}

static inline uint32_t be32_at(bytes b, size_t offset) {
    return (uint32_t)be16_at(b, offset) << 16 | be16_at(b, offset + 2);
}

static inline uint64_t be64_at(bytes b, size_t offset) {
    return (uint64_t)be32_at(b, offset) << 32 | be32_at(b, offset + 4);
}

static inline uint16_t le16_at(bytes b, size_t offset) {
    bytes s = bytes_at(b, offset, 2);
    return s.len ? (uint16_t)(s.data[1] << 8 | s.data[0]) : 0;
}

static inline uint32_t le32_at(bytes b, size_t offset) {
    return (uint32_t)le16_at(b, offset + 2) << 16 | le16_at(b, offset);
}

#endif
