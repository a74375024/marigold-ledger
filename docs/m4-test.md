# M4: race cameras, tennis, Help exports (and the remaining gaps)

Install over the previous build as in `docs/m0-test.md`.

## Races and tennis

| Area | How to check | Pass |
|---|---|---|
| Camera hint | Watch an F1 or NASCAR race that has onboard feeds | "Press Down for onboard cameras" |
| Cameras panel | Down while watching it | Bottom panel: LIVE and the race, quick picks (main, mixed onboard, tracker, data…), drivers in team colours |
| Switch | OK on a camera | Plays it; the race stays on screen (Left still opens the event guide, Down reopens the panel) |
| On screen | Reopen the panel | The camera playing is outlined and says "now" |
| Follow | Hold OK (or Play/Pause) on a driver | A star; followed drivers come first next time |
| Driver photos | Open the panel during an F1 weekend | Initials in team colours, then photos as they arrive |
| Podium | A finished qualifying or race session (scores on) | "1 VER 2 NOR 3 LEC" with team colours on the tile; hidden with scores off |
| Tennis | A tennis row | Player photos (initials until they load) on each side; a pair icon for doubles |

Requests: race drivers and results only for race weekends you can watch
(spaced, at most 60 a day); player photos only for players on the board
(6 s apart, at most 120 a day, kept 90 days).

## Help exports

| Area | How to check | Pass |
|---|---|---|
| Channel report | Options › Help › Export Channel Report | "Building the report…", then a QR code and a link |
| Open on a phone | Scan the code on the same Wi-Fi | A CSV downloads; no passwords, links or server names in it |
| Countdown | Wait | "The link works for 9:59…"; after 10 minutes, "Sharing stopped." |
| Stop | Stop Sharing | The link stops working at once |
| Playback log | Options › Help › Export Playback Log | Tunes, feeds tried, stalls and errors; never a stream link |
| Clear | Clear Log | The log starts again |

## The other gaps

| Area | How to check | Pass |
|---|---|---|
| M3U guide | An M3U playlist with a guide link (url-tvg, or typed in) | Its channels get listings within a few minutes |
| Logos | All Channels | More national channels show logos; a broken logo falls back to the next one |
| More logos | My Lineup: MTV2, Fuse, Revolt, Fox Weather, Cinemax and Starz channels, NBA TV, Tennis Channel, The Weather Channel | Their own logos, alone in the cell like the national networks |
| White logos | Focus a channel whose logo is white (on the white focused cell) | A dark version of the logo; part-white logos sit on a small dark plate |
| Logo report | Export Channel Report | logo_sources ("logo set, provider") and logo_status ("shown", "failed") per channel; "provider" is always last |
| Free streaming | Free streaming off in My Lineup setup | Channels your provider also lists under Roku, Pluto, Tubi... (50 Cent Action, FailArmy, Tastemade) stay out of My Lineup; HBO, Starz, MGM+ and the national networks stay in |
| Preview and Live Events | A preview playing in the guide, then the yellow key | The preview stops; back in the guide it starts again |
| Edit My Lineup | Options › Edit My Lineup | Added by you / Removed by you / My Lineup; removing and adding back work |
| Countries | Options › Countries & Languages | Checking a country adds its channels to All Channels and Live Events |
| Guide Listings | Options › Guide Listings | Provider guide status, outside listings toggles and files, Refresh |
| Groups | All Channels, hold OK › Hide Group; or the filter's Provider groups | The group leaves All Channels; Options › Provider groups brings it back |
| Clock | Options › Clock | 24-hour times in the guide and on Live Events tiles |
| Home city | Options › Home city (two or more markets) | Its stations are listed first on games |
| Remind Me | OK on a later show | Remind Me / Watch; other airings listed; a card when it starts |
| Search | Search for a team or a show | Channels, Live Events, and Shows on now & later |
| My Sports | Open Live Events for the first time | My Sports setup opens once |

## Player

