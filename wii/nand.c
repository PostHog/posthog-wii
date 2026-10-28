#include "nand.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <gccore.h>
#include <ogc/isfs.h>

#include "playlog.h"
#include "vff.h"

enum { CHUNK_SIZE = 0x1000, IOS_ALIGNMENT = 32, FAILURE_CAP = 64 };

static const char MESSAGE_BOARD_PATH[] = "/title/00000001/00000002/data/cdb.vff";

/* IOS reads its arguments from 32-byte aligned memory and writes the bytes
   of a read to 32-byte aligned memory. */
static fstats stats ATTRIBUTE_ALIGN(IOS_ALIGNMENT);
static uint8_t chunk[CHUNK_SIZE] ATTRIBUTE_ALIGN(IOS_ALIGNMENT);

static s32 error;

/* position is SIZE_MAX when a failed call left it unknown. Each IOS call is
   a round trip, so a read that continues the last one skips the seek. */
typedef struct {
    s32 file;
    size_t position;
} open_file;

/* /dev/fs numbers the whence values of a seek as stdio does, with 0 for the
   start of the file (Dolphin, Source/Core/Core/IOS/FS/FileSystem.h). */
static bool read_at(void *source, size_t offset, uint8_t *out, size_t len) {
    open_file *open = source;
    if (open->position != offset) {
        open->position = SIZE_MAX;
        s32 position = ISFS_Seek(open->file, (s32)offset, SEEK_SET);
        if (position < 0) {
            error = position;
            return false;
        }
    }
    open->position = SIZE_MAX;
    for (size_t done = 0; done < len;) {
        size_t want = len - done < CHUNK_SIZE ? len - done : CHUNK_SIZE;
        s32 got = ISFS_Read(open->file, chunk, want);
        if (got <= 0) {
            /* A read of 0 bytes means the file ended before the walk expected. */
            error = got < 0 ? got : ISFS_EINVAL;
            return false;
        }
        memcpy(out + done, chunk, (size_t)got);
        done += (size_t)got;
    }
    open->position = offset + len;
    return true;
}

bool nand_each_session(playlog_session_fn *on_session, void *ctx) {
    error = ISFS_Initialize();
    if (error < 0) {
        return false;
    }
    s32 file = ISFS_Open(MESSAGE_BOARD_PATH, ISFS_OPEN_READ);
    error = file < 0 ? file : ISFS_GetFileStats(file, &stats);
    bool read_whole = false;
    if (error >= 0) {
        open_file open = {file, 0};
        const vff_image image = {read_at, &open, stats.file_length};
        read_whole = playlog_each_session(&image, on_session, ctx);
    }
    if (file >= 0) {
        ISFS_Close(file);
    }
    ISFS_Deinitialize();
    return read_whole;
}

const char *nand_failure(void) {
    static char text[FAILURE_CAP];
    (void)snprintf(text, sizeof text, "Could not read the Message Board file (ISFS error %d).", (int)error);
    return text;
}
