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

## Live Events: only events

| Area | How to check | Pass |
|---|---|---|
| Shows stay out | Live Events on a weekday, in NFL, NBA and College Football | No tiles for shows about the sport ("NBA Today", "NFL Insiders", "College Football Live") or replays and classics of old games; only the day's real games |
| Fight cards | Live Events the week after a UFC card | No tiles for last week's card ("UFC 333 Prelims"); next card under its day |
| Kept | The same rows | Whip-arounds (RedZone) on Sundays; games the schedule doesn't list (smaller conferences) still shown |

## East/West feeds and the last feed

| Area | How to check | Pass |
|---|---|---|
| West market | My Lineup with a Pacific or Mountain market (San Francisco, Denver) | Premium channels your provider has twice (HBO, HBO West) appear once, as the West version; Options › My Lineup › East/West feeds reads "Automatic: West" |
| East market | Set the market to an Eastern or Central one (New York, Chicago) | The East versions instead |
| Setting | East/West feeds › East feeds, West feeds, Both | My Lineup changes at once; Both shows both versions; the other version is always in All Channels and search |
| Added by hand | Add the other version in Edit My Lineup | It stays whichever coast is set |
| Last feed | Pick another feed for a channel in Where to Watch, leave, then open the channel again | It starts on the feed you watched last (a pinned feed still wins) |

## Live Events: search, filter, options and preview

| Area | How to check | Pass |
|---|---|---|
| Same places | Open the guide, then Live Events | The clock, the preview corner and the three round buttons sit exactly where the guide has them; the settings button is a gear |
| Buttons | Up to the tabs, Right past Live Events | Search, Filter, Options in turn; OK opens each; Left from Search goes back to the tabs; Down or Back returns to the tiles |
| Filter | Filter › Live now, My teams, or a sport | Only those tiles, each sport in its own row; a "● Live now ×" chip after the tabs and a red dot on the filter button; Back clears it |
| Not in my channels | Filter › Games not in my channels | The same switch as in Options; the board updates |
| Preview | Rest 2 s on a live game in your channels | It plays in the corner (no big faded badges then); OK goes full screen on the same stream with no re-tune |
| No preview | Rest on an upcoming or final game, or a game you can't watch | No corner; the badges show as before; a preview that was playing stops |
| Setting | Options › Preview channels off | No preview anywhere |
| Leaving | Back to the guide, or Back from a game you watched | The guide's preview doesn't carry into Live Events; back from full screen the game returns to the corner |

## First-run setup and phone sign-in

Start with no providers (a fresh install, or remove them all in Options › Providers).

| Area | How to check | Pass |
|---|---|---|
| Welcome | Open the app | "Welcome" with "Add it from your phone" focused and "Use the remote"; seven steps along the bottom; the key line shows OK (LG, Fire TV) or Click (Apple TV) |
| Scan | Pick "Add it from your phone" | A QR code, the TV's address and a 6-digit code, three numbered lines, "Waiting for your phone…" with the minutes left |
| Phone page | Scan the code with a phone on the same Wi-Fi | A page headed "Marigold · your TV" with Xtream / Emby / Jellyfin / M3U; the TV changes to "Phone connected" |
| No camera | Open the address on a phone or computer, type the code | The same page; five wrong codes bring a new code on the TV |
| Sign in | Fill in a provider and tap Send to TV | The TV shows "Got it from your phone", signs in and loads channels, then moves on to Area by itself; the phone says "Signed in" |
| Wrong password | Send a wrong password | The phone marks the user name and password fields and says why; the TV says the same; send again from the phone |
| Remote way | Back to Welcome, "Use the remote" | Today's form with "STEP 1 OF 7"; signing in moves on to Area |
| Area, Languages, Channels | Continue through them | One screen each; the busiest area is picked; LG CH ▲▼ / Fire TV ⏪ ⏩ jump a letter in the A to Z list; Back goes to the step before |
| Sports, Teams | Continue | The busiest leagues are picked; teams in a rail by league with the reminder switch; letter keys jump in the team grid |
| Done | Continue | Channel count, today's games, your teams; "Start watching" opens the guide, "Go to Live Events" the board; My Sports doesn't pop up later |
| Skip | Skip any step after the provider | It moves on and keeps that step's defaults |
| From Options | Options › Providers › "Add from your phone" | The same scan screen; it closes with "Provider added" once the channels are in |
| Network | Put the phone on mobile data | The page doesn't open; "Use the remote instead" works |
| Apple TV | First scan screen on Apple TV | tvOS asks once to allow local network access; after allowing, the phone page opens |

## East and West versions, one channel at a time

A provider with both versions of a channel (HBO and HBO West). Options › East/West Feeds on Automatic.

| Area | How to check | Pass |
|---|---|---|
| Guide tags | Look at the pair in All Channels | A blue EAST tag on the East version and an amber WEST tag on the West one; the West row sits right below the East one; channels without a pair are unchanged |
| Also on | Focus a show on the East version | Above the show title: the tag and "Also on HBO West at …" when the same show is on later there |
| Hold OK | Hold OK (LG), hold Select (Fire TV), click and hold (Apple TV) on the version in My Lineup | "Watch HBO West" with what's on now, and "+ Add HBO West to My Lineup"; adding it puts it right below HBO in My Lineup |
| Player | Watch the East version, press Down | "Watch West ↔" next to Sources; OK switches in place, the banner tag turns WEST, the pill reads "Watch East ↔"; Last goes back |
| Sources | Open Sources while watching | VERSION with East and West (the playing one ticked, what's on the other); OK on the other switches; "+ Add … to My Lineup" adds it, then the row reads "Both versions are in My Lineup" (OK removes it) |
| Edit My Lineup | Options › Edit My Lineup | EAST & WEST with East / West / Both for each pair; Left / Right changes My Lineup at once and the choice stays when the East/West setting changes |

## College games from Divisions II and III

Channels with small-college soccer, field hockey or volleyball (event channels titled "Harbor College vs Valley College").

| Area | How to check | Pass |
|---|---|---|
| Matched | Open Live Events, look at Soccer, Field Hockey and Volleyball | Division II and III games show both schools and the tag "DIVISION III · WOMEN'S" (or II, MEN'S); Division I women's volleyball and field hockey are matched too, before My Sports is set up |
| Scores | A game on now | The score with the period and clock as the NCAA gives them ("2nd Half 61:05", "Halftime"); "Final" when over |
| Only yours | Scroll the rows | No list of other colleges' games under "Not in your channels" |
| My Sports | Set up My Sports without Soccer, then with it | Division II and III soccer only with a soccer pick; field hockey and volleyball only with those sports |

## More schedules for the sports your channels show

Before My Sports is set up, the sports your event channels have games of are looked up too.

| Area | How to check | Pass |
|---|---|---|
| Rugby | Rugby games on your channels (Top 14, Premiership, URC) | Tiles with both clubs and scores, Saturday's round included ("Montpellier vs Castres") |
| College football | FCS and Division II/III games (event channels) | Matched in College Football, with logos; a separate "Football" row is mostly gone |
| College hockey | College hockey on your channels | Matched in Hockey with scores; Division III from the NCAA pages |
| Replays | Titles with a past date ("Harbor vs Valley 9 Feb", "Sun 6th") | Not in Live Events |
| No sport named | A title like "Temple vs East Carolina" with no sport | Matched to the game at that time (volleyball Friday, not soccer Thursday) |
| Only yours | College rows | No lists of other colleges' games under "Not in your channels" unless that league is picked in My Sports |

