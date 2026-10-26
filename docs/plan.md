# App plan (draft for review)

Status: proposal, no code yet. Written 2026-10-26.

The app is a native webOS app with two parts: Live TV and Live Events.
It ports the Apple TV design from the tvOS fork (`multiview` branch,
`Core/LiveTV` and `UI/LiveTV`, about 23k lines of Swift) to C++17.
It vendors a pinned, trimmed copy of the platform and IPTV layers of
Nuvio Native Legacy (GPL-3, C on SDL2/GLES2, upstream commit `287694ce`).

Everything below comes from reading both codebases. The feasibility
note (Addenda 1–3) was not available when this was written, so its
findings still need to be reconciled with this plan.

## 1. Layers

```
+-------------------------------------------------------------+
| ui/        screens, overlays, focus, colour keys     (C++)  |
+-------------------------------------------------------------+
| core/      catalog, lineup, guide, playback policy,         |
|            sports schedule, matcher, scores          (C++)  |
|            pure logic: no platform headers, host-testable    |
+-------------------------------------------------------------+
| platform/  thin C++ facades: Video, Http, Gfx, Text,        |
|            Input, Storage, Clock                     (C++)  |
+-------------------------------------------------------------+
| vendor/legacy/  trimmed Legacy C: LS2 video, curl, GLES2,   |
|            SDL_ttf text, keys, Xtream/XMLTV/M3U parsers,    |
|            TS proxy, stall watchdog                  (C)    |
+-------------------------------------------------------------+
| TV system libs: SDL2, SDL2_image/ttf, GLESv2, EGL, zlib,    |
| libcurl (dlopen), luna-service2 (dlopen), libjpeg, libwebp  |
+-------------------------------------------------------------+
```

Rules:

- `core/` never includes a platform or vendor header.
  - It is built and tested on the CI host under ASan, UBSan and TSan.
  - Everything the TV can't easily debug lives here: name matching, schedule logic, failover decisions and request budgets.
- `platform/` is the only code that calls Legacy C.
  - It turns C handles and callbacks into RAII objects and plain events, so Portuguese names stop at this boundary.
- `ui/` talks only to `core/` and `platform/`.

### Threads and memory

These rules answer the bug classes in Legacy's crash study (`docs/plans/rust-piloto.md`): lifetimes, races and fixed buffers.

- **One UI thread** handles render, input and all `core` state changes.
- **A small worker pool** (4 threads) does network, gunzip and parsing.
  - Workers return results through one locked queue.
  - The UI thread drains that queue once per frame.
  - Workers never touch UI or catalog state.
- **Large data is published as immutable snapshots** (`std::shared_ptr<const Catalog>`, `<const Listings>`, `<const Schedule>`).
  - A rebuild produces a new snapshot and swaps the pointer on the UI thread.
  - No stack object is ever handed to a thread. That is the 28% crash class in Legacy.
- **No fixed-size buffers in our code.** We use `std::string`, `std::vector` and `std::string_view` slices with checked bounds.
  - Legacy's vendored C keeps its buffers. Every place we call it with a `cap` is wrapped so the result is clamped (the `epg_faixa` > cap class).
- **Bounded memory:**
  - channel strings in one arena, about 250 B per channel;
  - at most 30k channels;
  - guide programmes kept as compact records (minutes since epoch, length, interned strings, flags);
  - the guide window is −3 h to +48 h;
  - descriptions are capped at 200 chars and kept only for the next 8 h and for sports;
  - the logo texture cache has a RAM budget.
  - These numbers come from the tvOS `OutsideGuide` and Legacy `ltv.c`.

## 2. Folder layout

