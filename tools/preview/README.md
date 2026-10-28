# marigold-preview

The Live Events board on a Mac, from a channel report, for quick feedback.
It runs the same engine as the TV apps: your channels from the report, and
schedules and scores fetched live when you run it. The result is one web
page that opens in your browser.

## Get it

From a CI run, download **marigold-preview-mac-NN** (Apple Silicon) and
unzip it. You get `marigold-preview` and this file.

## Run it

1. On the TV: Help › Export channel report, and get the CSV onto the Mac
   (the export screen's QR code or address).
2. In Terminal:

   ```
   cd ~/Downloads/marigold-preview-mac-NN
   xattr -d com.apple.quarantine marigold-preview   # first time only
   ./marigold-preview ~/Downloads/marigold-channels.csv
   ```

   The `xattr` line tells macOS the download is fine to run (it isn't
   signed). Without it macOS says it "can't be opened"; System Settings ›
   Privacy & Security › Open Anyway does the same.
3. After about half a minute the board opens in your browser. The page is
   saved next to the report as `marigold-live-events.html`. To put it
   elsewhere, name it: `./marigold-preview report.csv ~/Desktop/board.html`.

Run it again for a fresh board (new scores, games moving from Later to Live).

## Using the page

- The chips at the top show one row (sport) at a time; a red dot means
  something in it is live. The search box finds teams or titles.
- "Time not listed" and "Event channels" are hidden at first (they're
  long); untick the box to see them.
- Click a tile for its detail card: the lines the TV's card shows, where to
  watch it (your channels and every provider feed), and how the tile was
  made (the schedule game it matched, or built from the channel title; its
  live status).
- Each tile has a number (#4). **Copy reference** on the detail card copies
  "Soccer › Live #4 · Harbor vs Valley" and the tile's id: paste that with
  your note so the exact tile can be found.

## What it is and isn't

- Same data and decisions as the TV; a web version of the tiles, close but
  not pixel-exact.
- No video, no guide, player or onboarding.
- Your report stays on the Mac. Only schedule and score requests go out;
  provider servers, guides and streams are never contacted (the report has
  no stream links).
- My Sports and settings are the defaults (no teams picked); My Lineup is
  the one in the report.
