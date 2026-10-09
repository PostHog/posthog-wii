#include "ticks.h"

#include <stdio.h>

static const int64_t UNIX_SECONDS_AT_2000 = 946684800;
static const int64_t MS_PER_DAY = 86400000;

uint64_t ticks_to_seconds(uint64_t ticks) {
    return ticks / TICKS_PER_SECOND + (ticks % TICKS_PER_SECOND >= TICKS_PER_SECOND / 2 ? 1 : 0);
}

int64_t ticks_to_unix_ms(uint64_t ticks, int utc_offset_minutes) {
    int64_t seconds = (int64_t)(ticks / TICKS_PER_SECOND) + UNIX_SECONDS_AT_2000 - (int64_t)utc_offset_minutes * 60;
    int64_t millis = (int64_t)(ticks % TICKS_PER_SECOND * 1000 / TICKS_PER_SECOND);
    return seconds * 1000 + millis;
}

void unix_ms_to_iso(int64_t unix_ms, char out[ISO_TIME_CAP]) {
    int64_t days = unix_ms / MS_PER_DAY;
    int64_t ms_of_day = unix_ms % MS_PER_DAY;
    if (ms_of_day < 0) {
        ms_of_day += MS_PER_DAY;
        days -= 1;
    }

    /* Days to civil date: https://howardhinnant.github.io/date_algorithms.html#civil_from_days */
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t day_of_era = z - era * 146097;
    int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    int64_t shifted_month = (5 * day_of_year + 2) / 153;
    int day = (int)(day_of_year - (153 * shifted_month + 2) / 5 + 1);
    int month = (int)(shifted_month < 10 ? shifted_month + 3 : shifted_month - 9);
    long long year = (long long)(year_of_era + era * 400 + (month <= 2 ? 1 : 0));

    snprintf(out, ISO_TIME_CAP, "%04lld-%02d-%02dT%02d:%02d:%02d.%03dZ", year, month, day,
             (int)(ms_of_day / 3600000), (int)(ms_of_day / 60000 % 60), (int)(ms_of_day / 1000 % 60),
             (int)(ms_of_day % 1000));
}
