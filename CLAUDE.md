# posthog-wii

Reads the play history that the Wii System Menu keeps in its Message Board file and sends it to PostHog as events.
Two front ends share one C core: a host CLI (`wii2posthog`) and a Wii homebrew app.

## Commands

- `make` builds `build/wii2posthog`.
- `make test` builds and runs the tests under ASan and UBSan.
- `make lint` builds the Wii app, runs every hook in `.pre-commit-config.yaml` on every file, then runs clang-tidy on every C file. It must pass before a commit. The hooks pin every linter but clang-tidy, which the Makefile pins.
- `build/wii2posthog samples/cdb.vff --dry-run` prints the request bodies without sending.
- `make wii` cross-compiles the Wii app in Docker to `build/wii/boot.elf` and `boot.dol`, then runs `tools/nand_guard.sh` on the result. The first run downloads mbedTLS into `assets/` and checks its hash.
- `make sd HOST=https://us.i.posthog.com API_KEY=phc_...` builds `build/sd/apps/posthog-wii/`, the folder that goes on an SD card: `boot.dol`, `meta.xml`, `icon.png`, `settings.txt` and `seed.bin`.
- `make dolphin HOST=http://127.0.0.1:18010 API_KEY=phc_...` boots that app in Dolphin with the isolated user folder `.dolphin/`. It writes the two values to the settings file on Dolphin's SD card, and a new seed file next to it. The first run copies `samples/cdb.vff` into its NAND.
- `make shots` builds `build/wii/debug.elf`, runs it in Dolphin, and saves its frames to `build/shots/`. Read those PNGs to check any drawing change. They are the raw 640-pixel display buffer, so on a 16:9 console the picture looks squeezed sideways.
- `uv run tools/listener.py <dir>` is a local stand-in for PostHog capture. Point `HOST` at `http://127.0.0.1:18010` and read `<dir>/posts.log`.

Dolphin's command-line config names its graphics system `Graphics`, not `GFX`. Dolphin ignores a `-C` override with an unknown name and reports no error.

A local PostHog dev stack has two capture addresses. The proxy on port 8010 listens on loopback only and routes only the host name `localhost`. For any other name, an IP address included, it answers 200 with an empty body and drops the request. Capture itself listens on port 3307 on every interface and answers `{"status":"Ok"}`, so a console on the network uses `http://<computer address>:3307`.

## Layout

