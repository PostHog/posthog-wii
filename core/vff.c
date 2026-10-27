#include "vff.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bytes.h"

/* Layout: https://wiibrew.org/wiki/VFF. A 0x20-byte big-endian header, two
   copies of the FAT, a fixed root directory, then the data clusters. The
   FAT and the directory entries are little-endian, as in standard FAT. */
enum {
    VFF_MAGIC = 0x56464620,
    VFF_BYTE_ORDER = 0xFEFF,
    HEADER_MAGIC = 0x00,
    HEADER_BYTE_ORDER = 0x04,
    HEADER_VOLUME_SIZE = 0x08,
    HEADER_CLUSTER_SIZE = 0x0C,
    HEADER_SIZE = 0x20,
    CLUSTER_SIZE_UNIT = 16,

    DIRENT_NAME = 0x00,
    DIRENT_ATTRIBUTES = 0x0B,
    DIRENT_FIRST_CLUSTER = 0x1A,
    DIRENT_FILE_SIZE = 0x1C,
    DIRENT_SIZE = 0x20,
    ROOT_ENTRIES = 128,
    ROOT_SIZE = ROOT_ENTRIES * DIRENT_SIZE,
    MAX_DEPTH = 16,

    /* The Message Board volume is always 20 MB, which makes it FAT16.
       Smaller VFF volumes use FAT12, which this file does not read. */
    FAT16_MIN_CLUSTERS = 0xFF5,
    FAT16_MAX_CLUSTERS = 0xFFF5,
    FIRST_DATA_CLUSTER = 2,
    VISITED_SIZE = ((FAT16_MAX_CLUSTERS + FIRST_DATA_CLUSTER) / CHAR_BIT) + 1,

    DIRENT_END = 0x00,
    DIRENT_DELETED = 0xE5,
};

static const uint8_t ATTR_VOLUME = 0x08;
static const uint8_t ATTR_DIRECTORY = 0x10;

typedef struct {
    bytes image;
    bytes fat;
    bytes root;
    size_t data_offset;
    uint32_t cluster_size;
    uint32_t cluster_end;
    uint8_t *visited;
    uint8_t *scratch;
    size_t scratch_cap;
    vff_file_fn *on_file;
    void *ctx;
} volume;

/* The place the walk has reached in one directory. */
typedef struct {
    uint32_t cluster; /* 0 in the root directory, which is not a cluster chain */
    bytes block;
    size_t offset;
} directory_position;

static bool volume_open(volume *vol, bytes image) {
    if (be32_at(image, HEADER_MAGIC) != VFF_MAGIC || be16_at(image, HEADER_BYTE_ORDER) != VFF_BYTE_ORDER) {
        return false;
    }
    uint32_t volume_size = be32_at(image, HEADER_VOLUME_SIZE);
    uint32_t cluster_size = (uint32_t)be16_at(image, HEADER_CLUSTER_SIZE) * CLUSTER_SIZE_UNIT;
    if (cluster_size < DIRENT_SIZE) {
        return false;
    }
    /* The reader rejects a volume that claims more bytes than the image has.
       Every cluster offset then stays below twice the image size, so the
       offset arithmetic cannot wrap on the Wii's 32-bit size_t. */
    uint32_t cluster_count = volume_size / cluster_size;
    if (volume_size > image.len || cluster_count < FAT16_MIN_CLUSTERS || cluster_count >= FAT16_MAX_CLUSTERS) {
        return false;
    }
    size_t fat_size = (((size_t)cluster_count * 2) + cluster_size - 1) / cluster_size * cluster_size;
    size_t root_offset = HEADER_SIZE + (2 * fat_size);

    vol->image = image;
    vol->fat = bytes_at(image, HEADER_SIZE, fat_size);
    vol->root = bytes_at(image, root_offset, ROOT_SIZE);
    vol->data_offset = root_offset + ROOT_SIZE;
    vol->cluster_size = cluster_size;
    vol->cluster_end = cluster_count + FIRST_DATA_CLUSTER;
    return vol->fat.len != 0;
}

/* A number outside the volume ends a chain, as an end-of-chain mark does. */
static bool is_data_cluster(const volume *vol, uint32_t cluster) {
    return (cluster >= FIRST_DATA_CLUSTER && cluster < vol->cluster_end) != 0;
}

