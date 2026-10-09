#ifndef NAND_H
#define NAND_H

#include <stddef.h>
#include <stdint.h>

/* Reads the Message Board file from the console NAND into a new buffer that
   the caller frees. Returns NULL when the file cannot be read. */
uint8_t *nand_read_message_board(size_t *len);

#endif
