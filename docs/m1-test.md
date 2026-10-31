# M1: My Lineup and the guide

Install the `.ipk` the same way as M0 (`docs/m0-test.md`). The M0 test screen
is still there: Settings › Help & info › Diagnostics.

## First run

1. The app opens on "Welcome to Marigold". Pick the kind with Left/Right
   (Xtream, Emby / Jellyfin, M3U playlist), fill in the fields with the TV
   keyboard (OK on a field opens it) and choose Sign In.
   - Xtream: server address, user name, password.
   - Emby / Jellyfin: server address, user name, password.
   - M3U: the playlist link, optionally a guide link.
2. The guide opens and loads the channels. Big lists show "Sorting your
   channels…" for a few seconds the first time; later launches open from
   the list saved on the TV and refresh in the background.

Passwords and playlist links are stored only in the app's private secrets
file. They never appear on screen, in the log or in a channel id.

## What to check

| Area | How to check | Pass |
|---|---|---|
| Setup prompt | Before My Lineup is set up, the first row says "Make Live TV yours" | OK opens the setup |
| My Lineup setup | Pick countries, Continue, toggle kinds of channels, Build My Lineup | "My Lineup is ready" with the channel count; the guide shows only those channels |
| Guide grid | Up/Down rows, Left/Right shows | Rows under neighborhood headers; the red now line; the focused show is white |
| Listings | Rows fill in as they come on screen | "Loading guide…" turns into shows within a few seconds |
| Later in the day | Right past the last show; Back | The window moves 90 minutes; "BACK returns to Now" shows; Back returns |
| Time menu | Up from the first row (over the channel column), OK | Now / Tonight / Tomorrow…; picking one moves the grid |
| Neighborhoods | Left from the first show of a row | The "Jump to" rail; Up/Down moves, OK lands, Back cancels |
| Preview | Rest on a channel for 2 seconds | It plays in the corner with a LIVE tag |
| Watch | OK on a show that is on now | Full screen. If it was previewing, the same picture just grows |
| Player | Up/Down or CH+/CH−, Right, OK, Back | Flips channels; Right returns to the previous channel; OK shows the banner; Back returns to the guide with the picture in the corner |
| Backups | Watch a channel with several feeds whose first feed is broken | It moves to the next feed by itself; "Switched to a backup feed" at the top right |
| Sources (red) | In the guide or the player | Every feed with quality, East/West and the provider's name; picking one switches |
| My Lineup (green) | On any channel | Toast "added to / removed from My Lineup" |
| Quick settings (blue) | Toggle Preview channels, All Channels tab | The guide follows at once |
| Number keys | Type a local station's number, or a lineup number | It tunes after 1.5 seconds |
| Search | Search button (top right), type part of a name | Results as you type; OK jumps to the channel |
| Long OK | Hold OK on a channel | Watch / Add to My Lineup / Choose Feed / Favorites |
| Exit | Back twice in the guide | The app closes |

## Requests the app makes

- Channel list: once at start (after the saved list is on screen), then
  every 30 minutes while the guide is open.
- Xtream short guide: only for rows on screen with no full guide, 25 rows per
  batch, at most four batches at once.
- Xtream full guide: about every 12 hours, four seconds after start.
- Outside listings (Settings › Guide › Guide listings › Fill in missing listings): about once a day,
  eight seconds after start, asking the servers to skip unchanged files.
- Logos: once per logo, then kept on the TV; only for rows drawn on screen.
