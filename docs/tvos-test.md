# Apple TV: first checks on a real Apple TV

Install as in `docs/tvos.md`. Everything else in the TV checklists (`m1`,
`m3`, `m4`) applies as written: the screens and rules are the same code.
These rows are what is new on Apple TV.

## Start and look

| Area | How to check | Pass |
|---|---|---|
| Launch | Open Marigold from the Home screen | Its icon tilts with the remote; the app opens full screen in a few seconds |
| Top Shelf | Move Marigold to the top row | The Marigold banner above it |
| First run | No accounts yet | The accounts screen, as on the TV |
| Text | Any screen | Inter Display, crisp |
| Icons | Live Events title tiles | The sport line icons, white, black when focused |
| 4K TV | A 4K Apple TV | The UI fills the screen, sharp (drawn at 1080p and scaled) |

## Remote

| Area | How to check | Pass |
|---|---|---|
| Clickpad | Press the ring up, down, left, right around the guide | Moves one step; held, it repeats |
| Short swipe | A short quick swipe on the touch surface (the first Siri Remote: the whole top), each way, around the guide | Exactly one step per swipe, never two |
| Long swipe | Slide slowly across the whole surface, then halfway | Several steps (about one per third of the surface), along one axis only even when the slide slants |
| Flick | A fast flick across the surface | A few steps, then 2–4 more after the thumb lifts, slowing, then it stops |
| Resting thumb | Rest the thumb on the surface; wobble it a little | Nothing moves |
| Stop a coast | Flick, then press Back (or any button) at once | The coast stops there |
| Ring presses | Press the ring around the guide; hold it | Still one step per press; held, it repeats (unchanged by swipes) |
| Player swipes | Controls up, one short swipe sideways | One button along, no channel change |
| Player long swipe | Controls down, a long swipe or flick up; then controls up, a long swipe sideways | One channel change; one button along (a swipe is one move in the player) |
| Glide at an edge | Search: a long swipe or flick toward the edge of the letter grid | The glide stops on the edge letter; one more swipe crosses it (the Search screen's job) |
| Keyboard | With the system keyboard up, swipe | Only the keyboard moves; the app behind doesn't |
| Click and hold | Click a channel; then hold the click on one | Tunes; held, the channel's actions (Watch, Remind Me, My Lineup, Choose Feed) |
| Key hints | The guide's bottom bar | "Hold OK · Channel options" and "⏯ Live Events"; no coloured squares |
| ⏯ | ⏯ on the guide, then ⏯ again | Live Events opens, then back to the guide |
| Back | Back from a panel, then the player | Closes each, back to the guide |
| Back on the guide | Back with nothing open on the guide | The Apple TV Home screen at once |
| Return | Open Marigold again | The guide as you left it, at once |
| TV button | Press it in the player | Home; the picture stopped. Back in Marigold: the guide, and the channel starts again when chosen |
| Settings | Up to the tabs, Right to the gear, OK | All settings opens |
| TV remote | A TV remote over HDMI with CH or number keys | Works; its keys join the hints once pressed |

## Keyboard

| Area | How to check | Pass |
|---|---|---|
| Search | Search, then type on its grid | Marigold's own keyboard (the same as on LG and Fire TV); results follow as you type. See Search (Build 113) below |
| Speak | 🎤 Speak, then hold the remote's microphone button and talk | Apple's keyboard appears only then; the text comes back into Marigold's field and focus returns to Speak, where ▶ reaches the top result |
| Cancel | Back on Apple's keyboard | It closes; the field keeps the text |
| Accounts | Add an account, fill each field | Each field opens the keyboard with its current value |

## Playback

| Area | How to check | Pass |
|---|---|---|
| Full screen | OK a live channel | The picture full screen, the right shape (letterboxed if it isn't 16:9), sound |
| Corner | The guide's corner preview | The picture in the corner, the guide drawn around it |
| TS | A plain .ts channel (Xtream) | Plays |
| HLS | A channel that is HLS (.m3u8) | Plays |
| MPEG-2 | A broadcast channel in MPEG-2 or with MP2 sound, if you have one | Plays, with sound |
| Headers | A provider that needs its referer or user agent | Plays |
| Failover | A channel that is down | The same backup feed and messages as on the TV |
| Screen saver | Leave the guide idle; then leave a channel playing | The screen saver starts on the guide, never over a playing picture |

## Data and network

| Area | How to check | Pass |
|---|---|---|
| Saved | Add an account; leave the app, swipe it away in the app switcher (TV button twice); open it | Still there, with its channels from the cache |
| Kept for good | After a reinstall of the same build | Accounts, settings and My Lineup kept; the Help log shows no "Keychain unavailable" (if it does, passwords are in a file Apple TV may clear) |
| Plain http | A provider on http:// | Loads |
| Emby | A LAN Emby server with its own certificate | Connects (allow local network access when asked) |
| Share | Settings › Help & info › Send a report to your phone › Channel report | The QR code; a phone on the Wi-Fi gets the file |
| Diagnostics | Settings › Help & info › Diagnostics | tvOS version, model, GPU, video state and the last log lines; no stream links |

## Search (Build 113)

Everything in `docs/m4-test.md` › Search (Build 113) applies; these rows are what differs on the Siri Remote.

| Area | How to check | Pass |
|---|---|---|
| Ring | Press the clickpad's ring around the grid | One key per press, with repeat; only the centre click types |
| Swipe | A long swipe right from "a" | Several keys with acceleration; it stops at the grid's edge. One more swipe crosses to the results |
| Swipe in the results | A long swipe down the results, then left | One item per swipe step; it stops at the list's edge; a new swipe left from a row's first item returns to the keyboard |
| Speaking copy | Look under the grid | "To speak: choose Speak, then hold the microphone button on the remote and say what you want." |
| Siri button | The Siri button with Search open (not from Speak) | The system's Siri, as everywhere else |
| Click and hold | Click and hold on a result | Its More panel (Follow, Where to Watch, Remind me…) |
| Key bar | The keyboard, then the results | "Swipe or ring Move · Centre click Type · ⏯ 123"; "Click and hold More · ◀ Keyboard · Back: Keyboard" |
| Muted corner | Rest 2 s on a live channel in the results, then click | The corner plays without sound (VLC); full screen has sound at once, no re-tune |
| Guide corner | The guide's preview after using Search | With sound, as before |
| HDMI remote digits | A TV remote over HDMI with number keys | As on LG |

### Search keyboard and scope (Build 115)

Everything in `docs/m4-test.md` › Search keyboard and scope (Build 115) applies; these rows are what differs on the Siri Remote.

| Area | How to check | Pass |
|---|---|---|
| Play/Pause | ⏯ on the letters, then again | The 123 page, then the letters; on 123 or abc the focus stays on it, on another key it moves to the key in its place |
| Key bar | The letters, then the 123 page | "Swipe or ring Move · Centre click Type · ⏯ 123", then "… ⏯ abc" |
| Swipe on 123 | A long swipe right from 1 | It stops on 3, the keypad's edge (not at the panel's margin); one more swipe crosses to the results |
| Swipe along the last row | A long swipe right from y | It stops on Space; one more swipe crosses |
| Swipe down | A long swipe down from 2 on the 123 page | It stops on abc |
| More outside | Swipe down to the last row, click | Everything, marked as on LG; click and hold on that row does nothing |

## Caption timing (Build 115)

Everything in `docs/m4-test.md` › Caption timing (Build 115) about the rows applies. VLC draws the captions; Caption timing becomes its subtitle delay.

| Area | How to check | Pass |
|---|---|---|
| Later | Caption timing 2 s later, on a channel with captions | Captions show 2 s later at once (no re-tune) |
| Earlier | Caption timing 1.5 s earlier | Captions on time; the Settings row's text says Apple TV can only hold them back |
| One channel | Aa › Only this channel, 1 s later; change channel and back | 1 s later only on that channel |
| Log | The playback log after 2 minutes with captions | "caption timing: clock player; setting +2.0 s" once a minute |

## Picture check (Build 115)

Everything in `docs/m4-test.md` › Picture check (Build 115) applies, from VLC's signals (no relay on Apple TV): a picture once VLC has displayed one (displayedPictures > 0); the displayed count must keep increasing while the clock runs; the video tracks VLC lists. Black and static pictures are not detected here.

| Area | How to check | Pass |
|---|---|---|
| Picture facts | Any channel | Log: "video · 1920x1080 · h264 · aac" once after "picture"; on an Apple TV HD an HEVC feed says "hevc (no hardware decoder)" |
| No video track | A feed with only sound | After 6 s "picture check · no video track (the player lists none)", the next version |
| Sound, no frame | A feed whose video never displays while the sound plays | After 8 s a failed start, one retry, then the next version (before Build 115 it waited for "no picture after 10 s") |
| Frozen | A stream whose displayed pictures stop while the clock runs (an HEVC 4K feed on an Apple TV HD) | After 15 s (8 s at the start) one reconnect, then the next version |
| Radio | A radio channel | Plays with the sound alone (VLC's clock running), no check |
| No picture anywhere | Every version sound only | The card with Try another source and Keep listening |