```
CMakeLists.txt
cmake/            toolchain-webos.cmake, sanitizers.cmake, warnings.cmake
app/              appinfo.json, icons, splash (packaged into the .ipk)
src/
  core/
    util/         time and zones, text folding, streaming JSON, budgets, LRU
    sources/      Xtream and Emby request builders + parsers, accounts, M3U
    catalog/      traits, clustering, families, events, core networks, lineup
    guide/        provider guide, outside XMLTV, matching, clock shift, merge, genres
    playback/     feed ordering, reliability, failover state machine, source memory, flip lists
    sports/       schedule, matcher, network map, live scores, My Sports, race data,
                  special feeds (cameras, whip-arounds), player photos, reminders
  platform/       video.hpp, http.hpp, gfx.hpp, text.hpp, input.hpp, storage.hpp
  ui/
    kit/          focus graph, panels, rows, pills, toggles, toasts, colour-key bar
    guide/        guide grid, hero, preview, rail, time menu, filter, search, detail card
    events/       board, tiles, hero, sports column, Where to Watch, detail card
    player/       banner, mini guide, event guide, Sources, cameras, tuning card, notes
    setup/        accounts, My Lineup wizard, My Sports wizard, Options
  main.cpp
vendor/legacy/    trimmed Legacy files, kept byte-identical where possible
  UPSTREAM.md     commit, file list, local patches, last sync
  patches/        one .patch per local change, so an upstream sync can replay them
tests/            doctest unit tests; synthetic fixture generators (no real channel data)
tools/            Dockerfile (SDK), build-ipk.sh, mkipk.sh, hygiene.sh, mock provider
docs/             this plan
.github/workflows/ci.yml
LICENSE, CREDITS.md (added with the first Legacy file)
```

## 3. What we vendor from Legacy

Upstream: `iqui27/nuvio-native-legacy`, pinned at `287694ce4a0b` (2026-10-25).

- Files are copied whole and trimmed by deleting code.
- Licence headers are kept, and every local change is also saved as a patch in `vendor/legacy/patches/`.
- Upstream fixes are pulled selectively: read the diff of a file we carry, replay our patches, and run CI.

Line counts below are upstream `wc -l`.

### Take (platform)

| Legacy file | Lines | Use | Trim |
|---|---|---|---|
| `video.c` / `video.h` | 3990 / 562 | LS2 to `com.webos.media` (uMS), plane via libAcb (webOS ≤4) or SDL exported window (5+), events, audio tracks, screensaver hold | Drop the DV guard, DTS state and subtitle code (1–224), MKV probe and credits (1977–2282), the DTS/DV path (2884–3254), DV/MKV API (3571–3740), subtitles (3777–3903), playback speed (3904–3948), DolbyHdrInfo, moviebuffer and cacheboost. Keep the plane, events, `tocar`, reconnect, headers, lean live payload (modo 2), stop, window/crop, audio choice and shutdown. About 2.4k lines remain |
| `video_modo.c`, `video_escala.h`, `video_reconexao.h` | 7 / 43 / 147 | live load mode, dest-rect scaling, reconnect state | none |
| `lsregistro.c/.h`, `webosver.c/.h`, `app_id.h`, `mediaurl.h` | 109 / 278 / 17 / 18 | LS2 register backoff, webOS version, app and bus ids | rename the ids |
| `livestall.h` | 172 | stall watchdog (pure) | none |
| `proxyts.c/.h` | 789 / 51 | local HLS→continuous TS proxy, one session, 16 MiB ring | Lower the per-segment cap from 48 MiB to 8 MiB. Replace the `ajustes_livetv_resolucao` extern with a parameter |
| `livetv_regras.h`, `ts_sonda.h` | 233 / 151 | resolution and name rules; TS probe (PAT/PMT, scrambling) | none |
| `rede.c/.h`, `rede_pedido.inc`, `redeurl.c`, `negcache.*` | 4031 / 512 / 403 / 178 / 255 | libcurl via `dlopen` (so.5, then .4), per-thread handles, ETag, streaming download, body-stall watchdog, credential stripping for logs | Drop the Emscripten/VIDAA branch (194–1135), Android/TPK branches, the TMDB fallback and the Discord TLS socket. Enable the shared DNS cache (today `NV_TPK` only) on webOS. About 2.9k lines remain |
| `js.c/.h` | 475 | tolerant JSON reader | none (used for small bodies only, see §5) |
| `dados.c/.h` | 894 | data dir, atomic writes | Android cache include removed |
| `gfx.c/.h`, `gl_compat.h`, `perftv.*`, `sdlcompat.h`, `anim.h` | 5201 / 852 / 59 / 52 / 102 / 170 | GLES2 rects, textures, clipping, video hole (`gfx_furo`) | Keep init, rect, colour, texture, clip, hole and icons. Drop glass/blur/ambient/snapshot/FBO effects, `corviva`, `gpunivel` and SGSR upscaling. About 1.5k lines remain |
| `text.c/.h`, `bidi.c`, `dobra.c` | 2532 / 380 / 320 / 66 | SDL_ttf text with a per-string texture cache and fallback fonts | Replace the app style enum with our own small set. Drop the subtitle fonts. Keep Latin, plus CJK and Arabic fallback through system fonts |
| `jpegrapido.c/.h`, `webp.c/.h`, `vendor/jpeg62/` | 477 / 834 / 1882 | JPEG (TV libjpeg.so.62, `dlopen`), WebP (TV libwebp.so.7) | Add the IJG licence file that upstream lacks |
| `ponteiro.c/.h` | 1211 | Magic Remote pointer: hover to focus, wheel to arrows, cursor show/hide | `gfx_*` calls go through our facade |
| `teclavoltar.h`, `chsegura.c/.h`, `layout.h` key codes | 22 / 106 / ~10 | Back detection, CH+ hold vs tap, scancodes (Back 482, CH 480/481, red 486, green 487, yellow 488, blue 489) | Take only the key lines of `layout.h` |
| `entrada_texto.c/.h` | 567 | LG system keyboard via `SDL_StartTextInput` | Keep the webOS branch only |
| Fonts from `deploy/app/fonts` | — | Inter Display (OFL) + `DroidSansFallback-Subset` (Apache) | Only the weights we use |

