# posthog-wii

Send your Wii play history to PostHog.

The Wii System Menu records every game and channel you launch, and shows the totals as the "Today's Accomplishments" letter on the Message Board.
This project reads those records and sends each play session to a PostHog project as two events, `game started` and `game ended`.

## Parts

- `wii2posthog`, a command-line tool that reads a Message Board dump on a computer and sends it.
- A Wii app that reads the same data on the console, sends it, and then runs a small game.

## Requirements

- The command-line tool needs a C11 compiler, `make`, and libcurl with its headers.
- The Wii app needs Docker to run the devkitPPC build image, plus [uv](https://docs.astral.sh/uv/), `curl` and `shasum`. The first build downloads mbedTLS and two PostHog npm packages, and checks the hash of each.
- `make dolphin` and `make shots` need the [Dolphin](https://dolphin-emu.org/) emulator. Set `DOLPHIN` to its binary if it is not at the macOS default path.
- `make lint` needs Docker and uv.

## Use

1. On a Wii with the Homebrew Channel, run [cdbackup](https://oscwii.org/library/app/cdbackup) and choose Backup. It writes `cdbackup.vff` to the SD card. Do not choose Restore or Delete. Both change the Message Board file on the console.
2. Build the CLI: `make`.
3. Check what it found:

   ```sh
   build/wii2posthog cdbackup.vff --utc-offset -04:00 --dry-run
   ```

4. Send it:

   ```sh
   build/wii2posthog cdbackup.vff --utc-offset -04:00 --host https://us.i.posthog.com --api-key phc_...
   ```

The Wii stores local time without a time zone, so `--utc-offset` is the offset of the place where the console was used.

## The Wii app

Build the folder for the SD card:

```sh
make sd HOST=https://us.i.posthog.com API_KEY=phc_... UTC_OFFSET=-04:00
```

Copy `build/sd/apps` to the top of the card. The Homebrew Channel then lists the app as "PostHog for Wii".

The app reads `apps/posthog-wii/settings.txt` from the SD card, which `make sd` writes:

```
host = https://us.i.posthog.com
api_key = phc_...
utc_offset = -04:00
```

- `host` starts with `https://` or `http://`. Without a host and a project token the app sends nothing.
- For an `https://` host the app checks the server's certificate. It trusts the five root certificates of Amazon Trust Services, which signs PostHog Cloud. For a host with another certificate authority, put that authority's root certificate, as PEM text, in `apps/posthog-wii/roots.pem`.
- The certificate check needs the console's date to be right.
- An `https://` host also needs `apps/posthog-wii/seed.bin`, which `make sd` writes. A Wii has no source of random numbers, so the keys of a secure connection start from random bytes that the computer made. The app replaces the file on each run that sends over https.

Each run sends only the sessions that are newer than the last one it sent. The app stores the start time of that session in `apps/posthog-wii/sent.txt`. Delete the file to send everything again.

The app writes two files, `sent.txt` and `seed.bin`, both on the SD card.

## What is sent

For each session, the events carry the game code, the maker code, the title as the Wii Menu shows it, the start and end times, and an ID derived from the console.
Neither tool reads messages, photos, Miis or friend names.

## Safety

- The Wii app cannot write to the console's internal storage. The build replaces the one function that writes a file there, and fails if a binary contains a way around it. The console's own system software still records each title it starts, as it does for every game.
- It installs nothing on the console: no channel, no WAD, no IOS. It runs from the SD card.

## Limits

- The Wii records sessions, not what happens inside a game.
- It records only what is launched from the Wii Menu.
- It keeps 10 sessions per day. Later sessions that day lose their title, and both tools skip them.

## Development

- `make test` runs the tests under AddressSanitizer and UndefinedBehaviorSanitizer.
- `uvx pre-commit install` sets up the hooks in `.pre-commit-config.yaml`. Before each commit they format C and Python files, run Ruff, ShellCheck and zizmor, scan for secrets, and refuse a Wii dump.
- `make lint` runs every hook on every file, then clang-tidy on every C file. It builds the Wii app first, which also runs the check that the app cannot write the console's internal storage. A hook that formats a file fails once, so run it again.
- CI runs `make`, `make test` and `make lint` on every pull request.

`CLAUDE.md` holds the project's design, safety and comment rules.

## License

MIT. See `LICENSE`. The pictures that the build downloads from `@posthog/hedgehog-mode` and `@posthog/brand` keep their own licenses. The `@posthog/brand` license does not allow redistribution, so do not share a built `boot.dol` or `icon.png`.

Not affiliated with Nintendo.
