#include "vff.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    FAT_ENTRY_SIZE = 2,
    FIRST_DATA_CLUSTER = 2,
    VISITED_SIZE = ((FAT16_MAX_CLUSTERS + FIRST_DATA_CLUSTER) / CHAR_BIT) + 1,

    DIRENT_END = 0x00,
    DIRENT_DELETED = 0xE5,

    /* A window holds 16 directory entries or 256 FAT entries. */
    WINDOW_SIZE = 0x200,
};

static const uint8_t ATTR_VOLUME = 0x08;
static const uint8_t ATTR_DIRECTORY = 0x10;

/* A copy of part of the image. A run of small reads that falls inside it
   costs one read of the image. */
typedef struct {
    size_t offset;
    size_t len;
    uint8_t data[WINDOW_SIZE];
} window;

typedef struct {
    const vff_image *image;
    size_t fat_size;
    size_t root_offset;
    size_t data_offset;
    uint32_t cluster_size;
    uint32_t cluster_end;
    window fat;
    window directory;
    uint8_t *visited;
    uint8_t *scratch;
    size_t scratch_cap;
    vff_file_fn *on_file;
    void *ctx;
    bool read_failed;
} volume;

/* The place the walk has reached in one directory. */
typedef struct {
    uint32_t cluster; /* 0 in the root directory, which is not a cluster chain */
    size_t offset;    /* of the next entry in the image */
    size_t end;       /* of the cluster, or of the root directory */
} directory_position;

static bool in_image(const volume *vol, size_t offset, size_t len) {
    return (offset <= vol->image->len && len <= vol->image->len - offset) != 0;
}

/* The one place the walk reads the image, so the reader never sees a range
   outside it. */
static bool read_image(volume *vol, size_t offset, uint8_t *out, size_t len) {
    if (!in_image(vol, offset, len)) {
        return false;
    }
    if (len > 0 && !vol->image->read(vol->image->source, offset, out, len)) {
        vol->read_failed = true;
        return false;
    }
    return true;
}

/* Returns len bytes, at most WINDOW_SIZE, at offset in the image. A range
   that the image does not hold, or a failed read, gives an empty range. */
static bytes window_at(volume *vol, window *view, size_t offset, size_t len) {
    size_t start = offset - view->offset;
    if (offset < view->offset || start > view->len || len > view->len - start) {
        size_t rest = offset < vol->image->len ? vol->image->len - offset : 0;
        view->offset = offset;
        view->len = rest < WINDOW_SIZE ? rest : WINDOW_SIZE;
        if (!read_image(vol, offset, view->data, view->len)) {
            view->len = 0;
        }
        start = 0;
    }
    return bytes_at((bytes){view->data, view->len}, start, len);
}

static bool volume_open(volume *vol) {
    uint8_t header_data[HEADER_SIZE];
    if (!read_image(vol, 0, header_data, sizeof header_data)) {
        return false;
    }
    bytes header = {header_data, sizeof header_data};
    if (be32_at(header, HEADER_MAGIC) != VFF_MAGIC || be16_at(header, HEADER_BYTE_ORDER) != VFF_BYTE_ORDER) {
        return false;
    }
    uint32_t volume_size = be32_at(header, HEADER_VOLUME_SIZE);
    uint32_t cluster_size = (uint32_t)be16_at(header, HEADER_CLUSTER_SIZE) * CLUSTER_SIZE_UNIT;
    if (cluster_size < DIRENT_SIZE) {
        return false;
    }
    /* The reader rejects a volume that claims more bytes than the image has.
       Every cluster offset then stays below twice the image size, so the
       offset arithmetic cannot wrap on the Wii's 32-bit size_t. */
    uint32_t cluster_count = volume_size / cluster_size;
    if (volume_size > vol->image->len || cluster_count < FAT16_MIN_CLUSTERS || cluster_count >= FAT16_MAX_CLUSTERS) {
        return false;
    }
    vol->fat_size = (((size_t)cluster_count * FAT_ENTRY_SIZE) + cluster_size - 1) / cluster_size * cluster_size;
    vol->root_offset = HEADER_SIZE + (2 * vol->fat_size);
    vol->data_offset = vol->root_offset + ROOT_SIZE;
    vol->cluster_size = cluster_size;
    vol->cluster_end = cluster_count + FIRST_DATA_CLUSTER;
    return in_image(vol, HEADER_SIZE, vol->fat_size);
}

