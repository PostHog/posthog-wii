#include "vff.h"

#include <stdbool.h>
#include <string.h>

#include "bytes.h"

/* Layout: https://wiibrew.org/wiki/VFF. A 0x20-byte big-endian header, two
   copies of the FAT, a fixed root directory, then the data clusters. The
   FAT and the directory entries are little-endian, as in standard FAT. */
enum {
    VFF_MAGIC = 0x56464620,
    VFF_BYTE_ORDER = 0xFEFF,
    HEADER_SIZE = 0x20,
    DIRENT_SIZE = 0x20,
    ROOT_ENTRIES = 128,
    MAX_DEPTH = 16,

    /* The Message Board volume is always 20 MB, which makes it FAT16.
       Smaller VFF volumes use FAT12 and are not read. */
    FAT16_MIN_CLUSTERS = 0xFF5,
    FAT16_MAX_CLUSTERS = 0xFFF5,
    FAT16_END_OF_CHAIN = 0xFFF8,
    VISITED_SIZE = FAT16_END_OF_CHAIN / 8 + 1,

    DIRENT_END = 0x00,
    DIRENT_DELETED = 0xE5,
    ATTR_VOLUME = 0x08,
    ATTR_DIRECTORY = 0x10,
};

typedef struct {
    bytes image;
    bytes fat;
    bytes root;
    size_t data_offset;
    uint32_t cluster_size;
    uint8_t *visited;
    uint8_t *scratch;
    size_t scratch_cap;
    vff_file_fn *fn;
    void *ctx;
} volume;

static bool volume_open(volume *v, bytes image) {
    if (be32_at(image, 0x00) != VFF_MAGIC || be16_at(image, 0x04) != VFF_BYTE_ORDER) {
        return false;
    }
    uint32_t volume_size = be32_at(image, 0x08);
    uint32_t cluster_size = (uint32_t)be16_at(image, 0x0C) << 4;
    if (cluster_size < DIRENT_SIZE) {
        return false;
    }
    uint32_t cluster_count = volume_size / cluster_size;
    if (cluster_count < FAT16_MIN_CLUSTERS || cluster_count >= FAT16_MAX_CLUSTERS) {
        return false;
    }
    size_t fat_size = (cluster_count * 2 + cluster_size - 1) / cluster_size * cluster_size;
    size_t root_offset = HEADER_SIZE + 2 * fat_size;

    v->image = image;
    v->fat = bytes_at(image, HEADER_SIZE, fat_size);
    v->root = bytes_at(image, root_offset, ROOT_ENTRIES * DIRENT_SIZE);
    v->data_offset = root_offset + ROOT_ENTRIES * DIRENT_SIZE;
    v->cluster_size = cluster_size;
    return v->fat.len != 0;
}

static bool is_data_cluster(uint32_t cluster) {
    return cluster >= 2 && cluster < FAT16_END_OF_CHAIN;
}

static bytes cluster_at(const volume *v, uint32_t cluster) {
    return bytes_at(v->image, v->data_offset + (size_t)(cluster - 2) * v->cluster_size, v->cluster_size);
}

static uint32_t next_cluster(const volume *v, uint32_t cluster) {
    return le16_at(v->fat, (size_t)cluster * 2);
}

static size_t read_head(const volume *v, uint32_t first_cluster, uint32_t file_size) {
    size_t want = file_size < v->scratch_cap ? file_size : v->scratch_cap;
    size_t got = 0;
    for (uint32_t cluster = first_cluster; got < want && is_data_cluster(cluster); cluster = next_cluster(v, cluster)) {
        bytes chunk = cluster_at(v, cluster);
        if (chunk.len == 0) {
            break;
        }
        size_t n = chunk.len < want - got ? chunk.len : want - got;
        memcpy(v->scratch + got, chunk.data, n);
        got += n;
    }
    return got;
}

/* A corrupt FAT or directory can link directories into a loop. Reading
   each directory cluster once keeps the walk linear in the image size. */
static bool first_visit(const volume *v, uint32_t cluster) {
    uint8_t bit = (uint8_t)(1u << (cluster % 8));
    bool unseen = !(v->visited[cluster / 8] & bit);
    v->visited[cluster / 8] |= bit;
    return unseen;
}

static void walk_directory(const volume *v, uint32_t first_cluster, int depth);

/* Returns false at the end-of-directory marker. */
static bool visit_entries(const volume *v, bytes block, int depth) {
    for (size_t offset = 0; offset + DIRENT_SIZE <= block.len; offset += DIRENT_SIZE) {
        bytes entry = bytes_at(block, offset, DIRENT_SIZE);
        uint8_t first = u8_at(entry, 0);
        uint8_t attributes = u8_at(entry, 11);
        if (first == DIRENT_END) {
            return false;
        }
        /* A long-name entry has the volume bit set, so the volume test
           skips it too. */
        if (first == DIRENT_DELETED || (attributes & ATTR_VOLUME)) {
            continue;
        }
        uint32_t first_cluster = le16_at(entry, 26);
        if (attributes & ATTR_DIRECTORY) {
            walk_directory(v, first_cluster, depth + 1);
        } else {
            size_t head_len = read_head(v, first_cluster, le32_at(entry, 28));
            v->fn(v->ctx, v->scratch, head_len);
        }
    }
    return true;
}

static void walk_directory(const volume *v, uint32_t first_cluster, int depth) {
    if (depth > MAX_DEPTH) {
        return;
    }
    for (uint32_t cluster = first_cluster; is_data_cluster(cluster) && first_visit(v, cluster);
         cluster = next_cluster(v, cluster)) {
        bytes block = cluster_at(v, cluster);
        if (block.len == 0 || !visit_entries(v, block, depth)) {
            return;
        }
    }
}

void vff_each_file(const uint8_t *image, size_t image_len,
                   uint8_t *scratch, size_t scratch_cap,
                   vff_file_fn *fn, void *ctx) {
    uint8_t visited[VISITED_SIZE] = {0};
    volume v = {.visited = visited, .scratch = scratch, .scratch_cap = scratch_cap, .fn = fn, .ctx = ctx};
    if (volume_open(&v, (bytes){image, image_len})) {
        visit_entries(&v, v.root, 0);
    }
}
