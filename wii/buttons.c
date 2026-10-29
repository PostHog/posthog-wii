#include "buttons.h"

#include <stdbool.h>

#include <gccore.h>
#include <wiiuse/wpad.h>

typedef enum { PRESS_NONE, PRESS_RESET, PRESS_POWER } press;

/* The callbacks run with interrupts off, so they only record the press. */
static volatile press pressed;

static void on_reset(u32 irq, void *ctx) {
    (void)irq;
    (void)ctx;
    pressed = PRESS_RESET;
}

static void on_power(void) {
    pressed = PRESS_POWER;
}

static void on_remote_power(s32 channel) {
    (void)channel;
    pressed = PRESS_POWER;
}

void buttons_watch(void) {
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);
    WPAD_SetPowerButtonCallback(on_remote_power);
}

bool buttons_quit(void) {
    return ((WPAD_ButtonsDown(WPAD_CHAN_0) & WPAD_BUTTON_HOME) || pressed != PRESS_NONE) != 0;
}

bool buttons_power_off(void) {
    return pressed == PRESS_POWER;
}
