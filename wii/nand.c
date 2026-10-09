#include "nand.h"

#include <malloc.h>
#include <stdio.h>

#include <gccore.h>
#include <ogc/isfs.h>

/* IOS answers ACCESS_DENIED when the file's permissions exclude the title
   that runs the app. The number is IOS's, and libogc gives it no name. */
enum { CHUNK_SIZE = 0x8000, IOS_ALIGNMENT = 32, FAILURE_CAP = 96, ACCESS_DENIED = -102 };

static const char MESSAGE_BOARD_PATH[] = "/title/00000001/00000002/data/cdb.vff";

/* IOS reads its arguments from 32-byte aligned memory. */
static fstats stats ATTRIBUTE_ALIGN(IOS_ALIGNMENT);

static char failure[FAILURE_CAP];

static void fail(s32 error) {
    snprintf(failure, sizeof failure,
             error == ACCESS_DENIED ? "IOS did not let the app open the Message Board file\n(ISFS error %d)."
                                    : "Could not read the Message Board file (ISFS error %d).",
             (int)error);
}

static uint8_t *read_all(s32 fd, size_t *len) {
    s32 result = ISFS_GetFileStats(fd, &stats);
    if (result < 0) {
        fail(result);
        return NULL;
    }
    size_t size = stats.file_length;
    /* One block more than the file needs, so an empty file still gets a buffer. */
    uint8_t *image = memalign(IOS_ALIGNMENT, (size / IOS_ALIGNMENT + 1) * IOS_ALIGNMENT);
    if (!image) {
        fail(ISFS_ENOMEM);
        return NULL;
    }
    for (size_t got = 0; got < size;) {
        size_t want = size - got < CHUNK_SIZE ? size - got : CHUNK_SIZE;
        s32 n = ISFS_Read(fd, image + got, want);
        if (n <= 0) {
            fail(n);
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
    s32 result = ISFS_Initialize();
    if (result < 0) {
        fail(result);
        return NULL;
    }
    s32 fd = ISFS_Open(MESSAGE_BOARD_PATH, ISFS_OPEN_READ);
    if (fd >= 0) {
        image = read_all(fd, len);
        ISFS_Close(fd);
    } else {
        fail(fd);
    }
    ISFS_Deinitialize();
    return image;
}

const char *nand_failure(void) {
    return failure;
}