### Take (IPTV data)

| Legacy file | Lines | Use | Trim |
|---|---|---|---|
| `xtream.c/.h`, `iptvcontexto.inc` | 1133 / 64 | `player_api.php`: account, categories, URL building, connection limits | Drop the Emscripten relay. Replace `get_live_streams` whole-body parsing with our streaming parser (§5) |
| `xtepg.c/.h` | 385 | `get_short_epg`, one at a time, 30 min validity | none |
| `xmltv.c/.h` | 316 | streaming XMLTV, gzip auto, 32 KiB element buffer | none |
| `ltv.c/.h` | 734 | string arena, source keys, cap rules, streaming M3U parser, digit zapping | Our catalog owns the arena; keep the M3U parser and the digit timer |
| `m3u_parse.c`, `idcanal.c/.h`, `iptvid.h` | 134 / 90 / 43 | M3U attributes, stable channel ids (never contain credentials) | none |

### Read, don't vendor

These files are references only. Their behaviour is rewritten in `core/`, because the file mixes UI with logic or depends on app globals.

- **`guia.c` (7,890 lines).** It holds the loader, the per-source cache, the channel cap and EPG matching inside UI code.
  - Legacy's automatic load admits channel by channel and can cut inside a group. Only the "Choose groups" screen admits whole groups (`ltv_grupos_admitir`). A separate 48-category cap also applies.
  - Ours admits whole provider groups in provider order until 30k channels, with no category cap.
- **`app.c`.** Source choice, proxy and direct fallback, and the open deadlines (25/12/45/15 s). These live in `core/playback`.
- **`epg.c`.** The cascaded matcher and store. We port the tvOS `OutsideGuide` matcher instead, and reuse Legacy's `xmltv.c` reader underneath.
- **`player.c`, `aovivoui.c`, `teclado.c`, `tex_cache.c`.** UI and poster-cache code tied to the old app.
  - We write a small logo cache (LRU, RAM budget, disk copy, trimmed and dark-detected like tvOS `ChannelLogoLoader`).

### Never take

- `src/dts/`, MKV/ASS, DV7, `rust/`, QuickJS, FFmpeg, libass/freetype/harfbuzz.
- `idioma_*` (191k lines of translations), add-ons, sync, Trakt, profiles.
- Tizen, VIDAA and Android players.

### Shims we write (`vendor/legacy/shim/`)