| Area | How to check | Pass |
|---|---|---|
| First time | Play any channel full screen for the first time | "Getting around" card (Guide, Last channel, Channels, Info, Sources, Number keys); OK closes it, it never shows again |
| Banner | Start a channel, or OK while watching | Logo on a dark plate, number and name, LIVE and the show, times and progress, Next; under it the keys: ◀ Guide, ▶ Last channel, ▼ More, OK Info, red Sources (backups), blue Options; hides after 6 s |
| Banner on bright content | Start a channel showing snow, a pitch or a bright studio | Title, channel line, times and keys all readable; the top half of the picture isn't darkened |
| Action row | Down while the banner is up | Pills: Sources (feed), Guide, Back to the last channel, Favorite, More (Cameras first on a race); Left/Right, OK, Up hides it |
| Sources | Sources pill or the red key | Side panel over the picture: each feed playing / backup / why it failed lately; OK switches at once |
| Switching | Pick another feed | "Switching to Feed 2" with the feed and the 20 s rule; then the picture |
| Backup | A feed that stops while watching | Corner card "Switched to a backup: Feed 1 stopped, so Feed 2 (HD) took over" for 8 s |
| Nothing works | A channel whose feeds all fail | Card with Try Again, Choose a Source, Back to the last channel, and why each feed failed |
| Pointer | Move the Magic Remote | Guide and last-channel buttons at the edges, the action row clickable |
| Leave from Live Events | Watch a game from Live Events (or a game's page, or search), then Back | The stream stops; nothing keeps playing behind the board |
| Leave to the guide | Watch from the guide, then Back | The picture shrinks into the guide's corner (or stops with previews off) |

## Large provider lists

| Area | How to check | Pass |
|---|---|---|
| Over 30,000 channels | A provider with more channels than the cap, US checked in Countries | All Channels and My Lineup include the US groups; the groups left out are other countries' |
| Change countries | Check another country in Options › Countries & Languages | The list loads again with that country's groups kept first |
| Other countries | US checked only | All Channels and channel search show US (and unmarked) channels; Live Events still finds games on other countries' sports and event channels (UK, Canada, DAZN, PPV) |

## Memory

| Area | How to check | Pass |
|---|---|---|
| Reading | Options › Help | "Memory in use: 212 MB (peak 340 MB)" above the version |
| Log | Use the app for an hour, then Export Playback Log | "memory" lines every 5 minutes, before each tune, and after "channels sorted", "listings merged", "outside listings built", "provider guide built", "channel list checked" |
| Half-hourly refresh | Leave the guide open past 30 minutes | "channel list checked" (nothing sorted again) when the provider's list hasn't changed; memory stays level |
| Unchanged launch | Quit and reopen the app (the provider's list unchanged) | "channel list checked" rather than "channels sorted" after the download; the first screen opens as fast as before |
| Startup memory | Export the playback log after a launch | The "memory" lines after loading and the guide builds stay well under the earlier peaks |
| No restarts | A long session browsing and watching | No "restart to free up memory" |

## Engine move (nothing should look different)

The guide's lists and the playback session now come from the shared engine. Each row should behave exactly as before.

| Area | How to check | Pass |
|---|---|---|
| Guide tabs | My Lineup, Favorites, Recent, All Channels | Same channels, order and neighborhood headers as before; Recent has no headers |
| Filters | Filter › Sports on now, a neighborhood, a provider group | Same counts beside each choice; the grid shows only those channels; Back clears it |
| Search | Type a channel name, a channel number and a local station ("nbc") | Exact name first, then the number, then names that start with it |
| Mini guide | Down in the player | The list the channel was picked from, under its headers, the playing one focused |
| Flip and LAST | CH+ / CH− past the end of the list, then ▶ Last channel | Wraps around; LAST goes back to the channel watched before |
| Backup takes over | A channel whose first feed fails | Same retries, then the next feed; the corner card says which feed took over |
| HLS and Emby | A channel with an HLS feed, and an Emby channel | Both start as before (HLS through the local relay, Emby opened on the server) |
| Playback log | Export Playback Log after the above | Tunes, retries and memory lines as before; no links |
