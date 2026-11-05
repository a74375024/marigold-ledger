# Marigold on a Mac

The same app as on the LG TV, Android and Apple TV: the same screens, guide,
Live Events, playback rules and saved data format, built from the same code.
It runs in a window or full screen on any Mac with macOS 12 or newer, Apple
Silicon or Intel.

## Install

CI builds the app on every push: the `marigold-mac-NN` artifact, a zip with
`marigold-mac-NN.dmg` in it.

1. Download the artifact, unzip it and open the `.dmg`.
2. Drag Marigold onto Applications.
3. Open it. The first time, macOS stops it: the app isn't signed by a paid
   Apple developer account, so Apple hasn't checked it. Open
   **System Settings › Privacy & Security**, scroll down to "Marigold was
   blocked", click **Open Anyway** and confirm. From then on it opens like
   any app. (Or, in Terminal: `xattr -dr com.apple.quarantine
   /Applications/Marigold.app`.)

A new build replaces the old one in Applications (allow it once more the
same way). Accounts, settings, My Lineup, favourites, reminders and My
Sports stay.

## Keys, trackpad and mouse

| Mac | Does |
|---|---|
| Arrows | Move (held, they repeat) |
| Return (or enter) | OK; held, a channel's actions |
| Esc | Back. On the guide, out of full screen |
| Delete | Back, or in Search the last letter |
| Space | Play and pause; on the guide, Live Events |
| 0-9 | Channel numbers |
| Page Up, Page Down | CH + and CH - |
| Typing | Goes straight into Search (on Search, or the in-player guide's) |
| ⌘F | Full screen and back (the green button too); it remembers |
| ⌘Q, or closing the window | Quit (the app saves first) |
| Trackpad or mouse | Point and click, as with the LG's Magic Remote |
| Right-click (two-finger click) | Hold OK: a channel's actions |
| Two-finger scroll, wheel | Up and down a list |

Text fields (an account's server, user name and password; the lineup's
search) take the Mac's keyboard directly, with paste (⌘V): Return or tab
goes on to the next field, Esc leaves it as it is.

## How it is built

```
src/platform/macos/
  main.mm          AppKit: the app, its menus and window; the stage (the
                   video view under the UI's Metal layer, scaled to the
                   window with black bars where its shape differs); keys,
                   typing, the trackpad and mouse; the hidden text field;
                   quitting; the smoke test's self-tests
  CMakeLists.txt   the app, with the Apple TV's backends
  Info.plist.in
src/platform/tvos/ the backends, shared with the Apple TV: Metal drawing,
                   Core Text, ImageIO, NSURLSession, storage, VLC video and
                   the event queue (each says where the Mac differs)
tools/build-mac.sh builds Marigold.app (Apple Silicon and Intel in one),
                   signs it ad hoc and makes the .dmg
tools/mac-smoke.sh starts it and reads its log
tools/mac.conf     VLCKit's pinned version and checksum
tools/mac/         the icon (1024 points) and the smoke test's video
```

What differs from the Apple TV:

- **Window:** resizable; the app draws at 1920x1080, scaled to fit.
- **Video:** VLC for macOS (VLCKit 3.7.3, LGPL 2.1, fetched by the build at
  a pinned version and checksum, `tools/mac.conf`), the same player as on
  the Apple TV. The Mac doesn't sleep its display while a picture plays.
- **Storage:** everything in `~/Library/Application Support/Marigold/data`.
  Passwords and tokens are private files there (only your user can read
  them), as on Android: an app signed ad hoc is a stranger to the Keychain
  after every update, and macOS would ask for your Mac's password each time.
- **Leaving:** minimizing or hiding the app stops the video (as a TV's home
  screen does); in the background behind other windows it plays on. The
  Mac going to sleep stops it too.
- **Captions:** VLC draws them, as on the Apple TV. "Prefer closed captions
  and SDH" in the Mac's Accessibility settings turns them on.
- **User agent:** the Mac's own media player's.
- **Log:** the same file in the app's data, and the system log (subsystem
  `org.marigold.app`), redacted the same way.

## What has been checked

In CI, on GitHub's Apple Silicon Mac: the app builds for both kinds of Mac,
its signature verifies, and when started it opens its window, finds the
GPU, draws frames, opens and closes its text field twice with the keys
coming back to the app, plays a test video with the VLC packed inside it,
keeps running, and quits cleanly when asked (read from its log, no
screenshots).

Not checked yet: anything on your MacBook, including real providers,
playback of live channels, sound and full screen. The checklist is the
Mac section of `docs/m4-test.md`.
