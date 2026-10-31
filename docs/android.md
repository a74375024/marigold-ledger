# Marigold on Android (Fire TV, any Android TV stick)

The same app as on the LG TV: the same screens, guide, Live Events, playback
rules and saved data format, built from the same code. Only the layer that
talks to the device is different.

## Install on a Fire TV

1. On the Fire TV: Settings › My Fire TV › Developer options: turn on
   **Apps from Unknown Sources** (newer models: **Install unknown apps**,
   then allow the app you'll install with). If Developer options is missing,
   open Settings › My Fire TV › About and press OK on the device name seven
   times.
2. Download the APK from the CI run's artifacts (`marigold-android-NN`,
   a zip with `marigold-NN.apk`) and get it onto the stick, either:
   - with the **Downloader** app (from the Amazon Appstore): put the APK
     somewhere it can fetch over your network and enter that address; or
   - with adb from a computer on the same network: turn on **ADB
     debugging** in Developer options, then
     `adb connect <fire-tv-ip>` and `adb install -r marigold-NN.apk`.
3. Marigold appears under Apps (and can be moved to the home row).

Updates install over the old version (`adb install -r`, or Downloader
again) and keep your accounts and settings, as long as every build is signed
with the same key (see Signing).

Fire OS 6 and newer (2017 sticks onward) and Android TV 7 and newer are
supported. Both 32-bit and 64-bit ARM are in the APK.

## Remote

| Fire TV remote | Does |
|---|---|
| D-pad, select | Move, OK (hold select for the long press) |
| Back | Back; twice on the guide exits, as on the TV |
| Menu (☰) | Guide: the channel's actions (Watch, Remind Me, My Lineup, Choose Feed), as holding OK. Live Events: Where to Watch. Player: Sources |
| Play/pause (⏯) | Guide: Live Events. Live Events: back to the guide. Player: as on the LG remote |
| Rewind, fast forward (⏪ ⏩) | Guide: page up and down. Player: as on the LG remote |
| Home | The system's (the app keeps running; opening it again is instant) |

The Fire TV remote has no colour, number or CH keys, so on a Fire TV the
key hints on screen show the keys above (☰, ⏯, ⏪ ⏩) instead of the LG
colour keys. Settings and Search are the icons at the top right of the
guide (Up to the tabs, then Right). The LG app is unchanged.

### Any other Android TV stick or box

The app runs on any Android TV device (Android 7 or newer; Chromecast with
Google TV, Nvidia Shield, Onn and Xiaomi boxes, TVs with Android TV
built in). Their remotes differ, so the hints start with only what every
remote has: hold OK for a channel's actions, Up for Live Events, Search
and Settings, Down then OK in the player for Sources, Back for the guide.
Every action is reachable that way, with the D-pad alone.

The first time a key is pressed that the remote has (Menu, play/pause,
rewind and fast forward, CH up and down, number keys, the colour keys),
the hints add it from then on. A remote with colour keys gets the LG
hints. Game controllers work too (A is OK, B is Back).

## How it is built

```
src/core/          the engine and every rule: shared, host-tested
src/app/           screens, UI kit, the app loop: shared
src/platform/      the device layer
  *.hpp            the interfaces (window and events, HTTP transport,
                   fonts, image decoding, video, storage)
  http.cpp, text.cpp, image.cpp, gfx.cpp, logo_cache.cpp, icons.cpp,
  lan_share.cpp    shared implementations (the worker pool, the text cache,
                   the GLES2 renderer, logos, sharing)
  window.cpp, http_rede.cpp, text_sdl.cpp, image_sdl.cpp, video.cpp,
  storage.cpp, ts_proxy.cpp   webOS backends (SDL, libcurl, LS2)
  android/         Android backends (EGL, JNI to Java) and the entry point
android/           the Gradle project: a thin Java shell (Bridge.java is
                   every call between Java and native)
```

A rule changed in `src/core` or a screen changed in `src/app` changes both
apps. The device layer is a set of small, separate files behind the same
headers, so a backend can't drift from the interface without one of the two
builds failing. CI builds both on every push, and
`tools/android-jni-check.py` checks every call between Java and native
matches on both sides.

What differs on Android:

- **Video:** ExoPlayer (androidx.media3) on a surface under the UI. The UI
  clears a hole where the picture goes, exactly as over the TV's video
  plane. HLS plays directly (no local TS proxy: that was a webOS
  workaround). A link of the wrong kind (HLS without `.m3u8`) is retried
  as the other kind once.
- **Text:** the same Inter Display fonts, drawn by Android (missing glyphs,
  like emoji, fall back to the system's fonts).
- **Keyboard:** a one-line box with the Fire TV's on-screen keyboard.
- **Diagnostics** (Settings › Help & info) shows the system, GPU, video state and
  the log; the TV's key test screen is webOS only.
- **Log:** the same file in the app's data, and logcat (tag `marigold`),
  redacted the same way.

## What has been checked without a Fire TV

On an Android 9 emulator at 1920x1080 (logcat only, no screenshots), with
JNI checking on, against a made-up playlist, guide, logos and streams
served from the build machine:

- Sign in to an M3U account and to an Xtream account (server address,
  user name, password) with the remote and the on-screen keyboard.
- The channel list downloads, sorts and saves; the logos download and
  decode; the XMLTV guide downloads and matches every channel.
- Playback, full screen and in the guide's corner: an HLS channel,
  Xtream's /live/ TS channels (endless live streams), and an HLS playlist
  with no .m3u8 in its link (played after one retry as HLS).
- Home and back, Back twice to exit (a clean shutdown), and a cold start
  from the saved list.
- Live Events, search, options and the accounts screen open and close
  with the remote, with no errors.

Not checked there (no outside network): real providers, HTTPS, a LAN
Emby server, sound and picture quality, and the real Fire TV remote. The
Fire TV checklist is `docs/android-test.md`.

## Build locally

Needs JDK 17 and the Android SDK with NDK 27.2.12479018 and CMake 3.22.1.

```
cd android
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew assembleRelease        # app/build/outputs/apk/release/app-release.apk
./gradlew assembleRelease -PmarigoldAbis=x86_64   # for an emulator
```

## Signing

Every build must be signed with the same key to install over the previous
one. CI signs with a release key when the repository has these secrets
(Settings › Secrets and variables › Actions):

- `MARIGOLD_KEYSTORE_B64`: the keystore file, base64
- `MARIGOLD_KEYSTORE_PASSWORD`, `MARIGOLD_KEY_ALIAS`, `MARIGOLD_KEY_PASSWORD`

To make one: `keytool -genkeypair -v -keystore marigold.jks -alias marigold
-keyalg RSA -keysize 2048 -validity 10000`, then `base64 -w0 marigold.jks`.
Keep the file out of the repository (hygiene refuses `.jks` files).

Without them, CI signs with one debug key it keeps in its cache, so builds
still install over each other. If that cache is ever dropped (unused for a
week), the next build has a new key and needs an uninstall first, which
clears the app's data. Adding a release key avoids that.
