# Marigold on Apple TV

The same app as on the LG TV and Android: the same screens, guide, Live
Events, playback rules and saved data format, built from the same code.
Only the layer that talks to the device is different.

## Install

CI builds an unsigned app for every push: the `marigold-tvos-NN`
artifact, a zip with `marigold-tvos-NN.ipa`.

1. Download the artifact and unzip it to get the `.ipa`.
2. Sign and install it with a signing service (Signulous, for example),
   which signs it for your Apple TV and installs it, or with Xcode on a Mac.
3. Marigold appears on the Home screen. In the top row, its Top Shelf shows
   the Marigold banner.

A new build installs over the old one. Accounts, settings, My Lineup,
favourites, reminders and My Sports stay. Signed with a different
certificate, passwords may need entering again (see Storage).

tvOS 17 and newer is supported (Apple TV HD and every Apple TV 4K).

## Remote

| Siri Remote | Does |
|---|---|
| Clickpad ring or swipes | Move (held, the arrows repeat; a long swipe moves several steps, a flick coasts a few more) |
| Click | OK (hold for the long press: a channel's actions) |
| Back | Back. On the guide, straight to the Apple TV Home screen (Apple's rule for an app's top screen: no "Press Back again") |
| ⏯ | Guide: Live Events. Live Events: back to the guide. Player: play and pause |
| TV | The system's (Home). The app stays in memory and opens where it was |

The key hints show what the Siri Remote has: Hold OK for a channel's
actions and ⏯ for Live Events. Search and Settings are in the left rail's
strip (◀ to the menu). Search has Marigold's own A–Z keyboard, the same as
on LG and Fire TV: ring presses move one key, swipes move faster and stop
at the grid's edge, and only the centre click types; 🎤 Speak hands over
to Apple's keyboard (the hidden text field) for dictation. A TV's own remote over HDMI
that has page, number or colour keys adds them to the hints the first time
they are pressed, as on Android.

## How it is built

```
src/core/            the engine and every rule: shared, host-tested
src/app/             screens, UI kit, the app loop: shared
src/platform/        the device layer: the interfaces and the shared code
                     (worker pool, text cache, logos, sharing); Android and
                     tvOS also share diagnostics_basic.cpp, files_posix.cpp,
                     http_groups.cpp and ts_proxy_none.cpp
  tvos/              the tvOS backends (Objective-C++) and the entry point
    main.mm          the UIKit scene and view controller: the video view
                     under the UI's Metal layer, remote presses and swipes,
                     the system keyboard, the app thread
    window.mm        the event queue the app thread reads
    gfx_metal.mm     the renderer over Metal (the GLES2 one's shader and
                     blending, compiled when the app starts)
    text.mm          Core Text with the same Inter Display fonts
    image.mm         ImageIO (PNG, JPEG, GIF, WebP)
    http.mm          NSURLSession
    storage.mm       files, the defaults and the Keychain (see Storage)
    video.mm         VLC (TVVLCKit)
    Assets.xcassets  the app icon (two layers) and Top Shelf images
tools/build-tvos.sh  builds the .ipa (or the Simulator app) with Xcode
tools/tvos-smoke.sh  starts it in the tvOS Simulator and reads its log
```

A rule changed in `src/core` or a screen changed in `src/app` changes all
three apps. CI builds and tests each on every push; the Apple TV job also
runs the host tests with Apple's compiler.

What differs on Apple TV:

- **Video:** VLC (TVVLCKit 3.7.4, LGPL 2.1, fetched by the build at a pinned
  version and checksum, `tools/tvos.conf`). Apple's own player can't play a
  continuous TS stream over HTTP, which most Xtream and M3U channels are,
  nor MPEG-2 video or MP2 audio. VLC plays those and HLS, with hardware
  decoding. It draws into a view under the UI, which clears a hole where the
  picture goes, as on the other two. The user agent and referrer of a feed
  are passed to it; other headers aren't (VLC's HTTP has no place for them).
- **Drawing:** Metal. Apple deprecated OpenGL ES years ago and it crashes in
  the Simulator. The renderer draws the same shapes with the same shader.
- **Storage:** an Apple TV keeps only about 500 KB for good. Anything else
  (Caches) may be deleted when the device runs short of space. So:
  - what you made (accounts, settings, My Lineup, favourites, reminders,
    My Sports, picked feeds) is also kept in the app's defaults, and comes
    back if the system deletes its file;
  - passwords and tokens go in the Keychain (if the Keychain refuses,
    which can happen with some signing, they're kept as a private file
    and the log says so);
  - the channel list, guides, logos and scores are rebuilt from your
    sources, as on a first start.
  Which file is which is `engine::fileKind` (`src/core/engine/file_kind.cpp`).
- **Keyboard:** the Apple TV's full-screen keyboard.
- **Leaving:** TV or Back on the guide sends the app to the background.
  Apple TV suspends it within seconds: it stops the video first, and a
  channel starts again when you return.
- **Log:** the same file in the app's data, and the system log (subsystem
  `org.marigold.app`), redacted the same way.
- **Diagnostics** (Settings › Help & info) shows the system, GPU, video state and
  the log, as on Android.

## What has been checked

In CI, on GitHub's macOS runner: the host tests pass with Apple's compiler,
the app builds for the device and for the Simulator, and in the tvOS
Simulator it starts, opens its window, compiles its shader, shows frames
and keeps running (read from its log, no screenshots).

Not checked yet: anything on a real Apple TV, including the Siri Remote,
playback, sound and picture quality, and real providers. The checklist is
`docs/tvos-test.md`.