/* A number outside the volume ends a chain, as an end-of-chain mark does. */
static bool is_data_cluster(const volume *vol, uint32_t cluster) {
    return (cluster >= FIRST_DATA_CLUSTER && cluster < vol->cluster_end) != 0;
}

static size_t cluster_offset(const volume *vol, uint32_t cluster) {
    return vol->data_offset + ((size_t)(cluster - FIRST_DATA_CLUSTER) * vol->cluster_size);
}

/* A cluster that ends past the image counts as missing. */
static bool cluster_in_image(const volume *vol, uint32_t cluster) {
    return in_image(vol, cluster_offset(vol, cluster), vol->cluster_size);
}

static uint32_t next_cluster(volume *vol, uint32_t cluster) {
    size_t entry = (size_t)cluster * FAT_ENTRY_SIZE;
    if (entry >= vol->fat_size) {
        return 0;
    }
    return le16_at(window_at(vol, &vol->fat, HEADER_SIZE + entry, FAT_ENTRY_SIZE), 0);
}

/* Copies the start of the entry's file to scratch. Returns the bytes copied. */
static size_t read_head(volume *vol, bytes entry) {
    uint32_t file_size = le32_at(entry, DIRENT_FILE_SIZE);
    size_t want = file_size < vol->scratch_cap ? file_size : vol->scratch_cap;
    size_t got = 0;
    uint32_t cluster = le16_at(entry, DIRENT_FIRST_CLUSTER);
    while (got < want && is_data_cluster(vol, cluster) && cluster_in_image(vol, cluster)) {
        /* A run of consecutive clusters costs one read of the image. */
        size_t run_len = vol->cluster_size;
        uint32_t next = next_cluster(vol, cluster);
        for (uint32_t last = cluster;
             got + run_len < want && next == last + 1 && is_data_cluster(vol, next) && cluster_in_image(vol, next);
             next = next_cluster(vol, last)) {
            last = next;
            run_len += vol->cluster_size;
        }
        size_t chunk_len = run_len < want - got ? run_len : want - got;
        if (!read_image(vol, cluster_offset(vol, cluster), vol->scratch + got, chunk_len)) {
            break;
        }
        got += chunk_len;
        cluster = next;
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
    if (!is_data_cluster(vol, cluster) || !first_visit(vol, cluster) || !cluster_in_image(vol, cluster)) {
        return false;
    }
    position->cluster = cluster;
    position->offset = cluster_offset(vol, cluster);
    position->end = position->offset + vol->cluster_size;
    return true;
}

/* Returns the next entry, or an empty range at the end of the directory.
   The entry stays valid until the next call. */
static bytes next_entry(volume *vol, directory_position *position) {
    if (position->offset + DIRENT_SIZE > position->end &&
        (!is_data_cluster(vol, position->cluster) ||
         !enter_cluster(vol, position, next_cluster(vol, position->cluster)))) {
        return (bytes){0};
    }
    bytes entry = window_at(vol, &vol->directory, position->offset, DIRENT_SIZE);
    position->offset += DIRENT_SIZE;
    return u8_at(entry, DIRENT_NAME) == DIRENT_END ? (bytes){0} : entry;
}

static void walk_volume(volume *vol) {
    directory_position path[MAX_DEPTH + 1] = {
        {.offset = vol->root_offset, .end = vol->root_offset + ROOT_SIZE},
    };
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

bool vff_each_file(const vff_image *image, uint8_t *scratch, size_t scratch_cap, vff_file_fn *on_file, void *ctx) {
    uint8_t visited[VISITED_SIZE] = {0};
    volume vol = {
        .image = image,
        .visited = visited,
        .scratch = scratch,
        .scratch_cap = scratch_cap,
        .on_file = on_file,
        .ctx = ctx,
    };
    if (volume_open(&vol)) {
        walk_volume(&vol);
    }
    return (!vol.read_failed) != 0;
}
