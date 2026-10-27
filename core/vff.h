#ifndef VFF_H
#define VFF_H

#include <stddef.h>
#include <stdint.h>

typedef void vff_file_fn(void *ctx, const uint8_t *head, size_t head_len);

/* Calls on_file once per regular file in a VFF image. head holds the first
   min(file size, scratch_cap) bytes of the file and is valid only during
   the call. An image that is not a FAT16 VFF volume has no files. */
void vff_each_file(const uint8_t *image, size_t image_len, uint8_t *scratch, size_t scratch_cap, vff_file_fn *on_file,
                   void *ctx);

#endif