| Path | Owns |
| --- | --- |
| `core/bytes.h` | Bounds-checked reads of image data, and big-endian writes |
| `core/vff.c` | The VFF container and its FAT16 filesystem, and which bytes of the image a walk reads through the front end's read function |
| `core/playlog.c` | The daily play log record layout |
| `core/ticks.c` | Wii tick time and its conversion to Unix time |
| `core/events.c` | The PostHog event model and the request body |
| `core/http_request.h` | The POST that a front end sends, and the type of the function that sends it |
| `core/upload.c` | The request path, how many sessions a request carries, what answer counts as accepted, and stopping at the first failure |
| `core/summary.c` | The one-line text form of a session that both front ends print |
| `core/settings.c` | The settings file format |
| `core/cursor.c` | The cursor file format, and which host and token a cursor counts for |
| `host/http.c` | libcurl. No other file includes `curl.h` |
| `host/main.c` | Arguments, file reading and output for the CLI |
| `game/runner.c` | The endless runner's rules for Max's jump, the track, collisions and the score. Portable code that draws nothing |
| `game/autoplay.c` | A player that clears every fire, for the tests and the debug build |
| `wii/nand.c` | ISFS. No other file reads the NAND |
| `wii/buttons.c` | The console's RESET and POWER buttons and the Wii Remote's power button |
| `wii/readonly.c` | The replacement for `IOS_Write`, linked into every Wii binary |
| `wii/sdcard.c` | libfat. No other file touches the SD card. A read mounts the card read-only |
| `wii/socket.c` | libogc sockets. No other file calls a `net_` function. The network start, a connect, a send and a receive each have a time limit. The name lookup has none, because libogc offers it only as a call that waits |
| `wii/http.c` | The HTTP request and response, and the choice between plain and TLS from the host's scheme |
| `wii/tls.c` | mbedTLS. No other file includes an mbedTLS header |
| `wii/tls_config.h` | The changes to mbedTLS's default configuration |
| `wii/roots.pem` | The root certificates every build trusts: Amazon Root CA 1 to 4 and Starfield Services Root CA G2. Amazon signs PostHog Cloud and asks a client with its own trust list to hold all five (https://www.amazontrust.com/repository/) |
| `wii/noise.c` | The readings that the TLS random number generator mixes with its seed file |
| `wii/gfx.c` | GX. No other file draws |
| `wii/play.c` | The game loop, with Wii Remote input, the scenery and its colors, and the score digits |
| `wii/main.c` | The app's entry point, which runs the text console and the sync |
| `wii/debug.c` | The debug build's entry point, which plays a script and captures frames |
| `wii/meta.xml` | The name and description that the Homebrew Channel shows |
| `tools/atlas.py` | Packs the pictures into one Wii texture |
| `tools/icon.py` | Makes the Homebrew Channel icon |
| `tools/shots.sh`, `tools/listener.py` | The frame capture run behind `make shots` |
| `tools/nand_guard.sh` | The build check that a Wii binary has no way to write the NAND |
| `tools/ioctl_guard.py` | The ioctl numbers a Wii binary may send to IOS, and the part of the build check that reads them from the binary |
| `tests/test_core.c` | A synthetic image builder and the core tests |
| `tests/test_game.c` | The game tests |

## Design rules

These follow "A Philosophy of Software Design".

- **A module hides one decision.** Only `vff.c` knows FAT. Only `playlog.c` knows record offsets. Only `events.c` knows event names and properties. A second file that needs one of these facts is a design error.
- **Interfaces stay smaller than implementations.** A new public function must remove more complexity from its callers than it adds to the header. No function only forwards to another.
- **Malformed input is not an error.** A truncated image, a broken cluster chain or a bad record yields fewer sessions. Callers of `core/` never check an error code for input shape.
- **Complexity goes down, not up.** A caller never computes an offset, a buffer size or a chunk boundary that a module can compute for it.
- **Nothing is built for a future case.** Add the parameter, the option or the abstraction when a second real caller exists.

## C rules

- C11, built with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`. The Wii build is GNU C11 without `-Wpedantic`, because `wii/` uses `#embed` and libogc's headers.
- `core/` compiles with both Apple clang and devkitPPC GCC. Run `make test` and `make wii` after a change to it, because each compiler rejects code the other accepts.
- `core/` and `game/` are portable: no libogc types, no host-only headers, no heap allocation, no global mutable state.
- Every read of image data goes through `core/bytes.h`. Never cast a buffer to a struct and never index image data directly.
- Buffer sizes are compile-time constants, checked with `static_assert` where one depends on another.
- `.clang-tidy` enables every check in its families, except the one that asks for the C11 Annex K functions, which newlib and glibc do not have. Fix a finding in the code. A `NOLINT` comment is allowed only where something outside the code fixes a name, a linkage or a signature (the linker's `--wrap`, an mbedTLS callback, `qsort`). It names each check, and a comment above it names that outside requirement.
- Fixed-width integer types for anything that comes from a file.

## Comments

- The default is no comment.
- No statement of the project's current state, in code or in a document: "not built yet", "never run on a console", "for now". It turns false without a diff that touches it. Write the rule or the fact that stays true.
- A comment states a reason the code cannot show, a consequence in another file, or the source of a format fact.
- No narration of what a line does, no change history, no mention of a conversation or a review, no em dashes.
- A header comment gives the part of a function's contract that its signature cannot: units, ownership, what a failure returns.
- A rule about which file may do what goes in this document, not in a comment.
- Write whole, plain sentences. No colon or semicolon to join two thoughts, no figure of speech.

## Safety and privacy

A user's console must still start after any run of this software. This rule comes before any feature.

- **No binary writes to the console's internal storage (the NAND).** libogc 2.13 writes it on its own. `WPAD_Init` and every exit save the Wii Remote list to `/shared2/sys/SYSCONF`, and a return to the Wii Menu writes `state.dat` and `NANDBOOTINFO`. `wii/readonly.c` replaces `IOS_Write` for the whole binary. `tools/nand_guard.sh` fails the build when a binary contains `IOS_Write`, contains an ISFS or ES function that is not on its read-only list, or sends IOS an ioctl that `tools/ioctl_guard.py` does not list.
- **The write block covers the app's calls, not what IOS does by itself.** IOS records each title launch in `/sys/launch.sys`, by Dolphin's model of ES, and every exit from the app starts a title. libogc can also write the clock chip's SRAM (`libogc/system.c`). Do not write that the app changes nothing on the console.
- **Only `wii/nand.c` opens a NAND file, and only with `ISFS_OPEN_READ`.** No file under `wii/` calls an `IOS_` function. The guard checks both.
- **A change of `DEVKIT_IMAGE` needs a new read of libogc's write paths.** They are `libogc/conf.c`, `libogc/wiilaunch.c` and `wiiuse/wpad.c`. A new ioctl fails the guard. Read what the ioctl does on its device before adding it to `tools/ioctl_guard.py`. The guard reads constants and follows no branch, so it does not replace the read.
- **The project builds no WAD and installs nothing on the console.** The app runs from the SD card through the Homebrew Channel. A channel banner that the Wii Menu cannot parse stops the menu from starting. No tool proves a banner safe, and a banner that renders in Dolphin can still fail on a console.
- **A run in Dolphin shows only that drawing and logic work.** Dolphin grants every file access, skips IOS patches and reports the network as connected. Do not claim support for a console type (Wii, Wii mini, Wii U) before a run on that type.
- **An `https://` connection always checks the certificate chain and the host name.** Never set `MBEDTLS_SSL_VERIFY_NONE` or `MBEDTLS_SSL_VERIFY_OPTIONAL`, and never add a setting that skips the check.
- **A new mbedTLS version changes `MBEDTLS_VERSION` and `MBEDTLS_SHA256` together.** Take the hash from the release page, not from the download.
- **A new version of an npm package changes its version and its hash together, and a new `DEVKIT_IMAGE` tag changes its digest.** Take the hash from `dist.integrity` in the registry's answer for that version, and the digest from the registry that holds the image.
- **The TLS keys start from the seed file that `make sd` writes on the computer.** A Wii gives an app no random number source. mbedTLS mixes the file with the readings in `wii/noise.c`, replaces the file before it gives out a random byte, and fails without it. Never make the seed optional, and never remove a source.
- **The app writes two files, the cursor and the random seed, both on the SD card.** `sdcard_write` is the only function that mounts the card for writing. A new file to write needs a reason as strong as theirs.
- **Dolphin loses SD card writes when it is killed.** Its window does not stop on a signal, so a second run cannot see what the first one wrote. Test a write and its read inside one run.
- Never commit `samples/` or a `*.vff` file. The pre-commit hook refuses both. A dump holds the owner's messages, photos and a console ID.
- Committed fixtures are synthetic. Do not derive one from a real dump.
- `playlog.c` never parses the sent-message buffer that follows the sessions. It holds friend names.

### Rules for code that patches IOS or runs at power-on

A console can need libpatcher (WiiLink24, MIT) before IOS lets the app open the Message Board file. libpatcher patches IOS in memory.

- Check that `HW_AHBPROT` (`0x0D800064`) reads `0xFFFFFFFF` before any patch. libpatcher does not check.
- Call only `patch_ahbprot_reset` and `patch_isfs_permissions`, and call them before WPAD, ISFS and the network start, because the first one reloads IOS.
- Never call `apply_patches`, `patch_ios_verify`, `patch_es_identify`, `ES_Identify` or `ES_SetUID`. Never depend on a custom IOS.
- `patch_isfs_permissions` removes the permission check for writes too, until the next IOS reload. The write block above stops libogc from using that gap.
- A missing `HW_AHBPROT` or a patch that returns false means no sync. The game still runs.
- A mode that Priiloader starts at every power-on must never call `exit`, must end by writing Priiloader's "Pune" word and launching the Wii Menu, and must bound its total run time. A Wii mini and a Wii U have no RESET button to escape a hang.

Sources: WiiBrew (`Brick`, `/dev/fs`, `/dev/es`, `Hardware/NAND`), HackMii, wii.hacks.guide, and the libogc 2.13.0, libpatcher, Priiloader and Homebrew Channel source.

## Provenance

- Format facts come from WiiBrew, the Riiwind parser (MIT) and the System Menu decompilation. Reimplement the facts. Never copy code from the decompilation, from leaked SDK material, or from a repository without a license (`cdbackup`, `WiiVFF`).
- A real dump from system menu 4.3 confirms the play log layout.

## Brand

- Never generate or edit hedgehog art.
- Max's sprites and the fire come from the `@posthog/hedgehog-mode` npm package, fetched into the gitignored `assets/`. `tools/atlas.py` copies frames pixel for pixel. No code tints, redraws or rescales them with smoothing.
- Hogzilla is that package's `hogzilla` skin. The game draws him at a whole multiple of his size, so each sprite pixel stays a square.
- The pickups are team crests from `@posthog/brand`, also fetched into `assets/`. They are vector art, so the atlas tool scales them down and changes nothing else.
- The Homebrew Channel icon is the `two-player` illustration from `@posthog/brand`, scaled down onto the page color by `tools/icon.py`.
- Do not use the `@posthog/icons` package.
- `@posthog/brand` assets (hogs, RoundHog) are under the PolyForm Strict license, which does not allow distribution. They stay out of the repository and out of any published build.

## Commits

Conventional commits, subject line only, under 50 characters.
