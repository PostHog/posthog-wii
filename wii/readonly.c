#include <gccore.h>

/* The linker sends every IOS_Write and IOS_WriteAsync call in the binary
   here: see WRAP in the Makefile. libogc writes NAND files without being
   asked. WPAD_Init and every exit save the Wii Remote list to SYSCONF, and a
   return to the Wii Menu writes state.dat and NANDBOOTINFO. Sockets, the SD
   card and the Wii Remotes use ioctls, so they still work. */
s32 __wrap_IOS_Write(s32 fd, const void *buffer, s32 len);
s32 __wrap_IOS_WriteAsync(s32 fd, const void *buffer, s32 len, ipccallback callback, void *user_data);

s32 __wrap_IOS_Write(s32 fd, const void *buffer, s32 len) {
    (void)fd;
    (void)buffer;
    (void)len;
    return IPC_EINVAL;
}

s32 __wrap_IOS_WriteAsync(s32 fd, const void *buffer, s32 len, ipccallback callback, void *user_data) {
    (void)fd;
    (void)buffer;
    (void)len;
    (void)callback;
    (void)user_data;
    return IPC_EINVAL;
}
