# posthog-wii

Reads the play history that the Wii System Menu keeps in its Message Board file and sends it to PostHog as events.
Two front ends share one C core: a host CLI (`wii2posthog`) and a Wii homebrew app.

## Commands

- `make` builds `build/wii2posthog`.
- `make test` builds and runs the core tests under ASan and UBSan.
- `build/wii2posthog samples/cdb.vff --dry-run` prints the request bodies without sending.
- `make wii HOST=http://127.0.0.1:8010 API_KEY=phc_... UTC_OFFSET_MINUTES=-240` cross-compiles the Wii app in Docker to `build/wii/boot.elf`. The three settings are compiled in until the app reads a config file from the SD card.
- `make dolphin` boots that app in Dolphin with the isolated user folder `.dolphin/`. Its NAND is seeded once from `samples/cdb.vff`.
- `make shots` builds the debug game, runs its fixed input script in Dolphin, and saves seven frames to `build/shots/`. Read those PNGs to check any drawing change. They are the raw 640-pixel display buffer, so on a 16:9 console the picture looks squeezed sideways.
- `uv run tools/listener.py <dir>` is a local stand-in for PostHog capture. Point `HOST` at `http://127.0.0.1:18010` and read `<dir>/posts.log`.

Dolphin's command-line config names its graphics system `Graphics`, not `GFX`. A `-C` override with an unknown name is ignored without an error.

## Layout

| Path | Owns |
| --- | --- |
| `core/bytes.h` | Bounds-checked big-endian and little-endian reads |
| `core/vff.c` | The VFF container and its FAT16 filesystem |
| `core/playlog.c` | The daily play log record layout |
| `core/ticks.c` | Wii tick time and its conversion to Unix time |
| `core/events.c` | The PostHog event model and the `/batch` JSON |
| `core/summary.c` | The one-line text form of a session that both front ends print |
| `core/send.h` | The one-function sender interface that each front end implements |
| `host/send_curl.c` | libcurl. No other file includes `curl.h` |
| `host/main.c` | Arguments, file reading and output for the CLI |
| `game/hedgehog.c` | Hedgehog mode: Max's physics, controls, wandering and animation choice. Portable, no drawing |
| `wii/nand.c` | ISFS. No other file reads the NAND |
| `wii/http.c` | Plain HTTP over libogc sockets. No TLS |
| `wii/gfx.c` | GX. No other file draws |
| `wii/play.c` | The hedgehog mode loop: Wii Remote input, the room and its colors |
| `wii/main.c` | Console setup and the sync flow on the Wii |
| `tools/atlas.py` | Packs the hedgehog frames into one Wii texture |
| `tests/test_core.c` | A synthetic image builder and the core tests |
| `tests/test_game.c` | The hedgehog mode tests |

## Design rules

These follow "A Philosophy of Software Design".

- **A module hides one decision.** Only `vff.c` knows FAT. Only `playlog.c` knows record offsets. Only `events.c` knows event names and properties. A second file that needs one of these facts is a design error.
- **Interfaces stay smaller than implementations.** A new public function must remove more complexity from its callers than it adds to the header. No function only forwards to another.
- **Malformed input is not an error.** A truncated image, a broken cluster chain or a bad record yields fewer sessions. Callers of `core/` never check an error code for input shape.
- **Complexity goes down, not up.** A caller never computes an offset, a buffer size or a chunk boundary that a module can compute for it.
- **Nothing is built for a future case.** Add the parameter, the option or the abstraction when a second real caller exists.

## C rules

- C11, built with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.
- `core/` compiles with both Apple clang and devkitPPC GCC. Run `make test` and `make wii` after a change to it, because each compiler rejects code the other accepts.
- `core/` and `game/` are portable: no libogc types, no host-only headers, no heap allocation, no global mutable state.
- Every read of image data goes through `core/bytes.h`. Never cast a buffer to a struct and never index image data directly.
- Buffer sizes are compile-time constants, checked with `_Static_assert` where one depends on another.
- Fixed-width integer types for anything that comes from a file.

## Comments

- The default is no comment.
- A comment states a reason the code cannot show, a consequence in another file, or the source of a format fact.
- No narration of what a line does, no change history, no mention of a conversation or a review, no em dashes.
- A header comment describes the contract of a public function: what the caller gets and in which units.

## Safety and privacy

- The Wii app opens NAND files for reading only. No code path calls an ISFS function that writes, creates, renames or deletes.
- `samples/` and `*.vff` are never committed. A dump holds the owner's messages, photos and a console ID.
- Committed fixtures are synthetic. Do not derive one from a real dump.
- `playlog.c` never parses the sent-message buffer that follows the sessions. It holds friend names.
- A WAD goes on a real console only after its banner renders in Dolphin's Wii Menu, and only on a console with Priiloader or BootMii as boot2 and a fresh NAND backup.

## Provenance

- Format facts come from WiiBrew, the Riiwind parser (MIT) and the System Menu decompilation. Reimplement the facts. Never copy code from the decompilation, from leaked SDK material, or from a repository without a license (`cdbackup`, `WiiVFF`).
- The play log layout is verified against one real dump from system menu 4.3. The older layout without the `03_0` magic comes from the decompilation only.

## Brand

- Never generate or edit hedgehog art.
- Max's sprites come from the `@posthog/hedgehog-mode` npm package, fetched into the gitignored `assets/`. `tools/atlas.py` copies frames pixel for pixel. No code tints, redraws or rescales them with smoothing.
- `@posthog/brand` assets (hogs, RoundHog) are PolyForm Strict. They stay out of the repository until PostHog's brand team approves a release.

## Commits

Conventional commits, subject line only, under 50 characters.
