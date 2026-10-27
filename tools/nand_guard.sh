#!/usr/bin/env bash
# Fails when a Wii binary can change the console's internal storage.
# Usage: nand_guard.sh <elf>...
set -euo pipefail

# Every ISFS and ES function a binary may contain. None of them writes.
allowed='^(ISFS_(Initialize|Deinitialize|Open|GetFileStats|Read|Close)|ES_(GetTitleID|GetDeviceID|GetNumTicketViews|GetTicketViews|GetStoredTMD|ReadContent|LaunchTitle))$'

failed=0

# Raw IPC reaches every IOS device, so no list of functions can bound it.
if grep -nE '\bIOS_[A-Za-z]+ *\(|ISFS_OPEN_(WRITE|RW)' "$(dirname "$0")"/../wii/*.c; then
    echo "nand_guard: the lines above call IOS directly or open a NAND file for writing" >&2
    failed=1
fi

for elf in "$@"; do
    # A failed nm stops the script here. Inside the pipeline below it would read as a clean binary.
    symbols=$(powerpc-eabi-nm --defined-only "${elf}" | awk '{print $3}')
    if ! grep -qx main <<< "${symbols}"; then
        echo "nand_guard: ${elf} has no symbol table to check" >&2
        exit 1
    fi
    # The linker drops each function that nothing calls, so the binary can reach every function it contains.
    found=$(grep -E '^(IOS_Write(Async)?|ISFS_.*|ES_.*)$' <<< "${symbols}" | grep -vE "${allowed}" || true)
    if [[ -n ${found} ]]; then
        echo "nand_guard: ${elf} contains ${found//$'\n'/ }" >&2
        failed=1
    fi
    # A function that the compiler folds into another has no name for the list above to match.
    if ! powerpc-eabi-objdump -d "${elf}" | python3 "$(dirname "$0")"/ioctl_guard.py; then
        echo "nand_guard: ${elf} sends IOS an ioctl that tools/ioctl_guard.py does not list" >&2
        failed=1
    fi
done

exit "${failed}"