static bytes cluster_at(const volume *vol, uint32_t cluster) {
    return bytes_at(vol->image, vol->data_offset + ((size_t)(cluster - FIRST_DATA_CLUSTER) * vol->cluster_size),
                    vol->cluster_size);
}

static uint32_t next_cluster(const volume *vol, uint32_t cluster) {
    return le16_at(vol->fat, (size_t)cluster * 2);
}

/* Copies the start of the entry's file to scratch. Returns the bytes copied. */
static size_t read_head(const volume *vol, bytes entry) {
    uint32_t file_size = le32_at(entry, DIRENT_FILE_SIZE);
    size_t want = file_size < vol->scratch_cap ? file_size : vol->scratch_cap;
    size_t got = 0;
    for (uint32_t cluster = le16_at(entry, DIRENT_FIRST_CLUSTER); got < want && is_data_cluster(vol, cluster);
         cluster = next_cluster(vol, cluster)) {
        bytes chunk = cluster_at(vol, cluster);
        if (chunk.len == 0) {
            break;
        }
        size_t chunk_len = chunk.len < want - got ? chunk.len : want - got;
        memcpy(vol->scratch + got, chunk.data, chunk_len);
        got += chunk_len;
    }
    return got;
}

/* A corrupt FAT or directory can link directories into a loop. Reading
   each directory cluster once keeps the walk linear in the image size. */
static bool first_visit(const volume *vol, uint32_t cluster) {
    uint8_t bit = (uint8_t)(1U << (cluster % CHAR_BIT));
    bool unseen = (!(vol->visited[cluster / CHAR_BIT] & bit)) != 0;
    vol->visited[cluster / CHAR_BIT] |= bit;
    return unseen;
}

/* Moves to the start of a directory cluster. Returns false when the chain
   has ended there. */
static bool enter_cluster(const volume *vol, directory_position *position, uint32_t cluster) {
    if (!is_data_cluster(vol, cluster) || !first_visit(vol, cluster)) {
        return false;
    }
    position->cluster = cluster;
    position->block = cluster_at(vol, cluster);
    position->offset = 0;
    return position->block.len != 0;
}

/* Returns the next entry, or an empty range at the end of the directory. */
static bytes next_entry(const volume *vol, directory_position *position) {
    if (position->offset + DIRENT_SIZE > position->block.len &&
        (!is_data_cluster(vol, position->cluster) ||
         !enter_cluster(vol, position, next_cluster(vol, position->cluster)))) {
        return (bytes){0};
    }
    bytes entry = bytes_at(position->block, position->offset, DIRENT_SIZE);
    position->offset += DIRENT_SIZE;
    return u8_at(entry, DIRENT_NAME) == DIRENT_END ? (bytes){0} : entry;
}

static void walk_volume(const volume *vol) {
    directory_position path[MAX_DEPTH + 1] = {{.block = vol->root}};
    size_t depth = 0;
    for (;;) {
        bytes entry = next_entry(vol, &path[depth]);
        if (entry.len == 0) {
            if (depth == 0) {
                return;
            }
            depth--;
            continue;
        }
        uint8_t attributes = u8_at(entry, DIRENT_ATTRIBUTES);
        /* A long-name entry has the volume bit set, so the volume test
           skips it too. */
        if (u8_at(entry, DIRENT_NAME) == DIRENT_DELETED || (attributes & ATTR_VOLUME)) {
            continue;
        }
        if (!(attributes & ATTR_DIRECTORY)) {
            size_t head_len = read_head(vol, entry);
            vol->on_file(vol->ctx, vol->scratch, head_len);
        } else if (depth < MAX_DEPTH && enter_cluster(vol, &path[depth + 1], le16_at(entry, DIRENT_FIRST_CLUSTER))) {
            depth++;
        }
    }
}

void vff_each_file(const uint8_t *image, size_t image_len, uint8_t *scratch, size_t scratch_cap, vff_file_fn *on_file,
                   void *ctx) {
    uint8_t visited[VISITED_SIZE] = {0};
    volume vol = {.visited = visited, .scratch = scratch, .scratch_cap = scratch_cap, .on_file = on_file, .ctx = ctx};
    if (volume_open(&vol, (bytes){image, image_len})) {
        walk_volume(&vol);
    }
}
