# posthog-wii

Send your Wii play history to PostHog.

The Wii System Menu records every game and channel you launch, and shows the totals as the "Today's Accomplishments" letter on the Message Board.
This project reads those records and sends each play session to a PostHog project as two events, `game started` and `game ended`.

## Status

Prototype. The host CLI reads a Message Board dump and sends it. The Wii app does not exist yet.

## Use

1. On a Wii with the Homebrew Channel, run [cdbackup](https://oscwii.org/library/app/cdbackup). It writes `cdbackup.vff` to the SD card.
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

## What is sent

For each session: the game code, the maker code, the title as the Wii Menu shows it, the start and end times, and an ID derived from the console.
Messages, photos, Miis and friend names are never read.

## Limits

- The Wii records sessions, not what happens inside a game.
- It records only what is launched from the Wii Menu.
- It keeps 10 sessions per day. Later sessions that day lose their title and are skipped.

Not affiliated with Nintendo.
