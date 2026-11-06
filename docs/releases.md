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
