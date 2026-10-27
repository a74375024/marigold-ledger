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

## Sport profiles (steps 1–2)

| Area | How to check | Pass |
|---|---|---|
| Nothing else moved | Browse Live Events with games of several sports on | Same tiles, times, "may have ended", icons and chips as before |
| Whole-word sports | Live Events after the first launch (the channel list is sorted again once) | No shows filed under a sport because a word hid inside another ("conflict", "multiple", "obligation"); "… FC" games found in Soccer |

## Sport profiles (steps 3–5) and runners on base

| Area | How to check | Pass |
|---|---|---|
| Golf, cricket | A golf or cricket broadcast with a long guide listing | Stays LIVE until the listing ends (up to 12 h), then "Likely over" |
| Long tennis | A men's Grand Slam singles match | LIVE for about 4 h before "Likely over" |
| Soccer knockouts | A cup or playoff match without live scores | "Likely over" lasts about an hour (extra time, penalties) before it ends |
| Suspended | A baseball or tennis game suspended by rain (scores on) | Yellow SUSPENDED pill, still in Live now; football or soccer "suspended" still reads POSTPONED |
| Doubleheader | An MLB doubleheader with provider feeds that give no time | Game 1's tile has them until game 2 starts, then game 2's; a "Game 2" feed goes to game 2 |
| Runners on base | A live baseball game, scores on | A small diamond before "2 outs · 1-2"; filled bases match the runners |
| Penalties | A soccer knockout going to penalties, scores on | "(4)" beside each side's score, the shootout's winner bright; footer "Penalties" while it's on |
| Aggregate | A second leg, scores on | Footer "Aggregate 3–1" (home first, as on the tile); none on a first leg |

## Tennis flags

| Area | How to check | Pass |
|---|---|---|
| Singles | Live Events with tennis on | Each player's country flag before the name; the three-letter code until it loads; no photo |
| Doubles | A doubles match | Two small flags per pair (blank boxes until they load) |
| Focused | Focus a tennis tile | Flags still clear on the white tile |

## More sports with schedules and scores

Pick each in Options › My Sports (More sports).

| Area | How to check | Pass |
|---|---|---|
| Rugby | Pick Rugby; a Six Nations, Premiership, URC, Top 14 or Champions Cup match | Its own Rugby row; home first ("Ireland vs France"); the minute ("64'") and the score |
| Volleyball | Pick Volleyball; a college match | Sets won as the score; footer "Set 4 · 14–11" while it's on |
| Lacrosse, field hockey | Pick them; a college or PLL game | Score and "Q3 8:12" |
| Softball | Pick Softball; a college game | Innings ("Bot 5th"), outs and count, runners on base |
| PFL | Pick UFC, PFL, boxing & wrestling | PFL cards with Prelims and Main Card, like UFC |
| IndyCar, NASCAR | Pick Racing; after a race, scores on | The race tile shows the top three ("1 BEL 2 GIB 3 RED") |
| Golf | Pick Golf (or have golf feeds); a PGA Tour, LPGA, LIV or DP World round | The top two: position (T2 for ties), flag, name, holes played, score to par; the tournament in the tag; "Round 3" while it's on, "Round 3 done" between rounds |
| Golf feeds | A provider golf channel naming the tournament or the tour | Its feeds are on that tournament's tile |
| Cricket | Pick Cricket (or have cricket feeds); a match on one of your channels | Home first; scores "278/8" and "166/4" with the batting side marked; footer "Need 113 from 65 balls" in a chase, else the overs; "3rd ODI" in the tag; Stumps, rain delays and innings in the status |
| Cricket, not watchable | Many matches under way (domestic cricket) | Only matches you can watch get tiles |
| Tests | A Test match on day 3 | Still live, and day-3 feeds are on its tile |

## Title tiles and sport icons

Events from provider feeds that no schedule game matched.

| Area | How to check | Pass |
|---|---|---|
| Matchups | A feed titled like "Rugby: A vs B - Six Nations" or "Cricket: A v B - 3rd ODI" | Two sides, each with a coloured badge (the same colour every time); "Six Nations" or "3rd ODI" in the tag |
| People | Darts, boxing or snooker feeds ("A. Name vs B. Name") | Initials in a plain circle, no team colours; "Night 12" or the weight class in the tag |
| Events | Golf, MotoGP, Olympics or other event feeds | The sport's icon (not a dot or star) in the box; a clean name ("The Masters", "Men's 100m"); the series or discipline under it; the round or stage ("Round 2", "Race", "Final") in the tag |
| Icons | Look across sports: football to cycling | Each sport has its own line icon, white on the tile and black when focused |
| Chips | A mixed row (search results, Live now) with boxing, Olympics or other events | BOXING, OLYMPICS, MOTOGP, or EVENT for anything else |
| Long names | A studio show ("The ... Show: Preview") | The show's name as the title, the rest under it |
