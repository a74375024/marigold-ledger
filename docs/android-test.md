# Android: first checks on a Fire TV (or another Android TV stick)

Install as in `docs/android.md`. Everything else in the TV checklists
(`m1`, `m3`, `m4`) applies as written: the screens and rules are the same
code. These rows are what is new on Android.

## Start and look

| Area | How to check | Pass |
|---|---|---|
| Launch | Open Marigold from Apps | The Marigold banner in Apps; the app opens full screen in a few seconds, no system bars |
| First run | No accounts yet | The accounts screen, as on the TV |
| Text | Any screen | Inter Display, crisp; emoji in channel names show (the system's emoji font) |
| Icons | Live Events title tiles | The sport line icons, white, black when focused |
| 4K TV | A 4K Fire TV or TV | The UI fills the screen, sharp (drawn at 1080p and scaled by the device) |

## Remote

| Area | How to check | Pass |
|---|---|---|
| D-pad and select | Move around the guide; OK a channel | Moves and tunes; holding select gives the long press (as holding OK on LG) |
| Back | Back from a panel, then the player | Closes each, back to the guide |
| Exit | Back twice on the guide | "Press Back again to exit", then the Fire TV home |
| Home and return | Home, then open Marigold again | The guide as you left it, at once (the app kept running) |
| Cold start | Open Marigold after exiting | The guide from the saved list in a few seconds |
| Key hints | Look at the guide's bottom bar | ☰ Channel options, ⏯ Live Events, ⏪ ⏩ Page; no coloured squares |
| Menu | ☰ on a channel in the guide; ☰ on Live Events; ☰ in the player | The channel's actions (Watch, Remind Me, My Lineup, Choose Feed); Where to Watch; Sources |
| Play/pause | ⏯ on the guide, then ⏯ again | Live Events opens, then back to the guide |
| Rewind, fast forward | ⏪ and ⏩ on the guide | A page up, a page down |
| Settings without colour keys | Up to the tabs, Right to the gear, OK | All settings opens |
| Media keys | Play/pause, rewind, fast forward in the player | As on LG |
| Another Android TV remote | On a non-Fire TV stick: the guide's bar, then press ☰ or ⏯ if it has them | First only "Hold OK · Channel options"; each key pressed joins the bar |
| Volume, Home | Press them | The system's: volume changes, Home goes home |

## Keyboard

| Area | How to check | Pass |
|---|---|---|
| Search | Search, then type on its grid | Marigold's own keyboard (the same as on LG); results follow as you type. See Search (Build 113) below |
| Speak (Android TV) | 🎤 Speak, then the remote's voice button | A one-line box with the system keyboard; what you say comes back into the field; the box closes and focus is on Speak |
| Cancel | Back on the system keyboard | The box closes; the field keeps what it had |
| Accounts | Add an account, fill each field | Each field opens the box with its current value |

## Playback

| Area | How to check | Pass |
|---|---|---|
| Full screen | OK a live channel | The picture full screen, the right shape (letterboxed if it isn't 16:9), sound |
| Corner | The guide's corner preview | The picture in the corner, the guide drawn around it |
| HLS | A channel that is HLS (.m3u8) | Plays (directly: there is no TS proxy on Android) |
| TS | A plain .ts channel (Xtream) | Plays |
| Headers | A provider that needs its referer or user agent | Plays |
| Failover | A channel that is down | The same backup feed and messages as on the TV |
| Background | Home while playing, then back | The picture stopped while away; the guide when back |
| Screen saver | Leave the guide idle; then leave a channel playing | The Fire TV screen saver starts on the guide, never over a playing picture |
| Alexa | Ask Alexa something while a channel plays | The sound ducks or pauses, then comes back |

## Data and network

| Area | How to check | Pass |
|---|---|---|
| Saved | Add an account; force stop the app (Settings › Applications); open it | Still there, with its channels from the cache |
| Plain http | A provider on http:// | Loads (cleartext is allowed) |
| Emby | A LAN Emby server with its own certificate | Connects |
| Update | Install a newer build over this one | Accounts and settings kept |
| Share | Settings › Help & info › Send a report to your phone › Channel report | The QR code; a phone on the Wi-Fi gets the file |
| Diagnostics | Settings › Help & info › Diagnostics | Android version, GPU, video state and the last log lines |
| Logcat | `adb logcat -s marigold` | The app's log lines; no stream links in them |

## Search (Build 113)

Everything in `docs/m4-test.md` › Search (Build 113) applies; these rows are what differs on a Fire TV.

| Area | How to check | Pass |
|---|---|---|
| No Speak | Open Search on a Fire TV | No 🎤 Speak pill; Up from the grid's top row stays there |
| Voice into Amazon's keyboard | Try the remote's voice button with Search open | Note what happens (Alexa, or text into the field), for deciding whether Fire TV gets Speak |
| Key bar | The keyboard, then the results | Nothing but "▶ Choose a result · Back: Close" on the keyboard; "☰ More" and "◀ Keyboard · Back: Keyboard" in the results |
| ☰ | ☰ on the keyboard, then on a result | Nothing on the keyboard; on a result, its More panel (as hold OK) |
| Numbers | 2 0 6 on the grid, ▶, OK | Sports Net 2 (or your channel 206) plays |
| Muted corner | Rest 2 s on a live channel in the results, then OK | The corner plays without sound; full screen has sound at once, no re-tune |
| Guide corner | The guide's preview after using Search | With sound, as before |
| Back from the player | OK a result, Back within 2 minutes | The same row; the picture back in the corner, muted |
