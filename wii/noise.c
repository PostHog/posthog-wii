#include "noise.h"

#include <string.h>
#include <time.h>
#include <unistd.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

enum { READINGS = 32, READING_GAP_US = 15000 };

_Static_assert(sizeof(u32) + sizeof(time_t) + READINGS * (sizeof(struct vec3w_t) + sizeof(u64)) <= NOISE_CAP,
               "NOISE_CAP must hold every reading");

static size_t put(uint8_t *out, size_t used, size_t cap, const void *value, size_t size) {
    if (used + size > cap) {
        return used;
    }
    memcpy(out + used, value, size);
    return used + size;
}

size_t noise_read(uint8_t *out, size_t cap) {
    u32 console_id = 0;
    ES_GetDeviceID(&console_id);
    time_t now = time(NULL);
    size_t used = put(out, 0, cap, &console_id, sizeof console_id);
    used = put(out, used, cap, &now, sizeof now);

    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC);
    for (int i = 0; i < READINGS; i++) {
        /* The sensor reports about every 10 ms, so each pass sees a new value. */
        usleep(READING_GAP_US);
        WPAD_ScanPads();
        struct vec3w_t motion = {0};
        WPAD_Accel(WPAD_CHAN_0, &motion);
        u64 ticks = gettime();
        used = put(out, used, cap, &motion, sizeof motion);
        used = put(out, used, cap, &ticks, sizeof ticks);
    }
    return used;
}
