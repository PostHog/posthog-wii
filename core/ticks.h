#ifndef TICKS_H
#define TICKS_H

#include <stdint.h>

/* The Wii counts time in ticks of 1/60750000 s from 2000-01-01 00:00:00 in
   console local time. It stores no time zone. */
enum { TICKS_PER_SECOND = 60750000, ISO_TIME_CAP = 64, ISO_TIME_OF_DAY = 11 };

/* Rounds to the nearest second. */
uint64_t ticks_to_seconds(uint64_t ticks);

/* Returns the console local time as if it were UTC. */
int64_t ticks_to_unix_ms(uint64_t ticks);

/* Writes "YYYY-MM-DDTHH:MM:SS.mmmZ". The date starts at out[0] and the
   time of day at out[ISO_TIME_OF_DAY] for every year from 0 through 9999. */
void unix_ms_to_iso(int64_t unix_ms, char out[ISO_TIME_CAP]);

#endif
