# Releases

A release is made when the version changes: raise `project(marigold VERSION x.y.z)`
in `CMakeLists.txt`, and the next green CI run on `master` publishes release
`vx.y.z` with the four apps attached under the same names every time:

| File | Device |
|---|---|
| `marigold-lg-webos.ipk` | LG TV (webOS, Developer Mode) |
| `marigold-android.apk` | Fire TV, Android TV |
| `marigold-apple-tv.ipa` | Apple TV (unsigned; sideload) |
| `marigold-mac.dmg` | Mac |

Each downloads directly (no sign-in):
`https://github.com/a74375024/marigold-ledger/releases/latest/download/<file>`
always gets the newest release; `.../releases/download/vx.y.z/<file>` a given one.

Other builds of the same version make no release; their apps stay in the CI
run's artifacts as before. The version shown in the app is `x.y.z`; on LG and
Android the package version is `x.y.<build>` (the CI run number).

## Updates in the app (Build 148)

The Android / Fire TV app and the Mac app find a new release themselves:
they read `https://api.github.com/repos/a74375024/marigold-ledger/releases/latest`
(at most once a day, unauthenticated), and offer it when its tag is a plain
`vx.y.z` above their own version and it carries their file
(`marigold-android.apk`, `marigold-mac.dmg`) with GitHub's SHA-256 `digest`.
The download must match that digest. So for an update to reach the apps:

- the tag stays `vx.y.z` (no suffix: `v1.0.2-beta` is ignored), and the
  release is neither a draft nor a prerelease;
- the file names stay as in the table above;
- the APK is signed with the same key as the one installed (the release key
  from `MARIGOLD_KEYSTORE_B64` when set, else CI's cached debug key):
  Android installs an update only over the same signer, and the app refuses
  one signed otherwise ("Couldn't update").

The LG and Apple TV apps never look (webOS Developer Mode and the Apple TV's
signing service install them). An app takes in-app updates from the first
release that has the updater (after 1.0.1); that one is installed by hand.
