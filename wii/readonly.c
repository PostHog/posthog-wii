#include <gccore.h>

/* The WRAP flags in the Makefile send every IOS_Write and IOS_WriteAsync
   call in the binary here. libogc writes NAND files on its own. WPAD_Init
   and every exit save the Wii Remote list to SYSCONF, and a return to the
   Wii Menu writes state.dat and NANDBOOTINFO. Sockets, the SD card and the
   Wii Remotes use ioctls, so they still work. */

/* The linker's --wrap option needs these exact names with external linkage. */
/* NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,misc-use-internal-linkage) */
s32 __wrap_IOS_Write(s32 descriptor, const void *buffer, s32 len);
s32 __wrap_IOS_WriteAsync(s32 descriptor, const void *buffer, s32 len, ipccallback callback, void *user_data);
/* NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,misc-use-internal-linkage) */

s32 __wrap_IOS_Write(s32 descriptor, const void *buffer, s32 len) {
    (void)descriptor;
    (void)buffer;
    (void)len;
    return IPC_EINVAL;
}

s32 __wrap_IOS_WriteAsync(s32 descriptor, const void *buffer, s32 len, ipccallback callback, void *user_data) {
    (void)descriptor;
    (void)buffer;
    (void)len;
    (void)callback;
    (void)user_data;
    return IPC_EINVAL;
}