- `i18n()` as identity.
- A few `ajustes_*` values passed in from our settings: live resolution, format, proxy and wait time. Style getters are constants; DV/DTS flags are 0.
- `perfis_ativo()` returning 1.
- `memlog_livre_mb()` from `/proc/meminfo`.
- `esmaecer_*` mapped to the LS2 screensaver hold.
- `marco()` and `ilha_avisar()` as no-ops.

**Size:** about 22–27k lines of Legacy remain after trimming, against 42.8k before.

## 4. Porting the tvOS logic

The Swift files map almost one-to-one onto `core/` modules.

| Swift (lines) | C++ module | Notes |
|---|---|---|
| ChannelCatalog (1147), CoreChannels (160), ChannelLogoTable (632, data) | `catalog/traits`, `catalog/cluster`, `catalog/families`, `catalog/events`, `catalog/core_networks` | cluster keys, quality, source tag, West, country/market, call signs, kinds, families (≥15 or ≥5 with ≥40% `NN:`), event title time parsing |
| LiveTVWork (317), LiveTVLineup (170), MarketDirectory (157) | `catalog/annotate`, `catalog/lineup`, `catalog/markets` | group prefix annotation, rename-only refresh, suggestion rules, markets cached 90 d |
| XtreamClient (273), LiveTVClient (475) | `sources/xtream`, `sources/emby` | request builders; parsing on workers; passwords never logged |
| OutsideGuide (734), OutsideGuideIO (656), ProviderGuide (188), ProgramGenre (80) | `guide/outside`, `guide/provider`, `guide/merge`, `guide/genre` | shards, backups for stale shards, call-sign and name targets, clock-shift learning, ETag/IMS, pause while playing |
| LiveTVStore playback part (~1,200 of 4,555) | `playback/feeds`, `playback/failover`, `playback/reliability`, `playback/memory` | versions ordering, pinned feed, event backups, reliability (ok+1)/(ok+fail+2) with 0.25 margin, game source memory (40 entries, 6 h) |
| LiveTVStore other parts | `catalog/store` (snapshots), `ui/*` view models | the 4,555-line hub is split by concern |
| SportsData (1791), SportsEvents (1247), LiveScores (309), MySports (145) | `sports/schedule`, `sports/matcher`, `sports/network_map`, `sports/scores`, `sports/my_sports` | the whole matcher, event state machine, live windows per sport, request caps |
| RaceData (422), SpecialFeeds (326), PlayerPhotos (186), LiveReminders (91) | `sports/race`, `sports/special_feeds`, `sports/photos`, `sports/reminders` | |
| LiveTVPlaybackLog (88) | `playback/log` | URLs stripped before a line is stored; on-screen errors stripped too |

Platform pieces that need replacements:

- **Regex.** The Swift code uses ICU patterns (look-behinds, `\b`, Unicode ranges).
  - Hot paths (name normalization, call signs, quality tokens) become hand-written tokenizers.
  - The remaining date and title patterns use PCRE2 (BSD), statically linked with JIT off.
  - `std::regex` is too slow on an ARM TV and lacks look-behind.
- **Time zones.** The app needs US Eastern, UK, CET and about 12 more zones.
  - These are a small built-in rule table. Event times only span about a week, so we don't depend on the TV's zoneinfo.
- **Gzip:** system zlib. **XML:** Legacy `xmltv.c`. **JSON:** a streaming tokenizer for big arrays (`get_live_streams`, scoreboards over 700 KB) and Legacy `js.c` for small bodies.
- **Storage:**
  - JSON files in the app data dir, written atomically through `dados.c`.
  - Account passwords go in a separate 0600 file, never in logs or exports.
  - The TV has no keychain, so the file permission is the protection.
- **Images:** SDL2_image, libjpeg and libwebp decode logos into our cache.

## 5. Efficiency and request load

The tvOS budgets carry over unchanged:

