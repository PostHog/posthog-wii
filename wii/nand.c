#include "nand.h"

#include <malloc.h>
#include <stdio.h>

#include <gccore.h>
#include <ogc/isfs.h>

enum { CHUNK_SIZE = 0x8000, IOS_ALIGNMENT = 32, FAILURE_CAP = 64 };

static const char MESSAGE_BOARD_PATH[] = "/title/00000001/00000002/data/cdb.vff";

/* IOS reads its arguments from 32-byte aligned memory. */
static fstats stats ATTRIBUTE_ALIGN(IOS_ALIGNMENT);

static s32 error;

static uint8_t *read_all(s32 fd, size_t *len) {
    error = ISFS_GetFileStats(fd, &stats);
    if (error < 0 || stats.file_length == 0) {
        return NULL;
    }
    size_t size = stats.file_length;
    uint8_t *image = memalign(IOS_ALIGNMENT, (size + IOS_ALIGNMENT - 1) / IOS_ALIGNMENT * IOS_ALIGNMENT);
    if (!image) {
        error = ISFS_ENOMEM;
        return NULL;
    }
    for (size_t got = 0; got < size;) {
        size_t want = size - got < CHUNK_SIZE ? size - got : CHUNK_SIZE;
        s32 n = ISFS_Read(fd, image + got, want);
        if (n <= 0) {
            error = n;
            free(image);
            return NULL;
        }
        got += (size_t)n;
    }
    *len = size;
    return image;
}

uint8_t *nand_read_message_board(size_t *len) {
    uint8_t *image = NULL;
    error = ISFS_Initialize();
    if (error < 0) {
        return NULL;
    }
    s32 fd = ISFS_Open(MESSAGE_BOARD_PATH, ISFS_OPEN_READ);
    if (fd >= 0) {
        image = read_all(fd, len);
        ISFS_Close(fd);
    } else {
        error = fd;
    }
    ISFS_Deinitialize();
    return image;
}

const char *nand_failure(void) {
    static char text[FAILURE_CAP];
    snprintf(text, sizeof text, "Could not read the Message Board file (ISFS error %d).", (int)error);
    return text;
}
