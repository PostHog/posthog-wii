#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>

/* The buttons that end the app: HOME on the Wii Remote, the console's RESET
   and POWER buttons, and the Wii Remote's power button. libogc's own
   handlers for the last three do nothing. */

/* Starts recording presses. Call it after WPAD_Init. */
void buttons_watch(void);

/* True when HOME was pressed in the last WPAD_ScanPads, or any other of the
   buttons since buttons_watch. */
bool buttons_quit(void);

/* True when a power button was pressed since buttons_watch. */
bool buttons_power_off(void);

#endif
