#!/bin/bash
# Runs the debug build in Dolphin and saves the frames it posts to build/shots.
# The debug build plays a fixed script on a fixed track, so the frames are the
# same every run. It posts /done after its last frame.
# Usage: tools/shots.sh <dolphin binary> <dolphin user dir> <port>
set -u
dolphin_bin="$1"
user_dir="$2"
port="$3"
out=build/shots

listener_up() {
    curl -s -o /dev/null --max-time 2 -X POST "http://127.0.0.1:$port/ping"
}

# A listener left over from an earlier run would save frames somewhere else.
if listener_up; then
    echo "shots: something already listens on port $port" >&2
    exit 1
fi

rm -rf "$out"
uv run tools/listener.py "$out" "$port" &
listener=$!
for _ in $(seq 1 60); do
    listener_up && break
    sleep 1
done

# Dolphin keeps the display buffer on the GPU unless this hack is off, and the
# game can only post the buffer from emulated memory.
"$dolphin_bin" -u "$user_dir" -b -e build/wii/debug.elf \
    -C Dolphin.Analytics.PermissionAsked=True -C Graphics.Hacks.XFBToTextureEnable=False > /dev/null 2>&1 &
dolphin=$!
for _ in $(seq 1 90); do
    [ -e "$out/done" ] && break
    sleep 1
done
# Dolphin in batch mode ignores SIGTERM.
kill -9 "$dolphin" 2>/dev/null
kill "$listener" 2>/dev/null
wait 2>/dev/null

if [ ! -e "$out/done" ]; then
    echo "shots: the game did not finish its captures" >&2
    exit 1
fi
ls "$out"
