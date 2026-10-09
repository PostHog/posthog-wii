#ifndef NOISE_H
#define NOISE_H

#include <stddef.h>
#include <stdint.h>

/* A buffer of this size holds everything noise_read writes. */
enum { NOISE_CAP = 512 };

/* Fills out with readings from the first Wii Remote's motion sensor, the
   clock's tick counter, the time and the console's ID. Returns the number
   of bytes written. Takes about half a second.

   A Wii gives an app no random number source, so the TLS keys come from
   these readings. Without a Wii Remote that reports motion, only the clock
   and the console's ID differ between runs. */
size_t noise_read(uint8_t *out, size_t cap);

#endif
