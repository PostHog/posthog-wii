# posthog-wii

Send your Wii play history to PostHog.

The Wii System Menu records every game and channel you launch, and shows the totals as the "Today's Accomplishments" letter on the Message Board.
This project reads those records and sends each play session to a PostHog project as two events, `game started` and `game ended`.

## Parts

- `wii2posthog`, a command-line tool that reads a Message Board dump on a computer and sends it.
- A Wii app that reads the same data on the console, sends it, and then runs a small game.

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

## Settings for the Wii app

The Wii app reads `apps/posthog-wii/settings.txt` from the SD card:

```
host = https://us.i.posthog.com
api_key = phc_...
utc_offset = -04:00
```

- `host` starts with `https://` or `http://`. Without a host and a project token the app sends nothing.
- For an `https://` host the app checks the server's certificate. It trusts Amazon Root CA 1, which signs PostHog Cloud. For a host with another certificate authority, put that authority's root certificate, as PEM text, in `apps/posthog-wii/roots.pem`.
- The certificate check needs the console's date to be right.

## What is sent

For each session: the game code, the maker code, the title as the Wii Menu shows it, the start and end times, and an ID derived from the console.
Messages, photos, Miis and friend names are never read.

## Safety

- The Wii app cannot write to the console's internal storage. The build replaces the one function that writes a file there, and fails if a binary contains a way around it.
- It installs nothing on the console: no channel, no WAD, no IOS. It runs from the SD card.

## Limits

- The Wii records sessions, not what happens inside a game.
- It records only what is launched from the Wii Menu.
- It keeps 10 sessions per day. Later sessions that day lose their title and are skipped.

Not affiliated with Nintendo.
