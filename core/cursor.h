#ifndef CURSOR_H
#define CURSOR_H

#include <stddef.h>
#include <stdint.h>

/* The cursor is the start, in ticks, of the newest session that a host and
   project token already have. A run sends only sessions that start later. */
enum { CURSOR_TEXT_CAP = 48 };

/* Reads the cursor from text that cursor_write made. Returns 0, which is
   before every session, when the text is empty, damaged or made for another
   host or project token. */
uint64_t cursor_read(const char *text, size_t len, const char *host, const char *api_key);

/* Writes the cursor as text. Returns its length. */
size_t cursor_write(char out[CURSOR_TEXT_CAP], const char *host, const char *api_key, uint64_t start_ticks);

#endif
