# /// script
# requires-python = ">=3.11"
# ///
"""Fails when a Wii binary sends IOS an ioctl that ALLOWED does not list.

Usage: powerpc-eabi-objdump -d <elf> | python3 tools/ioctl_guard.py

A change to the console's internal storage that is not a file write goes
through an ioctl on /dev/fs or /dev/es. The compiler folds one libogc function
into another, so a list of function names cannot bound those ioctls.

The check takes the constant loaded into the ioctl register nearest above each
call and follows no branch. It catches a libogc that sends a new ioctl. It
proves nothing about code written to hide a number.
"""

import re
import sys

# The register that carries the ioctl number into each IOS function.
IOCTL_REGISTER = {
    "IOS_Ioctl": "r4",
    "IOS_IoctlAsync": "r4",
    "IOS_Ioctlv": "r4",
    "IOS_IoctlvAsync": "r4",
    "IOS_IoctlvReboot": "r4",
    "IOS_IoctlvFormat": "r5",
    "IOS_IoctlvFormatAsync": "r5",
}

# The function passes on a number that its own caller chose.
FROM_CALLER = -1

# The numbers each function may send, from a read of libogc 2.13.0. Read what
# a new number does on its device before adding it.
ALLOWED: dict[str, set[int]] = {
    # /dev/fs, libogc/isfs.c. 0x0B reads the size of an open file.
    "ISFS_GetFileStats": {0x0B},
    # /dev/es, libogc/es.c. 0x08 starts a title. Every other number reads.
    "ES_GetDeviceID": {0x07},
    "ES_LaunchTitle": {0x08},
    "ES_ReadContent": {0x0A},
    "ES_GetNumTicketViews": {0x12},
    "ES_GetTicketViews": {0x13},
    "ES_GetTitleID": {0x20},
    "ES_GetStoredTMD": {0x35},
    "_ES_open_r": {0x09, 0x12, 0x24},
    "_ES_close_r": {0x0B},
    "_ES_seek_r": {0x23},
    "_ES_decodepath": {0x20, 0x34},
    # /dev/stm, libogc/stm.c. Power, reset and the disc slot light.
    "STM_RebootSystem": {0x2001},
    "STM_ShutdownToStandby": {0x2003},
    "STM_ShutdownToIdle": {0x2004},
    "STM_SetLedMode": {0x6002},
    "__STM_SetEventHook": {0x1000},
    "__STM_ReleaseEventHook": {0x3002},
    # The USB host controllers, libogc/usb.c. The Wii Remotes use the Bluetooth adapter on one.
    "USB_Initialize": {0x00, 0x01, 0x06},
    "USB_Deinitialize": {0x02},
    "USB_OpenDevice": {0x05, 0x06},
    "USB_ResumeDevice": {0x06},
    "USB_GetDeviceList": {0x0C},
    "USB_ClearHalt": {0x11},
    "USB_DeviceRemovalNotifyAsync": {0x1A},
    "USBV5_GetDescriptors": {0x03},
    "USBV5_SuspendResume": {0x10},
    "__usbv5_attachfinishCB": {0x01},
    "__usbv5_devicechangeCB": {0x06},
    "__usb_control_message": {0x00, 0x12},
    "__usb_interrupt_bulk_message": {0x15, FROM_CALLER},
    # /dev/sdio/slot0, libogc/wiisd.c. The SD card.
    "sdio_Startup": {0x01, 0x04, 0x06, 0x07, 0x0B},
    "sdio_IsInserted": {0x0B},
    "__sdio_sendcommand": {0x07},
    "__sdio_setbuswidth": {0x01, 0x02},
    "__sdio_waithcr": {0x02},
    "__sd0_deselect": {0x07},
    # /dev/net, libogc/network_wii.c. Sockets, the link state and the WiiConnect24 socket.
    "net_init_chain": {0x06, 0x07, 0x10, 0x1F},
    "net_wc24cleanup": {0x07},
    "net_socket": {0x0F},
    "net_connect": {0x04},
    "net_ioctl": {0x05},
    "net_setsockopt": {0x09},
    "net_poll": {0x0B},
    "net_gethostbyname": {0x11},
    "net_sendto": {0x0D},
    "net_recvfrom": {0x0C},
    "net_close": {0x03},
    # libogc/ipc.c. The check reads the number at each call of these two.
    "IOS_IoctlvFormat": {FROM_CALLER},
    "IOS_IoctlvFormatAsync": {FROM_CALLER},
}

FUNCTION = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
INSTRUCTION = re.compile(r"^\s*[0-9a-f]+:\t[0-9a-f ]+\t(\S+)\s*(.*)$")
TARGET = re.compile(r"<([^>+]+)>")
CALLS = {"bl", "bctrl", "blrl"}


def problems(listing: list[str]) -> list[str]:
    found: list[str] = []
    calls = 0
    caller = ""
    loaded: dict[str, int] = {}
    for line in listing:
        function = FUNCTION.match(line)
        if function:
            # The compiler names a changed copy of a function with a dot suffix.
            caller = function.group(1).split(".")[0]
            loaded = {}
            continue
        instruction = INSTRUCTION.match(line)
        if not instruction:
            continue
        mnemonic, operands = instruction.groups()
        target = TARGET.search(operands) if mnemonic in ("bl", "b") else None
        if target and target.group(1).startswith("IOS_Ioctl"):
            calls += 1
            name = target.group(1)
            if name not in IOCTL_REGISTER:
                found.append(f"{caller} calls {name}, which this check cannot read")
            else:
                number = loaded.get(IOCTL_REGISTER[name], FROM_CALLER)
                if number not in ALLOWED.get(caller, set()):
                    sent = "a number it does not load as a constant" if number == FROM_CALLER else f"ioctl {number:#x}"
                    found.append(f"{caller} sends {sent}, which ALLOWED does not list")
        if mnemonic in CALLS:
            # A call can change every register that carries an argument.
            loaded = {}
        elif mnemonic == "li":
            register, value = operands.split(",")
            loaded[register] = int(value)
        elif not mnemonic.startswith(("st", "cmp")):
            loaded.pop(operands.split(",")[0], None)
    if calls == 0:
        found.append("the listing holds no IOS ioctl call, so there was nothing to check")
    return list(dict.fromkeys(found))


if __name__ == "__main__":
    lines = problems(sys.stdin.read().splitlines())
    for line in lines:
        print(f"ioctl_guard: {line}", file=sys.stderr)
    sys.exit(1 if lines else 0)
