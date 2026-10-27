# Android: first checks on a Fire TV

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
| Back | Back from a panel, the player, then the guide | Closes each; on the guide, the Fire TV home appears |
| Return | Open Marigold again after Back | The guide as you left it, quickly (the app kept running) |
| Menu | ☰ on the guide | The same as the LG Menu key |
| Media keys | Play/pause, rewind, fast forward in the player | As on LG |
| Volume, Home | Press them | The system's: volume changes, Home goes home |

## Keyboard

| Area | How to check | Pass |
|---|---|---|
| Search | Search, then type | A one-line box over the app with the on-screen keyboard; results follow as you type |
| Done | OK / Done on the keyboard | The box closes and the search runs |
| Cancel | Back on the keyboard | The box closes; the field keeps what it had |
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
| Share | Options › Help › Export Channel Report | The QR code; a phone on the Wi-Fi gets the file |
| Diagnostics | Options › Help › Diagnostics | Android version, GPU, video state and the last log lines |
| Logcat | `adb logcat -s marigold` | The app's log lines; no stream links in them |
