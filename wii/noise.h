#ifndef NOISE_H
#define NOISE_H

#include <stddef.h>
#include <stdint.h>

/* A buffer of this size holds everything noise_read writes. */
enum { NOISE_CAP = 2048 };

typedef struct {
    unsigned readings; /* motion readings taken */
    unsigned changed;  /* readings that differ from the one before */
} noise_report;

/* Fills out with readings from the first Wii Remote's motion sensor, the
   clock's tick counter, the time and the console's ID. Returns the number
   of bytes written. It stops when enough motion readings have changed, or
   after about a second and a half.

   A Wii gives an app no random number source, so the TLS keys come from
   these readings. Without a Wii Remote that reports motion, only the clock
   and the console's ID differ between runs. */
size_t noise_read(uint8_t *out, size_t cap);

/* What the last noise_read saw. Both counts are 0 before the first call. */
noise_report noise_last(void);

#endif