| Data | Rule |
|---|---|
| Channel list | re-read every 30 min; per-source binary cache loads first |
| Xtream short EPG | only rows on screen, 6 in parallel, skipped where the provider guide covers |
| Provider guide (`xmltv.php`) | 12 h; in-scope channels only |
| Outside XMLTV | 20 h; ETag/IMS; backups only for stale shards; paused while video plays |
| Schedule | once a day or older than 20 h; 700 ms between leagues; backoff 1 h, 1 h, 6 h, 24 h, weekly |
| Live scores | only while the board or the event guide is open; 60 s live (120 s scores off); daily cap 500 |
| Race data | 60 a day; photos 1 per 6 s, 120 a day, kept 90 d |
| Logos | curated table first, disk cache, no refetch until evicted |

webOS-specific changes:

- **One hardware decoder.** The guide preview is the real video plane, scaled into the corner (`setDisplayWindow`).
  - Pressing OK on the previewed channel just grows the same picture to full screen. There is no second tune and no second provider connection.
- **Xtream `get_live_streams` is parsed as a stream** into the arena. Legacy reads the whole body into RAM with no cap, which is a risk on a 32-bit TV.
- **Downloads use a 4-thread pool.** Legacy caches DNS per curl handle; we turn on the shared DNS cache as well.
- **TLS:** Legacy turns certificate checks off because the TV's CA store is old.
  - We ship a current CA bundle and verify by default for schedule, score and photo hosts.
  - A provider with a broken certificate can be marked "allow insecure" per account.
- **One stream per account at a time.** Failover to another feed on the same account closes the first one before opening the next, matching Legacy's single proxy session.

## 6. Playback and failover

The policy combines the tvOS recovery rules with Legacy's stall detector.

- **Detection** (Legacy `livestall.h`, polled each frame):
  - position stuck for 8 s;
  - buffering for 8 s (HLS: 3× segment, up to 60 s);
  - a dry source: the buffer end has not grown in 15 s with less than 2 s left;
  - end of stream;
  - no first frame within 20 s.
- **Recovery** (tvOS):
  - reconnect the same feed: Xtream 2 times, Emby 1 time, +1 if the feed played over 60 s, +2 if picked by hand;
  - then move to the next feed in place;
  - give up after 6 recoveries in 180 s.
  - Startup on Xtream retries the same feed twice, 2 s apart, before moving on, to ride out connection limits.
- **Routing** (Legacy): HLS goes through the local TS proxy by default. Continuous TS goes direct, after a 302 probe.
- **Notes:** the only signs are a small top-right note ("Reconnecting…", "Switching to a backup feed…") and a toast on success. Errors never contain links.
- **Zapping:**
  - CH+/CH− and Up/Down flip within the list the channel came from, with a 350 ms debounce.
  - Right returns to the previous channel.
  - Number keys type a guide number and tune after 1.5 s.

## 7. UI

The screens follow the tvOS layout, redrawn at 1920×1080 with Legacy's GLES2 renderer. Mockups are in the design canvas shared in chat.

- **Focus:** a directional focus graph, with explicit neighbours where a grid needs them.
  - Magic Remote hover moves focus and the wheel scrolls rows.
  - Long OK opens the context menu, which tvOS gets from long-press Select.
- **Colour keys:** red Sources (Where to Watch on the board), green Add to My Lineup or Remind Me, yellow Live Events or Guide, blue Options.
  - Remotes without colour keys get the same actions from the on-screen key bar.
- **Live Events UI never names a data source.** Strings come from one English table, so this is reviewable in one file.

## 8. CI (GitHub Actions)

**Workflow `ci.yml`.** It runs on every push and pull request, with actions pinned by SHA.

1. **`hygiene`** (seconds). `tools/hygiene.sh` fails the build on any of these:
   - committed `*.csv`, `*.m3u`, log files or `local.properties`;
   - URLs with stream path shapes (Xtream `/live/` paths with a user and password, `get.php` playlist links, `.m3u8` links with query strings);
   - password or token query parameters;
   - any author identity other than the noreply one.
2. **`host`** (ubuntu, clang). CMake builds `core/`, the vendored pure C (parsers, `livestall.h`, `ltv.c`) and the tests in three configurations:
   - ASan + UBSan (`-fno-sanitize-recover`);
   - TSan;
   - plain `-O2` with `-Wall -Wextra -Werror` on our code, with warnings off for `vendor/`.
   - Tests use doctest (MIT, one header). They cover the matcher, schedule states, clustering, lineup rules, failover decisions, guide merge, clock-shift learning, time zones and request budgets.
   - A 60 s libFuzzer smoke run per parser (XMLTV, M3U, streaming JSON) runs nightly.
