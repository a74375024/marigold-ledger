# M0: installing and checking the test build

Each CI run on `master` uploads `org.marigold.app_0.1.<run>_arm.ipk` as the
artifact `marigold-build-<run>` (kept 30 days). Releases are cut by hand as
"Build NN".

## Install

Same two routes as Legacy:

- **Developer Mode** (any webOS TV, no root):
  1. Install "Developer Mode" from the LG Content Store, sign in with an LG developer account, turn on Dev Mode and Key Server, and reboot.
  2. On a computer: `ares-setup-device` (the TV's IP, port 9922, user `prisoner`), then `ares-novacom --device tv --getkey` with the passphrase the app shows.
  3. `ares-install --device tv org.marigold.app_0.1.<run>_arm.ipk`.
  4. Dev Mode expires after 50 hours unless "Extend" is pressed in the Developer Mode app.
- **Homebrew Channel** (rooted TV): install the `.ipk` from its file installer.
  - The repository URL route needs this repo to be public.

## What the test screen checks

| Area | How to check | Pass |
|---|---|---|
| Drawing | The screen opens with the Marigold header, two panels and the log strip | Text is sharp, no flicker |
| See-through surface | Header line says "see-through surface: yes" | yes |
| Remote | Press every key on the checklist once | Each chip turns green; the "Last keys" list shows the raw scancode for anything unexpected |
| Pointer | Move the Magic Remote, click, scroll | Coordinates, click and wheel counts change |
| Keyboard | "Enter stream link", type or paste a channel link | The panel shows "Link set: N characters"; the link itself is never shown |
| Video, full screen | "Play full screen" | Picture plays; the status pill shows Playing and the size; Back stops |
| Video, corner | "Play in corner" | Picture plays inside the outlined box while the screen stays usable |
| Load modes | Toggle "Load mode" between 0 and 2 and play again | Note which mode works on your TV |
| Exit | Back twice, or the Exit key | App closes |

The log is `marigold.log` in the app data folder (the previous session is
`marigold-previous.log`). Links and logins are removed from every line before
it is written.
