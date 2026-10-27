#include "ticks.h"

#include <stdint.h>
#include <stdio.h>

enum {
    MS_PER_SECOND = 1000,
    SECONDS_PER_MINUTE = 60,
    MINUTES_PER_HOUR = 60,
    MS_PER_MINUTE = MS_PER_SECOND * SECONDS_PER_MINUTE,
    MS_PER_HOUR = MS_PER_MINUTE * MINUTES_PER_HOUR,
};

/* Days to civil date: https://howardhinnant.github.io/date_algorithms.html#civil_from_days.
   Its years start on March 1, so a leap day is the last day of its year. */
enum {
    DAYS_FROM_0000_03_01_TO_UNIX_EPOCH = 719468,
    YEARS_PER_ERA = 400,
    DAYS_PER_ERA = 146097,
    YEARS_PER_CENTURY = 100,
    DAYS_PER_COMMON_YEAR = 365,
    DAYS_PER_LEAP_CYCLE_BEFORE_LEAP_DAY = 4 * DAYS_PER_COMMON_YEAR,
    DAYS_PER_COMMON_CENTURY = (YEARS_PER_CENTURY * DAYS_PER_COMMON_YEAR) + 24,
    /* March through July, the five months whose lengths repeat. */
    DAYS_PER_MONTH_RUN = 153,
    MONTHS_PER_MONTH_RUN = 5,
    JANUARY_FROM_MARCH = 10,
};

static const int64_t UNIX_SECONDS_AT_2000 = 946684800;
static const int64_t MS_PER_DAY = 86400000;

uint64_t ticks_to_seconds(uint64_t ticks) {
    return (ticks / TICKS_PER_SECOND) + (ticks % TICKS_PER_SECOND >= TICKS_PER_SECOND / 2 ? 1 : 0);
}

int64_t ticks_to_unix_ms(uint64_t ticks, int utc_offset_minutes) {
    int64_t seconds =
        (int64_t)(ticks / TICKS_PER_SECOND) + UNIX_SECONDS_AT_2000 - ((int64_t)utc_offset_minutes * SECONDS_PER_MINUTE);
    int64_t millis = (int64_t)(ticks % TICKS_PER_SECOND * MS_PER_SECOND / TICKS_PER_SECOND);
    return (seconds * MS_PER_SECOND) + millis;
}

void unix_ms_to_iso(int64_t unix_ms, char out[ISO_TIME_CAP]) {
    int64_t days = unix_ms / MS_PER_DAY;
    int64_t ms_of_day = unix_ms % MS_PER_DAY;
    if (ms_of_day < 0) {
        ms_of_day += MS_PER_DAY;
        days -= 1;
    }

    int64_t days_from_0000_03_01 = days + DAYS_FROM_0000_03_01_TO_UNIX_EPOCH;
    int64_t era =
        (days_from_0000_03_01 >= 0 ? days_from_0000_03_01 : days_from_0000_03_01 - (DAYS_PER_ERA - 1)) / DAYS_PER_ERA;
    int64_t day_of_era = days_from_0000_03_01 - (era * DAYS_PER_ERA);
    int64_t year_of_era = (day_of_era - (day_of_era / DAYS_PER_LEAP_CYCLE_BEFORE_LEAP_DAY) +
                           (day_of_era / DAYS_PER_COMMON_CENTURY) - (day_of_era / (DAYS_PER_ERA - 1))) /
                          DAYS_PER_COMMON_YEAR;
    int64_t day_of_year =
        day_of_era - ((DAYS_PER_COMMON_YEAR * year_of_era) + (year_of_era / 4) - (year_of_era / YEARS_PER_CENTURY));
    int64_t shifted_month = ((MONTHS_PER_MONTH_RUN * day_of_year) + 2) / DAYS_PER_MONTH_RUN;
    int day = (int)(day_of_year - (((DAYS_PER_MONTH_RUN * shifted_month) + 2) / MONTHS_PER_MONTH_RUN) + 1);
    int month = (int)(shifted_month < JANUARY_FROM_MARCH ? shifted_month + 3 : shifted_month - JANUARY_FROM_MARCH + 1);
    long long year = year_of_era + (era * YEARS_PER_ERA) + (month <= 2 ? 1 : 0);

    (void)snprintf(out, ISO_TIME_CAP, "%04lld-%02d-%02dT%02d:%02d:%02d.%03dZ", year, month, day,
                   (int)(ms_of_day / MS_PER_HOUR), (int)(ms_of_day / MS_PER_MINUTE % MINUTES_PER_HOUR),
                   (int)(ms_of_day / MS_PER_SECOND % SECONDS_PER_MINUTE), (int)(ms_of_day % MS_PER_SECOND));
}