3. **`ipk`** (ubuntu). This job builds the app.
   - A Docker image from `tools/Dockerfile` uses openlgtv `buildroot-nc4` release `webos-a38c582`, the same as Legacy's `tools/Dockerfile`.
   - We record the tarball's sha256 on first download and verify it from then on. The image is cached between runs.
   - The build cross-compiles for armv7 softfp and packages with `tools/mkipk.sh`.
     - That script writes the `ar` archive (debian-binary, control, data) directly, so CI needs no Node or `ares-package`.
     - It then checks the file modes and that no credential files are packaged.
   - The `.ipk` is uploaded as a build artifact.
4. **Releases** are cut by hand (workflow_dispatch) as "Build NN" and attach the `.ipk`.
   - Once the repo is public, `tools/hb-repo.sh` adds the `repo.json` and manifest for the Homebrew Channel.

Test data is synthetic only. Generators produce fake providers (like tvOS `mock-xtream.py`, with about 60k invented names), schedules and guides. No real channel list, log or export is ever committed.

## 9. Milestones

Each milestone ends with a "Build NN" `.ipk` you can install.

| # | Scope | Done when |
|---|---|---|
| M0 | Toolchain, CMake, CI (hygiene, host + sanitizers, ipk), platform facades, a test screen that draws, reads every remote key and plays one stream you enter on the TV | CI green; the app opens on your TV and plays |
| M1 · Lineup + guide | Xtream and Emby accounts (LG keyboard), streaming channel load with whole-group admission up to 30k, catalog and clustering, My Lineup wizard, guide grid with hero, rail, time menu, search, provider guide, outside XMLTV, logo cache, Options (lineup and guide parts) | A 60k-channel mock loads within its memory budget; the guide scrolls at 60 fps on the TV; listings match like tvOS on the fixtures |
| M2 · Playback with backups | Video facade, preview corner, tuning card, banner, previous channel, number and CH keys, mini guide, Sources panel, failover policy, reliability scores, pinned feeds, playback log | A killed feed fails over silently on the TV; failover unit tests pass under TSan |
| M3 · Live Events with scores | Schedule, network map, matcher, board (rows, tiles, hero, sports column), Where to Watch, detail card, live scores, My Sports wizard, reminders, event guide in the player, remembered source per game | The matcher reproduces the tvOS results on synthetic fixtures; scores stay within the request caps |
| M4 · Panels, cameras, tennis | F1/NASCAR cameras panel, race data, followed drivers, whip-arounds, tennis tiles with player photos, the rest of Options | Feature parity with the tvOS `multiview` branch, minus Multiview |

Multiview is out of scope: the TV has one hardware decoder, and providers cap streams per account.

## 10. Licence and credits

- The repo stays GPL-3. Vendored files keep their headers.
- `CREDITS.md` names Legacy's author and links the upstream repo. It is added in the same commit as the first copied file.
- It also lists the IJG notice for the jpeg62 headers, the font licences (Inter Display OFL; DroidSansFallback Apache-2.0) and, if used, PCRE2 (BSD) and doctest (MIT).

## 11. Open questions

1. **LG model and webOS version, and whether the TV is rooted.** These decide:
   - the plane path (libAcb on webOS ≤4, exported window on 5+);
   - the SDL version (2.0.2–2.0.5);
   - Homebrew Channel versus Developer Mode.
   - Legacy has only proven video on a rooted 2019 model. On a non-rooted TV the LS2 role is untested and may show a black picture.
2. The feasibility note with Addenda 1–3 is not in any branch of the tvOS repo, so this plan could not check against it.
3. **M3U accounts.** tvOS has none, but Legacy's parser is cheap to keep. Do you want them in M1?
4. **The app id and the name shown on the TV.** The proposal is a neutral id, `org.marigold.ledger`, and a title you choose.
