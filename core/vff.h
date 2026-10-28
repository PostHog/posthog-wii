#ifndef VFF_H
#define VFF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Copies len bytes that start at offset in the image to out. The walk asks
   only for bytes inside the image. Returns false when the read fails. */
typedef bool vff_read_fn(void *source, size_t offset, uint8_t *out, size_t len);

/* A VFF image that the walk reads in pieces, so a caller never holds the
   whole image in memory. */
typedef struct {
    vff_read_fn *read;
    void *source;
    size_t len;
} vff_image;

typedef void vff_file_fn(void *ctx, const uint8_t *head, size_t head_len);

/* Calls on_file once per regular file in a VFF image. head holds the first
   min(file size, scratch_cap) bytes of the file and is valid only during
   the call. An image that is not a FAT16 VFF volume has no files. A failed
   read ends the directory or the file head that it was part of. Returns
   false when a read failed, so a file may have been missed. */
bool vff_each_file(const vff_image *image, uint8_t *scratch, size_t scratch_cap, vff_file_fn *on_file, void *ctx);

#endif
