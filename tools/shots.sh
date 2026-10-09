#!/bin/bash
# Runs the debug build in Dolphin and saves the frames it posts to build/shots.
# The debug build plays a fixed script, so the frames are the same every run.
# Usage: tools/shots.sh <dolphin binary> <dolphin user dir> <frames to wait for>
set -u
dolphin_bin="$1"
user_dir="$2"
wanted="$3"
out=build/shots

rm -rf "$out"
uv run tools/listener.py "$out" &
listener=$!
# Dolphin keeps the display buffer on the GPU unless this hack is off, and the
# game can only post the buffer from emulated memory.
"$dolphin_bin" -u "$user_dir" -b -e build/wii/boot.elf \
    -C Dolphin.Analytics.PermissionAsked=True -C Graphics.Hacks.XFBToTextureEnable=False > /dev/null 2>&1 &
dolphin=$!

for _ in $(seq 1 60); do
    [ "$(find "$out" -name 'shot-*.png' 2>/dev/null | wc -l)" -ge "$wanted" ] && break
    sleep 1
done
# Dolphin in batch mode ignores SIGTERM.
kill -9 "$dolphin" 2>/dev/null
kill "$listener" 2>/dev/null
wait 2>/dev/null
ls "$out"
