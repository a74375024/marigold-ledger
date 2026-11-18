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
| Channel report | Settings › Help & info › Send a report to your phone › Channel report | "Building the report…", then a QR code and a link |
| Open on a phone | Scan the code on the same Wi-Fi | A CSV downloads; no passwords, links or server names in it |
| Countdown | Wait | "The link works for 9:59…"; after 10 minutes, "Sharing stopped." |
| Stop | Stop Sharing | The link stops working at once |
| Playback log | Settings › Help & info › Send a report to your phone › Playback log | Tunes, feeds tried, stalls and errors; never a stream link |
| Clear | Clear Log | The log starts again |

## The other gaps

| Area | How to check | Pass |
|---|---|---|
| M3U guide | An M3U playlist with a guide link (url-tvg, or typed in) | Its channels get listings within a few minutes |
| Logos | All Channels | More national channels show logos; a broken logo falls back to the next one |
| More logos | My Lineup: MTV2, Fuse, Revolt, Fox Weather, Cinemax and Starz channels, NBA TV, Tennis Channel, The Weather Channel | Their own logos, alone in the cell like the national networks |
| White logos | Focus a channel whose logo is white (on the white focused cell) | A dark version of the logo; part-white logos sit on a small dark plate |
| Logo report | Export Channel Report | logo_sources ("logo set, provider") and logo_status ("shown", "failed") per channel; "provider" is always last |
| Directory report | Export Channel Report | directory_id (the channel directory's entry, when it has one) and directory_match ("exact", "other name", "loose", "ambiguous", "none", or "none (another country: PK)") per channel |
| Free streaming | Free streaming off in My Lineup setup | Channels your provider also lists under Roku, Pluto, Tubi... (50 Cent Action, FailArmy, Tastemade) stay out of My Lineup; HBO, Starz, MGM+ and the national networks stay in |
| Preview and Live Events | A preview playing in the guide, then the yellow key | The preview stops; back in the guide it starts again |
| Edit My Lineup | Settings › My Lineup › Edit My Lineup | Added by you / Removed by you / My Lineup; removing and adding back work |
| Countries | Settings › My Lineup › Countries & languages | Checking a country adds its channels to All Channels and Live Events |
| Guide listings | Settings › Guide › Guide listings | Outside listings toggles and Refresh; the provider guide status and files are in Help & info › Status |
| Groups | All Channels, hold OK › Hide Group; or the filter's Provider groups | The group leaves All Channels; Settings › Guide › Groups in All Channels brings it back |
| Clock | Settings › Guide › Clock | 24-hour times in the guide and on Live Events tiles |
| Home city | Settings › Live Events › Home city (two or more areas) | Its stations are listed first on games |
| Remind Me | OK on a later show | Remind Me / Watch; other airings listed; a card when it starts |
| Search | Search for a team or a show | The top result, then Channels, Teams, Games and Shows (Build 113) |
| My Sports | Open Live Events for the first time | My Sports setup opens once |

## Player

| Area | How to check | Pass |
|---|---|---|
| First time | Play any channel full screen for the first time | "Getting around" card (Guide, Last channel, Channels, Info, Sources, Number keys); OK closes it, it never shows again |
| Banner | Start a channel, or OK while watching | Shows at once, also before the picture (black behind it, a small spinner where the picture goes, "Tuning in" bottom right). Logo, number and name, LIVE and the show, times, progress and "N min left"; on the right "Later on this channel": the next show ("Starts in 4 min" when it's close) and the two after; under it the keys. Hides 6 s after the picture comes up |
| Banner on bright content | Start a channel showing snow, a pitch or a bright studio | Title, channel line, times and keys all readable; the top half of the picture isn't darkened |
| Action row | Down while the banner is up | Pills: Sources (feed), Guide, Back to the last channel, Favorite, Settings (Cameras first on a race); Left/Right, OK, Up hides it |
| Sources | Sources pill or the red key | Side panel over the picture: each feed playing / backup / why it failed lately; OK switches at once |
| Switching | Pick another feed | The banner stays, its status line reads "Switching to another feed · HD"; no full-screen card, no feed numbers; then the picture |
| Backup | A feed that stops while watching | The banner with "Reconnecting…" or "Switching to another feed · HD"; when the picture's back, "✓ Back on CNN" for 2 s, then the banner hides. No corner card |
| Nothing works | A channel whose feeds all fail | In the banner: "CNN isn't playing right now", one line why, and Try again / Pick a feed / Back to the last channel where the keys go |
| Messages | Favorite a channel, or flip games with Up/Down on a game | The banner comes up and its status line reads "Added to Favorites" (or "N / M · the game"); no toast box |
| Wrong number | Type a channel number that doesn't exist | The number box top right shows the number struck through and "No channel 123" under it for about 2 s; what's playing doesn't change |
| Pointer | Move the Magic Remote | Guide and last-channel buttons at the edges, the action row clickable |
| Leave from Live Events | Watch a game from Live Events (or a game's page, or search), then Back | The stream stops; nothing keeps playing behind the board |
| Leave to the guide | Watch from the guide, then Back | The picture shrinks into the guide's corner (or stops with previews off) |

## Large provider lists

| Area | How to check | Pass |
|---|---|---|
| Over 30,000 channels | A provider with more channels than the cap, US checked in Countries | All Channels and My Lineup include the US groups; the groups left out are other countries' |
| Change countries | Check another country in Settings › My Lineup › Countries & languages | The list loads again with that country's groups kept first |
| Other countries | US checked only | All Channels and channel search show US (and unmarked) channels; Live Events still finds games on other countries' sports and event channels (UK, Canada, DAZN, PPV) |

## Memory

| Area | How to check | Pass |
|---|---|---|
| Reading | Settings › Help & info | About › Memory in use: "212 MB (peak 340 MB)" under the version |
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
| Filters | Filter › Sports on now, a neighborhood, a provider group | No counts beside the choices; the grid shows only those channels; Back clears it |
| Search | Type a channel name, a channel number and a local station ("nbc") | Exact name first, then the number, then names that start with it |
| Flip and LAST | CH+ / CH− past the end of the list, then ▶ Last channel | Wraps around; LAST goes back to the channel watched before |
| Flip quickly | CH+ three times fast | The banner changes to each channel at once; only the one stopped on starts (about half a second later) |
| Backup takes over | A channel whose first feed fails | Same retries, then the next feed; the banner reads "Tuning in", then after about 7 s "Still trying · HD"; no "back on" on a first start |
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

Pick each in Settings › Live Events › My leagues (More sports).

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
| West market | My Lineup with a Pacific or Mountain market (San Francisco, Denver) | Premium channels your provider has twice (HBO, HBO West) appear once, as the West version; Settings › My Lineup › East/West feeds reads "Automatic here: West" |
| East market | Set the market to an Eastern or Central one (New York, Chicago) | The East versions instead |
| Setting | East/West feeds › East feeds, West feeds, Both | My Lineup changes at once; Both shows both versions; the other version is always in All Channels and search |
| Added by hand | Add the other version in Edit My Lineup | It stays whichever coast is set |
| Last feed | Pick another feed for a channel in Where to Watch, leave, then open the channel again | It starts on the feed you watched last (a pinned feed still wins) |

## Live Events: search, filter, options and preview

| Area | How to check | Pass |
|---|---|---|
| Same places | Open the guide, then Live Events | The clock, the preview corner and the three round buttons sit exactly where the guide has them; the settings button is a gear |
| Buttons | Up to the tabs, Right past Live Events | Search, Filter, Settings in turn; OK opens each; Left from Search goes back to the tabs; Down or Back returns to the tiles |
| Filter | Filter › Live now, My teams, or a sport | Only those tiles, each sport in its own row; a "● Live now ×" chip after the tabs and a red dot on the filter button; Back clears it |
| Not in my channels | Filter › Games not on my channels | The same switch as in Settings; the board updates |
| Preview | Rest 2 s on a live game in your channels | It plays in the corner (no big faded badges then); OK goes full screen on the same stream with no re-tune |
| No preview | Rest on an upcoming or final game, or a game you can't watch | No corner; the badges show as before; a preview that was playing stops |
| Setting | Settings › Guide › Preview channels off | No preview anywhere |
| Leaving | Back to the guide, or Back from a game you watched | The guide's preview doesn't carry into Live Events; back from full screen the game returns to the corner |

## First-run setup and phone sign-in

Start with no providers (a fresh install, or remove them all in Settings › Providers).

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
| Sports, Teams | Continue | Sixteen tiles, the big US leagues, soccer's core and the college sports ticked for everyone (Build 127); teams in a rail by league with the reminder switch; letter keys jump in the team grid |
| Done | Continue | My Lineup ready (never a channel count), today's games, your teams; "Start watching" opens the guide, "Go to Live Events" the board; My Sports doesn't pop up later |
| Skip | Skip any step after the provider | It moves on and keeps that step's defaults |
| From Settings | Settings › Providers › Add from your phone | The same scan screen; it closes with "Provider added" once the channels are in |
| Network | Put the phone on mobile data | The page doesn't open; "Use the remote instead" works |
| Apple TV | First scan screen on Apple TV | tvOS asks once to allow local network access; after allowing, the phone page opens |

## East and West versions, one channel at a time

A provider with both versions of a channel (HBO and HBO West). Settings › My Lineup › East/West feeds on Automatic.

| Area | How to check | Pass |
|---|---|---|
| Guide tags | Look at the pair in All Channels | A blue EAST tag on the East version and an amber WEST tag on the West one; the West row sits right below the East one; channels without a pair are unchanged |
| Also on | Focus a show on the East version | Above the show title: the tag and "Also on HBO West at …" when the same show is on later there |
| Hold OK | Hold OK (LG), hold Select (Fire TV), click and hold (Apple TV) on the version in My Lineup | "Watch HBO West" with what's on now, and "+ Add HBO West to My Lineup"; adding it puts it right below HBO in My Lineup |
| Player | Watch the East version, press Down | "Watch West ↔" next to Sources; OK switches in place, the banner tag turns WEST, the pill reads "Watch East ↔"; Last goes back |
| Sources | Open Sources while watching | VERSION with East and West (the playing one ticked, what's on the other); OK on the other switches; "+ Add … to My Lineup" adds it, then the row reads "Both versions are in My Lineup" (OK removes it) |
| Channel by channel | Settings › My Lineup › Channel by channel | East / West / Both for each pair; Left / Right changes My Lineup at once and the choice stays when the East/West setting changes |

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


## My Lineup: one row per channel, only your area and languages

After the update the channel list is sorted again once (a minute or so on a big list).

| Area | How to check | Pass |
|---|---|---|
| Same channel | Edit My Lineup, look for UP TV, FYI, Baby TV, The Movie Channel, Oxygen, Game Show Network, Hallmark Movies & Mysteries | One row each, every copy under Sources; The Movie Channel and Oxygen pick East or West like HBO |
| Event feeds | NBC Sports 1/2/3, NBC Sports 4K (Event Only), Tennis Channel Plus N | Not in My Lineup; under Event channels as "NBC Sports" and "Tennis Channel Plus" |
| Other languages | Channels like Aastha, ARY Digital, Mexicanal, Antena 3, the French France 24 | Not suggested for US English; they appear when you add their country or Spanish |
| Out-of-town stations | MyNetworkTV stations of other cities, WGN, PBS12 | Not suggested unless you picked that market |
| Regional channels | NECN, OC16, Spectrum News 1; Fox Sports North / South / Prime Ticket, SportsNet LA, NBC Sports Philadelphia | Only in their own area's lineup |
| Bay Area sports | NBC Sports Bay Area and California | Suggested; their part-time Plus 2 / Alternate channels only when they have a guide |
| Yours stay | A channel you added yourself | Still in My Lineup (if it was a second copy that is now part of another row, add that row again) |

## My Lineup from the channel directory

The app knows real US channels by their other names, language and closing. After the update the channel list is sorted again once.

| Area | How to check | Pass |
|---|---|---|
| Names | Edit My Lineup: MeTV, C-SPAN 3, Scripps News (was Newsy), BlazeTV, Racer Network (was MAVTV) | Each shows its real name, every copy under Sources |
| Stations | CBS KPIX, KMOV CBS 4, CBS (Buffalo), WGN | Still one row per station, by call sign |
| Closed channels | El Rey, ThrillerMax | Not suggested; still found by Search and can be added |
| Yours stay | Channels you added, removed or pinned a feed for before the update | Kept as you set them, even for a channel that has closed |

## Loading without a wait screen

| Area | How to check | Pass |
|---|---|---|
| First run | Sign in from the phone | Once "Your channel list is in" ticks, setup moves on to Area; no wait screen |
| Area | The area list right away | Areas listed at once; "Looking at your channels…" until "Suggested from your channels" fills in; the focus doesn't jump |
| Languages | Continue from Area quickly on a slow TV | If the list is still sorting, "Building your lineup" with a bar and four steps where the list goes; the countries appear by themselves |
| After an update | Install a new build over an old one, open the app | The guide opens on your list right away; the header reads "Updating your channel list…", then "✓ Channel list updated" for about 3 seconds, then fades |
| Couldn't update | Turn the provider off (or the network), then Settings › Providers › Reload channels and listings | The list stays; the header reads "Couldn't update the channel list · tries again later" for a few seconds |
| Focus | Stay on a channel while it updates | The same channel stays focused (also when it was renamed); playback never stops |
| No list at all | Add a provider in Settings | "Building your lineup" with a bar that never jumps back or sits full |
| Straight to the guide | Set up, or add the first provider, and watch the end of "Building your lineup" | The full bar goes straight to the guide: no "No channels" message in between (Build 96) |
| Next launch | Close the app after the first sort, open it again | The guide opens on your rows; no "No channels" flash while the rest of the list loads (Build 96) |
| Another provider | With channels already in the guide, add a second provider in Settings | The guide keeps your list; the header reads "Adding channels from <name>…", then "✓ Channels added"; the new channels join the list without it emptying |
| Placeholders | Open the guide before its listings are in | Rows show soft pulsing blocks where the shows and times go (no "Loading guide…"); they turn into listings as they arrive; scrolling stays smooth |
| Live Events placeholders | Open Live Events on a fresh start | Two rows of pulsing tile shapes until the games are in; no "Loading this week's games…" |
| Edit My Lineup | Settings › My Lineup › Set up My Lineup again, change something, Build My Lineup | The summary shows pulsing blocks and a dimmed "Start Watching" with "Working out your channels…"; Start Watching works once the lineup is in |
| Timing | Help › playback log after a first run | A "sorted N channels in X ms" line: tell me the number |

## Built-in logos and the LG package

| Area | How to check | Pass |
|---|---|---|
| LG install | Install the .ipk on a webOS 6 TV (LG C1) | Installs; no "ipk verified failed" |
| Fallback logos | Edit My Lineup: NRB TV, Aspire TV, DOGTV, Logo TV, Daystar, BlazeTV, BYUtv | Each has a logo |
| Same logos | Channels that had a logo before | The same logo |
| One FETV | Edit My Lineup: FETV | One row, every spelling under Sources |
| Names | Cozi TV, FYI, Great, Comedy.TV, The Movie Channel Xtra | Shown with these names |

## My Lineup and guide fixes (Build 94)

| Area | How to check | Pass |
|---|---|---|
| France 24 | My Lineup › News: France 24, then play it | English; the French feed isn't in your lineup |
| One Entertainment | Guide › Filter › Neighborhoods | Entertainment once, with all its channels; Syfy West sits in its own section's block |
| Cinemax | Edit My Lineup: Cinemax Classics, Cinemax Action | One row each, with the 5StarMax / ActionMax copies under Sources; no "5StarMax" or "ActionMax" row; both under Movies & premium |
| Premium names | MoviePlex, IndiePlex, RetroPlex, Sho x BET | Under Movies & premium, not Entertainment |
| Free streaming | Free channels off, then look for Horror Machine, Westerns Plus, Bowling TV, Mayday: Air Disaster, Travelxp, AWE Plus | Not in My Lineup; with free channels on, under Free streaming |
| Stays put | 365BLK, TBN, MeTV | Still in Entertainment (they're on the air and on cable) |
| Movie loops | CINEBOX: Family, Horror, Action | Treated like 24/7 loops: not in My Lineup |
| Overflow feeds | NBC Sports Network 2 / 3 (Feed) | Not in My Lineup |
| Logos | Stingray music channels, BabyTV, ShortsTV, France 24, Arirang TV, Travelxp, Curiosity Now, EarthX, FlixFling Now, The Victory Channel | A clean logo instead of the provider's |
| Hand-picked logos | Mayday: Air Disaster, Big A Anime, Torque by History, Bowling TV, Reelz Famous & Infamous, Stingray Spa, NBC Sports California Plus | The channel's own logo shown alone, no name beside it; NBC Sports California Plus shows the NBC Sports California logo (Build 97) |

## Live Events tiles (Build 95)

| Area | How to check | Pass |
|---|---|---|
| Sport at a glance | Live Events › Live now | Every tile's top line names the sport with its icon, then the competition ("CRICKET · 3rd Test", "TENNIS · ATP · Swiss Indoors Basel · R2"); one the app can't place reads "LIVE EVENT" by a dotted ring |
| Own row | Open a sport's row (Soccer, Tennis) | The top line gives only the competition; no repeated sport |
| Live marker | Any live game | A red dot and LIVE, then the clock in bold; no filled LIVE pill |
| Starting soon | A game within the hour | "in 12 m" in amber beside the time |
| No feed marker | Any tile | No "Event feed" chip, no "1 feed" / "3 feeds", no channel logo on the tile; the bottom line is only the game's detail (the chase, down and distance, runners, the court, "5 boards", "Onboard cameras") |
| Cricket at stumps | A Test match between days | "Not live" and "Stumps · Day 3", under "Between days" in Cricket; not in Live now |
| Cricket break | A match at lunch, tea or the innings break | A BREAK pill; still in Live now |
| Darts | A PDC Players Championship day | One tile, "Players Championship 32 · Maasport", "5 boards" on the bottom line, LIVE from noon UK time; Sources lists the boards |
| High School | Live Events | A High School row; high-school games not in Live now until My Sports › High School is picked |
| High-school titles | High School row | "KSHSAA 4A Girls Volleyball Championship" with "Seeds #3 and #6" under it; no year in front |
| Divisions II & III | Volleyball (or Soccer) row | Division I games by day first; Division II and III after them in their own group |
| Cal State | Volleyball row | "Cal State Dominguez Hills", "Cal State San Marcos" (not "Cal Saint") |

## Faster failovers (Build 99)

| Area | How to check | Pass |
|---|---|---|
| Xtream timeout | A version that hangs (Help › playback log after a stuck start) | "start failed · no picture after 10s", then the retry; never a 20 s wait on an Xtream feed |
| Emby timeout | An Emby-only local that's slow to start | Still up to 20 s before the next version |
| Can't play here | A feed that fails with "601 Resource Allocation Error" (the PDC board feed did) | The next version within a second or two, no "Retrying…", no same-version retries; Sources says "this TV can't play it" |
| Remembered | Tune that channel again | Its other versions are tried first for a week; a version you pinned still goes first |
| 203 stays a retry | A channel that fails with "203 AV Type Not Founded" | Handled as before (it means the server sent something that isn't video, and a later try can work); not remembered |

## Faster tunes (Build 100)

| Area | How to check | Pass |
|---|---|---|
| Emby preview, then elsewhere | Rest on an Emby-only channel in the guide (its preview starts), then within a few seconds press OK on an Xtream channel | The Xtream channel starts in its usual ~2 s; it never waits for the Emby preview to open |
| Emby after Emby | Change between two Emby-only channels quickly | Still one stream at a time (the server never holds two); no stuck "Loading" |
| Channel up/down | CH+ once | The new channel starts loading about half a second after the press (it was about 0.85 s) |
| Backup that played | A channel whose Xtream copy failed once and then played on Emby | Next time (or after 6 hours) it starts on Xtream again; a copy you pinned still goes first |
| Guide while tuning | Scroll a page of the guide, then press OK on an Emby channel | No extra wait for the guide's listings to load first |
| Half past | Leave the guide open across :00 or :30 | Rows whose listings still run 90 minutes ahead don't reload; rows about to run out do |
| Log | Help › playback log | "server opened · N ms" after each Emby tune; a 601 says which audio the stream carries ("(audio: dts)") |

## Lighter in the background (Build 101)

| Area | How to check | Pass |
|---|---|---|
| Half-hour refresh | Leave the guide or Live Events open across :00 / :30 | No stutter when it refreshes; renamed games update in Live Events; the guide rows and their listings stay as they were |
| Memory | Help › playback log after an hour | Notes read "… (memory handed back in N ms)"; the peaks sit lower than before (they were 430–465 MB) |
| No freeze | Scroll the guide right after a refresh | Smooth; memory is handed back only after 2 s without a key press |
| Lineup toggles | Edit My Lineup, tick several channels quickly | No pause on each tick |
| Relaunch | Close and reopen the app | Your rows appear sooner (the first-screen file only holds today's games now); the full list follows a few seconds later |
| Saved files | — | The full list and guides are saved compressed; the list isn't rewritten when only game names changed (at most every 6 h) |

## Batch 3: LG connections, rechecks, dead copies, Emby test (Build 102)

| Area | How to check | Pass |
|---|---|---|
| Faster loading on LG | Scroll the guide quickly after launch | Logos and listings fill in faster than before |
| Rechecks | Leave the app on the guide for a few hours without opening Live Events | The channel list is read about every 2 hours, not every 30 minutes; with Live Events open (or used in the last 3 hours, or a reminder coming up) still every 30 minutes |
| New games | Open Live Events after a while away | New games show up within moments (a list older than 10 minutes is read again) |
| Dead copy | A copy that sends nothing (Help › playback log) | "start failed · no data after 6s", then the retry; never a 10 s wait for a copy that sent no data (LG and Fire TV; Apple TV waits as before) |
| Emby quick start | Settings › Advanced › Experimental: Emby / Jellyfin quick start on; tune Emby-only channels (your locals) | "quick start" in the log, then a picture; if the server refuses, one retry the normal way and it plays as before. Compare the time to picture with the switch off |
| Emby to Emby | With an Emby channel playing, change to another Emby channel | No longer slower than the first one |

## Launch (Build 103)

| Area | How to check | Pass |
|---|---|---|
| Relaunch | Open the app | The splash's flower stays in place, breathes in and unfurls; the petals light in a quick sweep; it flashes, blooms out and fades while the guide comes in top to bottom; no text |
| Slow start | First setup, or a provider that's slow | After 1.5 s a line under the flower says what's happening (GETTING YOUR CHANNELS, SORTING YOUR LINEUP…); petals light only as it gets there; never a number |
| Hand-off | Any start | No jump from the splash; no "No channels" or loading screen before the guide |

## Log review fixes (Build 104)

| Area | How to check | Pass |
|---|---|---|
| Emby quick start | With the switch on, tune an Emby channel twice | The first try says "quick start · refused by the server; normal open from now on" and plays the normal way at once (no 5 s detour); after that it isn't tried on that server again |
| College names | Live Events › Soccer / Volleyball | "Cal State San Marcos", "Cal State Dominguez Hills" (the saved schedule is fetched again once) |

## Smoother starts (Build 105)

| Area | How to check | Pass |
|---|---|---|
| Start-up buffering | Change channels on LG, then Help › playback log | No "stall" logged in the first two seconds after the picture for ordinary starts; a start that really hangs still shows "Buffering…" and logs "stall · at the start" |
| Network error | A copy that fails with "300 Network Error" | The retry starts at once (no 3 s wait) |
| Fire TV | Change channels | The picture comes up about a second sooner than before |
| Apple TV | Change channels | Slightly sooner (half a second of buffer instead of one) |
| Typed numbers | Type a channel number that no longer number starts with (e.g. 702 when there's no 7020) | It tunes at once; 7 alone still waits for the next digit if 70-something exists |
| OK | Menus, Settings, the player | OK acts as you press it; in the guide, Live Events and panels with a hold menu it still waits to tell a press from a hold |

## Subtitles (Build 106)

Off until turned on; then on for every channel that has them until turned off. Marigold draws them on LG and Fire TV (the same look); Apple TV's player draws its own.

| Area | How to check | Pass |
|---|---|---|
| Turning on | Settings › Watching, or OK in the player › Subtitles | Picking a language (or On) turns them on; Off turns them off; the choice stays across channels and restarts |
| On LG | Turn subtitles on while watching a US channel | The channel restarts once (a second or two) and captions appear low and centred; a CC tag next to LIVE |
| Timing | Watch for a minute with captions | Captions appear with the speech, not seconds early |
| Banner | Press OK while captions show | They move up above the banner and back down when it hides |
| Pill | Down in the player | Subtitles pill (with the language) only on channels that carry them, or did before |
| None | Subtitles on, a channel without them | "No subtitles on this channel" once in the banner; no pill, no CC |
| Style | Subtitles › Caption style | Size, Box / Outline / Shadow, Sentence case and Top change the captions on screen at once (Apple TV: size and look from the next channel) |
| Playback log | Help › playback log | "captions · in the stream (English…)" when a stream has them; "captions · none after a minute" when it doesn't |

## Through the app, floating panels, narrow rails (Build 107)

| Area | How to check | Pass |
|---|---|---|
| Through the app (LG) | Watch normally for a day, then export the playback log | Each start says "route · through the app"; start times and failures no worse than Build 105 (Settings › Advanced › Play through Marigold off goes back to "direct") |
| Subtitles on LG | Turn subtitles on while watching | They appear without the channel restarting |
| Banner | Press OK in the player | The banner sits on a rounded, translucent card; no dark fade over the picture |
| Panels | Sources, Subtitles, Quick settings, the cameras panel | Floating cards inset from the edges, no fades |

## Live Events tiles: scoreboard rows (Build 108)

| Area | How to check | Pass |
|---|---|---|
| Size | Live Events | Tiles are shorter (360×156); more of the next row shows |
| Top line | Any tile | The sport's icon, then the state (clock, start time with "· in 25 m", Final, or a pill); the league or round at the right; the icon is red while the game is live (no separate dot) |
| Spacing | Upcoming and final games | The teams sit centred under the top line; no empty band at the bottom |
| Scores | Live and final games | Scores line up at the right; the trailing or losing side is dimmed; the winner has a small ◀ after a final; possession shows the same way while live |
| Detail | A live football or baseball game | Down and distance (red in the red zone) or runners and count under the teams |
| Sets | Tennis, volleyball | Sets in columns, the set on now bold |
| Races and cards | A race weekend, a UFC card | Title with a track of the sessions or card parts: done parts dim, the current one bold (filling red while live) |
| Scores off | Settings › Live Events › Show scores off | Same layout, no numbers, sets, podiums or game detail |

## Live Events audit, filters, steadier relay (Build 109)

| Area | How to check | Pass |
|---|---|---|
| HLN and other drops (LG) | Watch HLN through the app for 20 minutes, then export the playback log | "relay · the source closed; reopened it (N times)" where it used to say "stream ended"; no 10 s freezes |
| A stream that does end | A channel whose stream ends while watching | It reconnects at once ("reconnect · the stream ended"), not after 6 s |
| Relay failure detail | After a "proxy failed · playing directly" line | It says what the source answered ("HTTP 403 with no video", "didn't answer") |
| Direct after the relay | The same case | The direct try gets its own 6 s (no "no data after 6s" right after the switch) |
| Old board | Open Live Events after a few hours away | "Updating…" by the top buttons until the fresh board is in (a minute at most); the log says "live events · opened; the board is from 9 h ago; updating" |
| Log lines | Export the playback log after opening Live Events | "live events" lines: opened, rebuilt (with what was hidden), the board's size |
| Report | Settings › Help & info › Send a report to your phone › Live Events report | A spreadsheet of every event slot: what it was read as, the tile and rows it's on, or why it was left out |
| High school | Live Events | No High School row and none in Live now; My Sports › High School brings both back |
| TSN+ shows | Live Events with TSN+ groups | No "SC with…", "Sportscentre" or "SC: Top Characters" tiles |
| Other languages | Danish, Italian, Brazilian event feeds | "Quarterfinals", "Day 2", "Finals" in place of Kvartfinaler, Giorno 2, Finais |
| Other countries | A match only on another country's foreign-language feeds | Not in Live now; under More events |
| Failing groups | A group whose feeds keep failing to start this week | Its tiles after the others in their row (the report's group_starts says "1 of 4 started") |

## Player controls (Build 110)

The guide in the player: In-player guide (Build 116).

| Area | How to check | Pass |
|---|---|---|
| Flipping | Up, pause a second, then Down, on every remote | Down always goes back down; it never opens buttons. The banner shows where Up and Down go (their numbers) |
| CH keys | CH+ on LG (or a remote with CH keys) | Goes to the next higher number; Up goes to the channel above (Settings › Watching › Up arrow in the player: Higher number flips Up) |
| Controls | OK while watching | The banner with buttons: Subtitles first (highlighted), Aa, Sources, Guide, Back to…, Favorite, Settings. Up/Down still flip and close them |
| Subtitles | OK, then OK again (a moment later) | Subtitles switch on or off; a large notice says so and how to undo. Two quick OKs do nothing |
| Subtitle button | With subtitles on | It shows the language ("English ✓"), "On · waiting…" before captions arrive, or "no Español here" when the picked one is missing |
| Aa | OK on Aa, or hold OK on Subtitles | Languages and style in one panel; Size has Extra large |
| Languages | Español on a Spanish channel, then an English channel | The English channel keeps its own setting (Settings › Watching has a switch for each) |
| LG Green / CC key | Press it while watching | Subtitles on or off in one press, with the notice |
| Fire TV system captions | Settings › Accessibility › Captions on, back to the app | Subtitles turn on within a few seconds (and off when it's turned off) |
| One layer | Banner up, then Left | Only the guide shows; no banner under it. Back from Sources/Quick settings returns to the plain picture |
| Back | Back with the banner up, then on a clear picture | Hides the banner; on a clear picture "Press Back again to leave · ▶ Back to …"; a second Back leaves |
| Back to | Watch A, flip to B and wait for its picture (however briefly), press Right | Goes to A; Right again goes back to B. A channel left before its picture came up is skipped (Build 159) |
| Exit (LG) | Exit while the controls are up | Everything clears, the channel keeps playing |
| Numbers | Type 2, 0, 6 and press OK | Tunes 206 at once |
| Play/pause | Press it while watching | "Live TV can't be paused" |
| Android Guide / Info | Press Guide, then Info | Guide opens the guide; Info shows the banner |
| Reminder while watching | Let a reminder fire, press OK | Tunes in place; Back once leaves the player |
| Pointer (LG) | Wave the remote | The controls show with nothing highlighted; a click on the picture closes them; the wheel over a button doesn't flip |
| Apple TV swipes | Controls up, swipe along the buttons at a slant | Moves along the buttons; no channel change |

## Channel rows (Build 111)

| Area | How to check | Pass |
|---|---|---|
| Renamed channels | Find HBO Hits, HBO Drama, Cinemax Hits, MGM+ Hits in the guide | One row each, with every copy (old and new names) in Sources; no separate HBO 2 / HBO Signature / MoreMAX rows |
| Names | Look for MSNBC, Fox News, TCM | Shown as MS NOW, FOX News Channel, Turner Classic Movies |
| West feeds | Set East/West to West; open Bravo, E!, Oxygen, Syfy | Each plays its West feed; no "WILD WEST TV" row holding them |
| Stations | Your local CBS, ABC, NBC | Still one row per station with its call sign, never merged into a national row |
| Old call signs | A local listed under a call sign it has since given up | Shown under the station that holds it now |
| Shut-down channels | HBO Family | Not suggested in My Lineup (still found by search) |
| Free streaming | Free streaming off | Channels no list knows (e.g. 365BLK, Love Kills) are gone from My Lineup; show channels and sports feeds stay |
| Saved picks | Favorites, hidden channels and lineup order from before | All still apply to the merged rows |
| Logos | BYUtv, MeTV, BabyFirst, News 12 | Clean library logos instead of the provider's |
| Channel report | Export it | A row_id column says which row each stream is in |

## Game banner (Build 112)

| Area | How to check | Pass |
|---|---|---|
| Low card | Watch a live game from Live Events | The banner is one card from about two thirds down: logos, "Away at Home" (or "vs"), LIVE; under it the league, week or round and venue in small capitals; the key line below as before. The top of the picture stays clear |
| No score | The same game with Settings › Live Events › Show scores on, then off | Neither shows a score, clock, period, down and distance, sets, serve, leaderboard, running order or result anywhere in the banner |
| Logos | A game with logos, one without (small college) | The two logos (the tiles' own); without logos, the generated colour badges; for a person named only in a title, initials |
| Your team | A My Sports team's game | A gold star after the matchup |
| Before the start | Watch a game's channel 25 minutes early (the detail card's Watch) | An amber "in 25 min" chip; at the right "Starts 8:20 PM · on now: <show>" |
| After the end | Keep watching past the game's usual length (or its listing's end) | "Likely over", outlined as on the tiles; "was scheduled until …" at the right. Never Final or a winner |
| Tennis | A tennis match | The players' flags (the country code until they load) and the court in the small line |
| Races and golf | An F1 race, a golf round | The sport's icon in a box where the logos go; the session in the title ("Australian GP · Race"); the circuit and "LIGHTS OUT 2:00 PM" (F1 races and sprints), or the course |
| Channel | A game on a channel with both coasts | The channel's logo, number and name at the right with EAST or WEST; in small type the listing times and "next 11:30 PM <show>" |
| Event feed | A game only on an event feed | The feed's name at the right; "started 1:10 PM" in place of the listing times |
| Guide channel | From the guide, tune a channel that's showing a game on the board | The same game banner. A channel with no game on (or one whose game ended over half an hour ago) keeps the usual banner |
| Controls | OK on a game | The pills replace the key line, exactly as on any channel; the ID line stays |
| Captions | Subtitles on, banner up | Captions sit just above the lower card, not over it; back down when it hides |
| Pointer (LG) | Wave the remote over a game | Edge buttons and pills work as before; a click on the picture opens and closes the controls |
| Hides in | Any banner | "Hides in 5 s" at the right end of the key line, counting down a second at a time (5 s with or without a description); between the seconds the TV draws nothing new |
| By the clock | Watch a game across its scheduled end, bringing the banner up now and then | The chip goes from LIVE to "Likely over" on its own; nothing else in the banner changes while watching |

## Left rail (Build 112)

| Area | How to check | Pass |
|---|---|---|
| Strip | Open the guide, then Live Events | A narrow strip down the left in its own colour with a clear edge: Search, My Lineup, Favorites, Recent, Live Events (All Channels when that setting is on), then My Sports and Settings at the bottom. A bar marks the current section. No tab row and no round buttons |
| No strip elsewhere | Open the player, the mini guide, the event guide, setup and a side panel | No strip; each behaves as before (Search has the strip from Build 113) |
| Hero | Move around, open the column, open the menu | The title, description and preview corner never move (the hero starts right of the strip) |
| More rows | Compare with Build 111 | The timeline and grid start right under the hero: about one more row |
| Filter | Up from the first row, Right | A small filter button beside "Now ▼"; OK opens the filter. A set filter shows a red dot and its chip at the top right of the hero |
| Neighborhoods | Left from the first show at Now | The column opens at the hero's left edge with MY LINEUP · Jump to in the timeline row; the timeline and grid slide right and the window shows 2 hours (re-timed once the slide ends). No channel counts |
| Walk | Up/Down in the column | One neighborhood per press (or swipe); the grid scrolls behind |
| Return rule | Scroll to the middle of a neighborhood on a later show, Left to the column, Up, Down, OK (or Right) | Back exactly where you were: same row, show, time and scroll |
| Another neighborhood | The same, but OK on a different neighborhood | Its first channel, at Now; the column closes and the grid slides back |
| Back in the column | Move to another neighborhood, then Back | Exactly where you were |
| Menu | Left in the column, or Back in the grid with nothing to undo (digits, filter, later time) | A full-height menu with labels over the dimmed hero and grid; the grid slides back under it. Up/Down move; the bar marks the current section |
| Menu picks | OK on My Lineup, Favorites, Recent, Live Events, Search, My Sports, Settings | My Lineup and Favorites land on their column (Favorites only lists neighborhoods with a favorite); Recent goes to its grid, newest first; Live Events opens on SPORTS; the others open their screens as before |
| Back from a picked column | Menu › Favorites, then Back | Back in the menu |
| Back in the menu | On Favorites, Back; Back again on My Lineup | The first goes to My Lineup; then "Press Back again to exit" (Apple TV leaves the app) |
| Recent | Left from the first show on Recent | The menu (Recent has no column); the bar reads "◀ Menu" |
| Settings | Settings in the strip, and from Live Events's strip | Both open All settings; the blue key opens Quick settings for where you are |
| Live Events | Left from a row's first tile; Up/Down; OK | SPORTS opens inside the board ("2 live" in red), the tile rows slide right, the game hero stays. The same sport returns to the same tile; another sport goes to its first game |
| Live Events Back | Back with a filter, then again | The filter clears first, then the menu; Back in the menu goes to My Lineup |
| Yellow | Yellow (Play/Pause on Fire TV) in the guide and in Live Events, also with the column open | Switches between the guide and Live Events, as before |
| Colour keys | Red, Green, Blue and hold OK with the column open and at rest | Same as before; with the column open they act on the channel behind it |
| Key bar | Look at the right end of the key bar in each state | "◀ Neighborhoods · Back: Menu · Hold OK: more", "OK or ▶ Go · Back: Stay · ◀ Menu" in the column, "◀ Menu …" on Recent, "◀ Sports …" on Live Events |
| Pointer (LG) | Hover the strip's icons; click one; click outside the open menu; click a show with the column open; wheel over the column | A name pill and nothing moves; a section opens its column (Recent its grid); the menu closes; the column closes on that show; the wheel walks the list |
| Pointer return rule | Open the column, click your own neighborhood, then another | Your own puts you back where you were; another goes to its top |

## Settings (Build 112)

"Options" is now Settings: a short Quick settings panel for where you are, and All settings, full screen, with seven categories. No screen shows a number of channels.

| Area | How to check | Pass |
|---|---|---|
| Name | The key bars, the player's controls, Live Events | "Settings" on the blue key's hint and on the player's last button; "Options" nowhere |
| Quick settings, guide | Blue in the guide | A side panel over the guide: Preview channels, All Channels tab, Edit My Lineup ›, then All settings › and Help & info ›; Blue or Back closes it |
| Quick settings, player | Blue while watching, or OK › Settings | Subtitles Off / On (says English- or Spanish-language channels), Caption size S / M / L / XL, Caption look; Version East / West on a channel with both, which switches in place; the picture keeps playing |
| Quick settings, Live Events | Blue on the board | Show scores, Games not on my channels, My Sports › (its leagues), Refresh schedule; with Game schedule off the first, second and last are dimmed with "Turn on Game schedule in All settings" |
| All settings | The gear (Up to the tabs, Right to the gear, OK), or All settings › in Quick settings | Full screen: Watching, Guide, My Lineup, Live Events, Providers, Advanced, Help & info, each with a line of its current values; Watching's rows show, quieter, in the middle |
| Moving | Up / Down on the categories | The middle shows each category's rows as you move; the right column says what the category is for |
| Going in | Right or OK, then Back, then Back | Right / OK goes into the rows; Back returns to the categories; Back again closes Settings |
| Rows | Inside a category | Every row has its value (a switch, the choices, or the value before ›); the right column describes the focused row; the key bar says ◀▶ Change on rows of choices and "Changes save as you go" |
| Pointer (LG) | Point at a category, a row, one of a row's choices; click; turn the wheel over each pane | Pointing at a category shows it; a click opens or changes what's under the pointer (a choice picks that one); the wheel scrolls the pane under the pointer |
| Help & info › | Help & info › in Quick settings | All settings opens inside Help & info |
| Dimmed rows | Turn off All Channels tab, then Game schedule | Groups in All Channels stays where it was, dimmed, with "Turn on All Channels tab"; Show scores, Games not on my channels and Refresh schedule dim with "Turn on Game schedule". Rows never come and go |
| Watching | Settings › Watching | English-language and Spanish-language channels (with the language picked in Aa), Caption size (with a preview on the right that follows), Caption look, Letters, Position (dimmed on Apple TV), Up arrow in the player: Channel above / Higher number |
| Guide | Settings › Guide | Preview channels, Clock 12-hour / 24-hour (a real choice), All Channels tab, Groups in All Channels › (checks, no counts), Guide listings › (Fill in missing listings, Listings to use dimmed when it's off, Refresh listings now) |
| My Lineup | Settings › My Lineup | Edit My Lineup › (Added by you, Removed by you, My Lineup: no counts in the headers), Refresh suggestions, Your area ›, Countries & languages ›, Kinds of channels ›, East/West feeds Automatic / East / West / Both (with "Automatic here: West" and why), Channel by channel ›, then Set up My Lineup again › |
| Your area | Your area ›, pick a second area, Back | One page, not the wizard; the guide's local stations change at once; Done or Back closes it |
| Kinds of channels | Kinds of channels ›, turn off News, Back | My Lineup loses its news channels without going through the other steps; no counts beside the kinds; Only channels with a guide is on the same page |
| Countries | Countries & languages ›, check Mexico | Saved at once; no counts; the last one can't be unchecked ("Keep at least one") |
| Live Events | Settings › Live Events | Game schedule, Show scores, Games not on my channels, My leagues ›, My teams › (opens on the teams; Back closes), Remind me before my teams play (dimmed until you have teams), Home city (your areas as choices; dimmed with a reason with one area), Refresh schedule |
| Providers | Settings › Providers | Each provider as "Xtream · Signed in" (never a channel count), Add a provider › (the form; passwords as dots), Add from your phone ›, Reload channels and listings |
| Remove | A provider › Remove this provider | Asks "Remove <name>?" with Cancel focused; Remove takes its channels out and says "Provider removed" |
| Advanced | Settings › Advanced | Only what applies: Play through Marigold (LG), Xtream stream format MPEG-TS / HLS (with an Xtream provider), Experimental: Emby / Jellyfin quick start (with Emby); not listed at all when none apply |
| Help & info | Settings › Help & info | Getting around › shows the keys card again; Send a report to your phone › has the three reports and Clear playback log (asks first); Diagnostics ›; Status › (guide files, memory); About: Version and Memory in use, both focusable |
| Defaults | A fresh install | Preview on, 12-hour clock, All Channels off, subtitles off, Medium / Box, Channel above, Game schedule, scores and games not on my channels on, MPEG-TS, quick start off |
| Old settings | Update over Build 111 | Every setting and My Lineup choice is kept |
| No counts | The guide filter and Jump to, search, the lineup setup and its summary, first-run setup, Guide listings, Status, provider lines | No number of channels anywhere |

## Search (Build 113)

Search is a section of the left rail with its own keyboard, the same on every TV. Results show beside it as you type: the top result, then Channels, Teams, Games and Shows. No header or row shows a number of channels, and nothing in Search shows a score, whatever the Show scores setting.

| Area | How to check | Pass |
|---|---|---|
| Opening | Search in the strip or the menu | The strip stays with its bar on Search; the field ("Channel, show, movie or team"), 🎤 Speak, the letters (a–z, 6×5, Space after z) and Delete · 123 · Clear on the left (Build 115; before, a 6×6 a–z / 1–0 grid); focus on "a"; the clock over the preview corner |
| Zero state, a fan | Follow a team that is live (or starts within the hour) | YOUR TEAMS AND LEAGUES in the band beside the corner (at most two rows: LIVE and the state, or "in 40 min", never a score); then chips from y 330 (Games on now, your leagues by name, News, Movies, Kids, More), Recently watched (two across, with what's on now) and Recent searches |
| Zero state, shared TV | No team followed, or none live within the hour | Recently watched in the band; News, Movies and Kids lead the chips. Someone else's team shows only while live or within the hour |
| New user | A fresh install with a provider | A "Follow your teams" card (OK opens My Sports), the chips, Games on now and Starting soon rows |
| Typing | Type "nb" | Results change with every letter; the top result has its type ("GAME · PBL · YOUR TEAM ★"); channel tiles say what's on now; later items show their time ("Tonight 10:00 PM", "in 20 min") |
| Top result waits | Type a word quickly, stop | The band shows a placeholder until the shows are in (under half a second), then the top result; it never swaps after it shows |
| Corner while typing | Focus on the keyboard with results | The corner shows the top result's card (logo, title, time), no video |
| ▶ to results | ▶ from the grid's right column (or from Speak) | Focus on the top result; the keyboard dims. ◀ from a row's first item returns to the key you left |
| Preview | Rest 2 s on a live channel, game or show on now, after moving once | It plays in the corner, muted, captioned "MUTED <channel> · <what's on>". Nothing plays on the first item focus lands on from the keyboard until you move |
| Moving on | Move to another live item | The corner drops to its card at once and the old stream stops; the next tunes after 2 s (at least 3 s after the last start) |
| OK on a preview | OK on the item playing in the corner | Full screen on the same stream, with sound, no re-tune |
| Back from the player | Back within 2 minutes; then again after watching 3 minutes | First: the same row, the stream still playing muted in the corner. Later: Back behaves as from any channel and Search is gone |
| Guide and Live Events corners | Rest on a channel in the guide, a game on the board | They play with sound, exactly as before |
| Later items | OK on a show later, then OK again, then hold OK | "Reminder set ✓" on the row and the card; the second OK does nothing; hold OK › Cancel reminder |
| Not in your channels | A game marked "Not in your channels" or "Streaming only" | Never the top result; OK opens Where to Watch; no reminder |
| Teams | Type a team's city ("riverton") | TEAMS: the full name, its live game or "Next: Sat 7:30 PM …", the league; the top result is its first game you can watch. A team on a bye says "No game in the next week" |
| Hold OK | Hold OK on a team, a game, a show, a channel and a recent search | Team: Follow / Unfollow, Where to Watch, Remind me. Game: Follow either team, Where to Watch, Remind me. Show: More about this show, Remind me, Channel. Channel: Watch, the other coast, My Lineup, Favorites, Choose Feed. Recent search: Remove this search |
| Nicknames | "niners", "sixers", "man utd", "spurs", "habs" | The team; "spurs" lists both Spurs teams |
| Matching | "law and ordr", "jepordy", "baking show great", "law & order", "office" | Law & Order (with "Showing Law & Order"), Jeopardy!, The Great British Baking Show, Law & Order, The Office |
| Two letters | "nb" | Word starts and initials only (Northbridge Bulls, NBX), never inside a word |
| Cleaner | "watch the bulls game", "news tonight", a show called "Lakers Tonight" | The Bulls; News tonight; the show itself (its title isn't cleaned) |
| Did you mean | "nothbridge" | "Showing Northbridge Bulls"; "Search for “nothbridge” instead" first under the band; OK on it searches exactly |
| Nothing | "lacrosse" (with no lacrosse anywhere) | "Nothing for “lacrosse”" and what to try, then the chips and recent searches. One letter never says nothing |
| Numbers on the keyboard | "4", "206" on the 123 page | The channel band; ▶, OK plays it |
| LG digits, complete | Empty field, 2 0 6 on the remote | The digit box in the corner and Sports Net 2 in the band; it plays when the number is complete |
| LG digits, a name | Empty field, 4 9 | Channel 49 in the band and "OR DID YOU MEAN" the 49ers; nothing tunes by itself; OK watches channel 49, ▼ goes to the team, a grid letter (after moving) carries on "49…" |
| LG digits with text | Type "sn", then 2 on the remote | "sn2" in the field |
| Chips | OK on Games on now, a league, News | Its page in the results ("PBL ×" over the band): the top live game, Live now, Later today, This week (or the guide's News on now as channel tiles). Back or a click on × returns to the zero state |
| Recent searches | Pick "Sports Net 2" after typing "sn2"; pick nine more things; search "lacrosse" | The chip says "Sports Net 2"; four show, eight are kept; nothing is saved for "lacrosse" |
| Clear | Clear in Recent searches | Asks "Clear recent searches?" (Clear · Cancel) first. Red only clears the typed text |
| Off switch | Settings › Guide › Recent searches off | The row is gone and the list forgotten; nothing is kept while off |
| Reopen | Close Search with text in the field, reopen within 5 minutes, then after 6 | The query comes back the first time, not the second |
| Finished games | Show scores on, then off; search a team that just played | On: the game says "Ended", comes last and is never the top result (a replay airing ranks above it). Off: not listed |
| No scores | Search a live game's team with Show scores on | No score in the rows, the top result, the corner card or its caption; titles are the matchup |
| Show scores off | Rest on a live game with Show scores off | The card only: live sports don't auto-preview |
| Preview off | Settings › Guide › Preview channels off | The corner always shows the card |
| Live Events data | Open Search for a few minutes with a live game followed | Its state (Q3, 2nd half) moves on as on the board |
| Pointer (LG) | Point at keys, chips, results and the corner; click; park the pointer while rows change | Each is a click target; a click on a playing corner goes full screen; a parked pointer doesn't start a preview |
| Key bar | Keyboard, results, the digit box | "Clear text · 0–9 Channel · ▶ Choose a result · Back: Close"; "◀ Keyboard · Hold OK: more · Back: Keyboard"; "Plays when the number is complete · Back: Cancel" |
| Rail | ◀ from the grid's left column; pick Live Events, then My Lineup | The menu over Search; each closes Search and opens that section |

## Tennis coverage (Build 115)

A tennis channel's live coverage that names its tournaments ("Center Court Live · Vienna-ATP & Basel-ATP Semifinals") is a way to watch those tournaments' matches while it's on. It never makes a tile of its own.

| Area | How to check | Pass |
|---|---|---|
| Matches | Live Events while Tennis Channel (in My Lineup) shows live coverage naming two tournaments | Each of their matches on or starting during it lists Tennis Channel; matches of other tournaments don't |
| Where to Watch | OK on such a match | Tennis Channel under Your channels with "Tournament coverage"; the match's own feeds under Event feeds |
| Order | Watch the match, then make its feeds fail | The match's own feeds play first; Tennis Channel is tried after them |
| Tile | A match with feeds, and one with only the channel | The first names its feeds; the second names Tennis Channel |
| Unnamed | Coverage that names no tournament ("Live: Center Court Live"), a studio show ("Tennis Channel Live") | Nothing is added to any match |
| Other times | Coverage later in the day, after the matches | Nothing is added to matches that aren't on then |
| Round slots | A provider slot "ATP 500 Tennis: Vienna vs Semifinals" | Joins Vienna's matches instead of a tile of its own |
| Local names | "ATP Tennis: Wien", "ATP 500 VIENNA" | Join Vienna's matches when they are on |

### Search keyboard and scope (Build 115)

The keyboard keeps letters on its first page and numbers behind a 123 key. Once My Lineup is set up, Search looks in your lineup and Live Events first and says so; what's outside is one row away. Never a count, a data source or a score.

| Area | How to check | Pass |
|---|---|---|
| Letters | Open Search | a–z in the 6×5 grid (same columns and key size as before), Space in the four cells after z, ⌫ Delete · 123 · Clear under it with Clear at the far end; no digits; the keyboard ends a row higher than in Build 114 |
| 123 page | OK on 123 | A phone keypad (1 2 3 / 4 5 6 / 7 8 9 / & 0 ') centred on a faint panel with even margins, Space across its row, ⌫ Delete · abc · Clear; every row full, no empty-looking cells, no line of text, no - or . keys; focus stays on abc |
| Mode key | OK on abc; ▼ from u, v, w or x; ▲ from 123 and from abc | Back to the letters with focus on 123; ▼ from the middle columns reaches 123 over Space; ▲ reaches Space, then the key above (0 on the 123 page) |
| Page stays | On 123, type 2 0 6 | The page stays on 123 after each digit; "206" in the field and its channel in the band |
| Back to letters | Clear; Red; ◀ from the zero state's results with an empty field; close Search with text and reopen within 5 minutes | Each comes back on the letters page; the reopened query is in the field, on the letters page |
| LG number keys | On either page: 2 0 6 with text in the field, then from an empty field | Into the field after the text; from an empty field the guide's digit box, as before |
| Pointer (LG) | Point at every key on both pages, then click 123 and abc | Each key is one target (Space too); the click flips the page and the pointer's key stays focused |
| Speak and ▶ | ▲ from the top row of either page; ▶ from its right column | 🎤 Speak; the results, as before |
| Your lineup first | My Lineup set up; type a word that matches a channel in it and one outside it | "YOUR LINEUP AND LIVE EVENTS" over the results: channels in My Lineup and Favorites, shows on them, games you can watch, teams you follow or with a game you can watch this week, leagues and genres |
| More outside | ▼ to the last row | An outlined row: "More outside your lineup", the first few names outside ("Harbor 24 · Harbor Life · Harbor Lights", never a count) and a ›; focused, "OK: Show" |
| Everything | OK on it | "EVERYTHING" over the results: the same list as Build 114, in the same order; outside channels, shows and teams say "Outside your lineup" in their second line ("2401 · Outside your lineup"); games keep "Not in your channels" or "Streaming only"; focus on the first new item outside; the last row says "Only your lineup and Live Events" / "Hide what's outside your lineup" |
| Back to your lineup | OK on that row | Your lineup and Live Events again; focus on "More outside your lineup" |
| Choice holds | Widen, type more letters and ⌫; close Search and open it again | New letters stay widened; reopened, your lineup comes first again |
| Exact names | A channel outside your lineup by its full name; its number on the 123 page | In the results at once, marked "Outside your lineup"; it can be the top result |
| LG number outside | Empty field: the number of a channel outside your lineup; then one in a hidden group | The band says "2061 · Outside your lineup" and it plays when the number is complete; the hidden group's waits for OK ("Doesn't tune by itself") |
| Nothing in your lineup | A word that only matches outside it (a show on a channel you haven't added) | "Nothing for “…” in your lineup", "Here's what's outside it.", then the matches under OUTSIDE YOUR LINEUP (no mark on each), then Try and Recent searches |
| Nothing anywhere | "lacrosse" | As in Build 113 |
| Zero state | Open Search with My Lineup set up | As before, with one quiet last line: "Search looks in your lineup and Live Events first." |
| Before a lineup | A fresh install before My Lineup is set up | Everything is searched; no scope words, no last row, no quiet line |
| All Channels tab | Settings › Guide › All Channels off, then on | The same words either way |
| Speed | Type quickly with a large provider | Each letter as fast as in Build 114 (the timing log's runMax and runAvg about the same) |

## Caption timing (Build 115)

Captions read from the stream now wait for the TV player's own clock (its position, carried on between its reports), so each one shows with the picture it belongs to. Live captions are typed as people speak and often come a few seconds after the words; Caption timing moves them. The playback log (Settings › Help & info) says how they were timed, once a minute while they show, in counts and ms only.

| Area | How to check | Pass |
|---|---|---|
| Pop-on in time | A channel with pop-on captions (a show or an ad, not live news), subtitles on | Each caption appears with the line being spoken, within a frame or two, from the first minute on |
| Roll-up | A live news channel | The words roll up steadily, a few seconds after they're said (the station's typing), and never ahead of the speaker |
| Clock in the log | Watch 2 minutes, then Help & info › the playback log | "caption timing: the player's clock counts from the first picture" (or "from the stream's first timestamp": note which), then once a minute "caption timing: clock currentTime; ahead median … ms (min …, max …); error ±… ms; jumps 0; setting 0.0 s". No caption text, links or names in it |
| Clock fallback | If the log says "clock buffer" or "clock default" | Note the TV model and webOS version: the player didn't say its position |
| Relay reopen | A channel whose source drops and reopens ("the source closed; reopened it" in the log) | Captions keep coming in order after it; "jumps 1" in the next timing line |
| Stall | A channel that stops to buffer mid-show | Captions freeze with the picture and carry on with it; none shows before its picture |
| Picture order | An HEVC (4K or 1080p HEVC) channel with captions | No scrambled or doubled letters |
| Settings row | Settings › Watching › Caption timing | ‹ On time ›; ◀ ▶ step by half a second from "3 s earlier" to "3 s later"; the arrow at an end dims; "For every channel" under it |
| Player row | Aa in the player › TIMING › Caption timing | The same row; "For every channel", or "For this channel" once Only this channel is on |
| Later | Caption timing 1.5 s later | Captions show 1.5 s later than before, every channel |
| Earlier | Caption timing 1 s earlier | Captions show 1 s sooner; at 3 s earlier on a channel with a short buffer they go no earlier than the stream allows (no message) |
| One channel | Aa › Only this channel on, then 2 s later; change channel | That channel keeps 2 s later (next time too); other channels keep every channel's value. Off again: it follows every channel's |
| Kept | Restart the app | Both values kept |
| Pointer | Click ‹ and › on the row | One step each; a click on the value does nothing |

## Picture check (Build 115)

A stream that plays must show a picture. The relay reads every byte it passes the TV (beside the captions) and calls each second of video picture, black or static; the player counts a picture only once the decoder reports its first frame (uMS videoInfo, no longer "playing"), and lists the video tracks (sourceInfo). Radio, a channel kept as sound only, a pause and buffering are never judged. Every step is in the playback log (Settings › Help & info › Send a report to your phone › Playback log); none of it ever names a link.

| Area | How to check | Pass |
|---|---|---|
| Picture facts | Any channel through the app | Log: "video · 1920x1080 59.94p · h264 High 8-bit · aac" (size, rate and scan, codec, profile and bit depth, sound) once after "picture" |
| A codec this TV can't decode | A feed in H.264 4:2:2 10-bit (or High 10) | Log "picture check · unsupported 0 s; h264 High 4:2:2 10-bit" at once, then the next version; next time it's tried last ("this TV can't play it"), as a 601 is |
| Scrambled | An encrypted feed (picture and sound scrambled) | After 3 s "picture check · scrambled 3 s; 100% of packets scrambled", the next version, no Xtream retries |
| No video | A feed with only sound (a radio-style feed under a TV channel) | After 6 s "picture check · no video track (PMT audio 1)", the next version, no retries |
| Tables only | A feed that sends only tables and padding | After 6 s "picture check · tables only 6 s; tables and padding only (null 99%)", the next version |
| Sound, no frame | A feed whose decoder never starts while the sound plays | After 8 s "start failed · the sound plays but no frame was drawn", one retry, then the next version |
| Black at the start | A feed that sends black (padded to a constant rate or not) | After 8 s "picture check · black 8 s; I 432 B, 16 kbps", one reconnect on an Xtream / M3U feed (none on Emby), then the next version. Sources says "black picture" |
| Black mid-stream | A channel that plays, then goes black and stays black | After 15 s one reconnect, then (8 s more) the next version |
| Fades | Programmes and ads with fades to black, night scenes | Never a reconnect or a switch (a fade is at most 3 s; a dark noisy scene is a picture) |
| Static (a slate) | A game whose source shows a "starting soon" slate | After 45 s "picture check · static 45 s; P 318 B, …" and, for a game with a source not tried yet, "No picture on this source — trying 2 of 3", then the "Back on" banner. A channel's slate is only logged |
| No picture anywhere | A channel whose every version has sound and no picture | Its versions are tried in turn (never another channel); then the card "<channel> has no picture right now · None of this channel’s versions shows a picture right now. The sound plays on." with Try another source (opens Sources) and Keep listening |
| Keep listening | Keep listening on that card; tune away and back | The card goes, the sound plays on; for 7 days the channel plays as sound only and isn't checked (log "keep listening") |
| Radio | A radio channel | Plays with sound alone as before; no picture check, no card |
| Budget | A channel with many black versions | At most 6 automatic actions in 3 minutes, one round through the versions, then the usual message |
| Our own display | A stream whose frames are fine while the TV shows nothing (the plane not attached) | Never a reconnect or switch: the relay's "frames are fine" counts as the picture |
| Emby / Jellyfin | An Emby channel (no relay) | Only the player's signals: videoInfo for the picture, sourceInfo's video tracks, sound without a frame |

### Sources that keep closing

The relay reopens a continuous source that closes (up to 6 times without 30 s of flow). It now reports each connection; a source that closes 3 times within 15 s of the picture, every time after under 5 s and never lasting 10 s, is a failed start.

| Area | How to check | Pass |
|---|---|---|
| Each reopen | A source that closes now and then | Log "relay · closed after 2 s, 310 KB; reopened it (3 times)" for each one (no longer only a count) |
| Keeps closing | A PPV event feed that reopens every ~2.4 s after its picture | "source keeps closing · 4 connections in 9 s", no Xtream retries, no reconnects: a game is on its next source within ~8–10 s, a channel on its next version. Sources says "the source keeps closing" |
| The relay gives up | A source whose connections last 5–9 s each until the relay stops | "source keeps closing · …; the relay gave up" and the next feed, not a reconnect of a stream that "played" |
| A real run | A source that played 30 s or more, then closes | Reconnected as before ("played" credit kept) |

## Pointer and keys (Build 116)

A key press puts the Magic Remote's pointer away; only real movement brings it back (more than 12 px in 200 ms, none counted in the first 350 ms after a key). Check on both LG TVs (the C1 and the other one).

| Area | How to check | Pass |
|---|---|---|
| Arrows hide it | Point at the guide, then press an arrow | The arrow goes (or stays up but does nothing), no hover highlight is left, focus moves from where it was by the arrow |
| No stealing | After an arrow, hold the remote as you normally do, hand resting, for 10 s; press arrows a few times | Focus moves only with the arrows; nothing under the arrow is focused, highlighted or opened (the player's controls do not open) |
| Other keys too | OK, Back, a digit, a colour key, CH+/CH- while pointing | Each acts on the focused item as a key; the pointer is away afterwards |
| OK with the arrow still up | On the TV that leaves its arrow up, press arrows, then OK without moving the remote | OK opens the focused item, not the one under the arrow |
| Moving brings it back | After arrows, move the remote across the screen | The arrow follows and hovering focuses again, as before |
| Clicks and the wheel | With the pointer back, click a rail icon, a programme and a setting; turn the wheel | As in Build 115 |
| Cursor sleeps | Point, then leave the remote on the table until the arrow disappears | No highlight left; OK acts on the focused item |
| Log | Start the app | One line "pointer: SDL_webOSCursorVisibility present" (or "absent"); nothing per key or motion |
| Other platforms | Fire TV, Android TV, Apple TV | Unchanged |

## Fixes (Build 116)

The guide in the player lists the list the channel was picked from only while that list has the channel playing; otherwise My Lineup (every channel when the lineup doesn't have it). Search plays its channels with My Lineup as the list CH+ / CH- go through. Picking a neighborhood scrolls it to the top of the grid.

| Area | How to check | Pass |
|---|---|---|
| Player guide after Search | Search a channel, OK on it, Left in the player | The guide in the player shows My Lineup under its neighborhoods, the channel playing focused (WATCHING), never "No channels in this list." |
| After a game | Watch a game from Live Events, Back, Search a channel, OK, Left | The same: My Lineup with the channel focused (not the games' list, not empty) |
| Search preview | In Search, rest on a live game until it plays in the corner, then on a channel until it plays, OK, Left | My Lineup with the channel focused; CH+ / CH- move through My Lineup, never to a game |
| Outside your lineup | Search shows EVERYTHING; OK on a channel marked "Outside your lineup", Left | Every channel's list with that channel focused |
| Back within 2 minutes | From that player, Back (Search with the stream in the corner), OK again, Left | The same stream full screen, no re-tune; the guide as above |
| Other ways in | Open the player from the guide (My Lineup, Favorites, a filter), Recent, a number typed in the player, a reminder card | From the guide: that list with the channel focused; a channel the list doesn't have (a number, a reminder): My Lineup with it focused |
| Pick in the player's guide | Pick another channel there, then CH+ / CH- | They move through the list the guide showed |
| Neighborhood at the top | My Lineup, Left to the column, Down several neighborhoods, OK | The picked neighborhood's header sits at the top of the grid with its first channel focused (not the last neighborhood with one or two rows of the new one at the bottom) |
| Walking the column | Up/Down in the column | Each neighborhood scrolls to the top behind the column |
| Near the end | OK on the last neighborhood (a short one) | The grid scrolls as far as it goes: the last rows fill the bottom, the header as high as it can be, its first channel focused |
| First neighborhood | Back up to the first neighborhood, OK | The list's very top (the setup prompt too, before a lineup is set up) |
| Return rule | Open the column mid-neighborhood, walk away and back, OK | Exactly where you were (row, show, time, scroll) |
| Favorites, All Channels | The same on Favorites and All Channels (setting on) | The same: the picked neighborhood at the top |
| Pointer (LG) | Click a neighborhood in the column; the wheel over the column | Same as OK and Up/Down: the neighborhood at the top |
| Live Events | SPORTS: OK on another sport | Its row at the top of the board, as before |

## Logos (Build 116)

A channel's card shows its logo alone whenever the logo that loads is good, wherever it comes from; the name stays only for poor logos (tiny, blurry when blown up, a blank tile, a thin sliver) and for locals.

| Area | How to check | Pass |
|---|---|---|
| Good logos fill the card | In My Lineup, find BBC News, PokerGO, LiveNOW from FOX, SportsGrid, NBC Sports Bay Area, PBS Kids | Each card shows only the logo, as large as the curated ones (CNN, ESPN); no small logo with the name to its right |
| Provider logos | A channel whose only logo is the provider's (e.g. WE tv, theGrio) | A clear logo fills the card; a tiny or blank one sits small next to the name |
| While loading | Scroll quickly to channels not seen yet | The name shows centred until the logo arrives, then the logo replaces it in place (no jump to a small logo first) |
| No logo | A channel whose logo fails to load (e.g. a 4K show channel) | Its name, readable, as before |
| Locals | Your CBS, ABC, NBC, PBS stations; a local with no call sign (a second PBS station) | Unchanged: logo, call sign and network number; a second PBS station still shows its name next to the PBS logo |
| Mini guide | Open the guide in the player | Same channels show the logo; a poor logo shows the name in its place |
| Search, Live Events, banner | Search for a channel; open a game's where-to-watch; change channel | The logo where it is good, the channel name where it is poor; the same channel reads the same everywhere |
| Restart | Restart the app (logos come from the disk cache) | The same channels fill their cards as before the restart |
| Channel report | Export it | logo_status says "shown" for logos that fill a card and "shown, low quality" for the ones kept next to the name |

## In-player guide (Build 116)

Left in the player opens the guide in its card, as before, without the pills along its top: the top line names the list and the focused row's neighborhood. Left at the first show at Now opens that list's neighborhoods inside the card and Left again the lists: the main guide's path, with its return rule. The picture never changes until OK.

| Area | How to check | Pass |
|---|---|---|
| Open | Left while watching a channel picked in My Lineup | The 1240-wide card, a third of the picture clear at the right with its sound; "MY LINEUP · LOCAL" (the playing channel's neighborhood) and the clock on the top line; 2½ hours; the playing channel focused, marked WATCHING; no pills; the legend starts "◀ Neighborhoods" |
| Top line | Up and Down across neighborhoods | Its neighborhood follows the focused row; no number of channels anywhere |
| Now ▼ | Up from the top row, OK, Down, OK | "Now ▼" in the timeline row opens Now, Tonight (before 7 PM) and Tomorrow; OK goes there and the button reads "Tomorrow ▼"; Back from the times returns to the grid |
| Left, not at the edge | Right once, then Left; ▶ to a later time, then Left on the first show | The earlier show; earlier in time first. The column opens only from the first show at Now |
| Column | Left at the first show at Now | The neighborhoods inside the card on the playing channel's (red dot), "MY LINEUP" and "◀ Lists" in its header, as the main guide's; the grid pushes right and shows 1½ hours; the card and the picture don't move |
| Walking | Down, Down | The grid scrolls behind to each neighborhood's first channel at the top; the top line follows; the details say "OK: Sports, from the top" (on the one you came from, "OK: back where you were"); nothing tunes |
| Return rule | OK on another neighborhood; again OK on your own after walking away; again Back | Its top at Now; exactly where you were (row, show, time); exactly where you were |
| Lists | Left in the column | The lists over the left of the card, the card dimmed, the picture clear: My Lineup (with the bar), Favorites, Recent, Live Events; All Channels only with Settings › Guide › All Channels tab on; no Live Events with Settings › Live Events › Game schedule off |
| Favorites | The lists, Favorites, OK | Favorites' grid and its column: only the neighborhoods that hold a favorite, on the playing channel's; OK there lands on the playing channel |
| Recent | The lists, Recent, OK; then Left at its first show | Recent's grid in watch order, no column; Left opens the lists directly; Back there returns to the grid |
| All Channels | The lists, All Channels (setting on) | Its column, as My Lineup's |
| Live Events | The lists, Live Events | The games rail with SPORTS open in the column's look ("2 live", red dot, "◀ Lists"); Up/Down walk sports and the games follow; OK or Right on a sport lands on its games (on the one it opened on, as they were); Left goes back to the lists; never a score |
| Back order | Open the lists, then Back again and again | The lists, the column, typed digits, a later time, then the guide closes |
| Flip list | The lists, Favorites, OK on another favorite on now; then Up/Down and CH+ / CH- | They go through Favorites |
| Opening again | Left again after that, and after a channel from Search | Favorites with the playing channel focused (WATCHING); after Search, My Lineup |
| Unchanged | A later show: OK, hold OK; type a number; the ▶ arrow | Its details; the REMINDER chip and the notice; the digits box over the clock in the top line, the row jumped to; the window slides |
| Games | Left while watching a game | The games rail, without scores; Left on a game opens SPORTS; Left again the lists; picking My Lineup there opens the guide on its neighborhoods |
| Pointer (LG) | Move the remote | The list name becomes a button, "◀ MY LINEUP · LOCAL"; a click opens the neighborhoods (on Recent, the lists); "Now ▼", shows and the arrows click as before; a click beside the card closes the guide |
| Pointer, column | Hover a neighborhood, click one, turn the wheel, click "◀ Lists", click a show | A soft highlight; the return rule; the wheel walks the column; the lists open; the column closes on that show |
| Pointer, lists | Hover a list, click it; click the dimmed card | A soft highlight; that list opens; the lists close (as Back) |
| Pointer away | With the column open, press an arrow, then rest the hand | No highlight left; OK acts on the focused neighborhood |
| Hints | LG, then a remote without number keys | "123 jumps to a channel" only with number keys; with the column open "OK or ▶ Go · Back: Stay · ◀ Lists" (with the pointer: what clicks and the wheel do) |

## Live Events opening (Build 116)

Opening Live Events with a large provider (tens of thousands of channels, big event groups) no longer stalls while the schedule, scores and the fresh board come in. The matcher's rebuild and the board are made on a worker from a snapshot and put in at once; a burst of answers makes one board; the screen shows each board as it is and keeps the focus on its game. A board kept from before shows only when its games were worked out under 15 minutes ago, the same day, and then made again from the clock; otherwise the board's placeholders show until fresh games are in. Fresh boards have the same tiles, rows and order as Build 115's.

| Area | How to check | Pass |
|---|---|---|
| Old board | Leave Live Events (or the app) for over 15 minutes, or until after midnight; open Live Events | No tiles from before: placeholders in the board's layout (row titles and tiles as flat shapes, no text or numbers) pulse slowly; Left opens SPORTS, Back the menu; the focus never lands on a placeholder |
| Rows come in | Watch the placeholders as the games load | Real rows replace them from the top as soon as the first board is made; placeholder rows stay after them until the load ends, then go (no empty rows) |
| Nothing comes | Open with the network unplugged | The placeholders stop after 20 s and the board says "No games this week in your sports." (no endless pulse) |
| New board | Close Live Events and open it again within a few minutes | The last board shows at once; within a minute of being made it shows as is, else placeholders for a moment while it is made again from the clock |
| Clock | Open on a board made 10 minutes ago, with a game that ended meanwhile and one that started | The ended game is gone (or Likely over for its usual overrun); the started one is LIVE; no LIVE the clock contradicts; "in N m" counts from now |
| Keys | Left/Right/Up/Down while the schedule, scores and fresh board come in | They answer at once; no freeze when a new board appears |
| Same board | Compare with Build 115 on the same provider and moment | Same rows, groups, tiles and order; same tile text, pills and footers |
| Focus | Rest on a game in a sport's row while a new board comes in (a Live now row appears above, or a game is added before it) | The focus stays on the same game in the same row; the board doesn't jump |
| Game gone | The focused game leaves its row (it ends) | The focus stays in that row at the same place |
| SPORTS column | Open SPORTS, wait for a new board, Back | Back on the same game |
| Scores | Watch the board while scores come in for several leagues | Each round shows within a couple of seconds; moving around stays smooth while it does |
| Your changes | Filter › Games not on my channels; Settings › Show scores; My Sports | The board changes right away |
| Long rows | Up/Down into Time not listed or More events (thousands of tiles) | The focus moves at once |
| Timing log | Keep Live Events open a few minutes, then send a log | At most one "events timing …" line a minute (boards, perMin, boardMax, tiles, rebuilds, rebuildMax, uiMax) and one "events board frames=… frameMax=… refreshes=… refreshMax=…" line; counts and milliseconds only, no titles or links |
| Account change | Add or remove a provider while Live Events is loading | The board follows the new list (never stuck on the old one) |

## Logos and focus (Build 117)

Every logo sits straight on its cell: no plates, chips or boxes behind it, anywhere. Each logo is prepared once after it loads, on a worker, in one version made for the app's dark surfaces: colours that would fade into the cells get just enough lightness to stand out, with their hue kept (navy turns a clear mid blue, black off-white, purple lilac); colours that already show are left alone. A logo that comes in its own plain white or black box loses the box. Where logos appear, focus no longer turns a cell white: the cell lifts a little, takes a slightly lighter fill and a 3 px white outline, and its text stays light. So a logo never changes when the focus or the pointer moves. Buttons, menus, pills, settings rows and the guide's show cells keep the white focus.

| Area | How to check | Pass |
|---|---|---|
| Dark logos | My Lineup with a black, navy or purple logo (e.g. a news or kids' channel), not focused | The logo reads clearly on the dark cell, in a lighter shade of its own colour; white, red, yellow, green and grey logos look as before |
| Guide focus | Up/Down through the guide | The focused row's channel cell lifts a little with a white outline and a dark fill; its logo, name and number look exactly as unfocused; the focused show cell is white as before |
| Pointer sweep (LG) | Sweep the pointer slowly down the guide's channel column and back | Only the outline moves; no logo flickers, flips to black or gets a box as the pointer passes |
| In-player guide | Left in the player, Up/Down | The focused channel cell keeps its fill and gets the white outline; no grey fill; every logo (black and navy included) readable |
| Search | Search a channel, a show, a team and a game; move through the results | Focused results lift with the outline and light text; the focused result's logo is the same as in the corner card; "OK: Watch" readable |
| Live Events | Move across the tiles | The focused tile lifts with the outline and stays dark; badges and text keep their look; LIVE stays red |
| Games rail, My Sports | The games rail in the player; My Sports › teams | Same calm focus; a picked team's green ring shows inside the white outline |
| Own boxes | A channel whose logo is a word on a plain white or black rectangle | The rectangle is gone: the word sits on the cell like the others, insides of letters see-through, edges smooth; a logo whose box is coloured (a blue square with a letter) keeps it |
| Team badges | Live Events tiles with team logos and with colour discs | Dark crests in a lighter shade; no ring or plate behind a logo; a dark disc shows a light edge ring; disc letters black or white, whichever is clearer (black on orange) |
| Flags | Tennis tiles, golf leaders, the game banner of a tennis match | Flags in their own shape (wider than tall), never squeezed square; a dark or red flag on a tile has a faint light edge |
| Dimmed | A not-watchable tile; the games rail while its column is open; a finished game in the rail | Its logos and badges go grey with the text but stay as bright; focused, the colour comes back |
| Thin logos | A channel with a thin outline logo, in the guide's small slot beside the name and in Search | Lines unbroken and even, no dark rim round the edges, no shimmer while scrolling |
| Banners | Change channel; watch a game | The channel logo sits on the banner card with no plate behind it |
| Night viewing | In a dark room, the guide and Live Events | Nothing glows: no white boxes, no white focus over logos; the outline is easy to follow from the sofa |
| Restart | Restart the app | The same logos, prepared again from the disk cache, look the same |
| Channel report | Export it | logo_status adds what was done: "shown, colours tuned", "shown, box removed", "shown, box removed, colours tuned" ("shown" when nothing was needed) |

## Guide focus spacing (Build 118)

| Area | Steps | Expected |
|---|---|---|
| First show | Focus the first show in a row (at Now), then Left and Right along the row | The show's white ring stays clear of the channel cell's outline; the channel logo is never covered |
| Later shows | Focus a show further right | It still grows a little on both sides, as before |

## Live Events freshness (Build 119)

Opening Live Events, the first start of the day, or waking the TV with Live Events on screen never shows games from old information. A board made now can still be made from old inputs: the channel list saved on the TV (its event slots as they were), yesterday's schedule, or scores answered hours ago. Until fresh ones are in, the board leaves out what such inputs decide and shows its placeholders while that is everything; real rows come in only from fresh inputs. New enough: the channel list for 15 minutes after it was fetched this session; the schedule while fetched the same day under 20 hours ago; a live score while its league is polled (2 minutes after its answer, or until its next look plus 2 minutes). After 18 s at most (before the placeholders' 20 s run out) every tile shows, each LIVE only from a score answered lately or from the clock, and the board is replaced as fresh data lands. Nothing else looks different, and no more requests are made than before (the daily caps and the refusals' day off still apply).

| Area | How to check | Pass |
|---|---|---|
| First open of the day | Start the app in the morning (Live Events last used the evening before) and open Live Events at once | Placeholders until the board comes; never a game from last night with LIVE, a score or "Likely over"; the first tiles that show are today's |
| Back after hours | Use Live Events in the evening, leave it (the guide, another app) for a few hours, open it again | Placeholders, then today's board; no LIVE on a game that ended meanwhile, not even for a moment |
| TV off on Live Events | Leave Live Events on screen, turn the TV off for over 15 minutes, turn it on | Placeholders straight away (no old tiles), then a fresh board; the focus comes back to the same game when it is still there |
| Short sleep | The same, off for 3 to 10 minutes | The board comes back made again from the clock (a moment of placeholders at most); games that ended meanwhile are gone or Likely over |
| Fills in | Watch the placeholders after a first start of the day | Rows come in from the top as their information arrives (games on your channels before event slots on a slow list); placeholder rows after them until the rest is in |
| Slow network | Open Live Events while the channel list is still downloading (e.g. right after starting the app on a slow connection) | Placeholders for up to about 18 s, then every tile (no LIVE from an old score; slots with no time say "Time not listed"); the board is replaced when the list is in |
| Offline | Open with the network unplugged | The board from what the TV has, made from the clock, within a few seconds (nothing to wait for); no LIVE from a score kept from before |
| Reopen at once | Close Live Events and open it again within a minute | The same board at once, no placeholders |
| Scores on | Open during live games | Live games show once their scores answer (a second or two); LIVE, scores and clocks as before; no flicker while watching the board |
| Player and back | Watch a game full screen for 10 minutes, Back to Live Events | Until scores answer again (within a minute, as before) a game is LIVE only if the clock says so; no old score or clock on a tile |
| Focus | Rest on a game while rows fill in after a wake | The focus stays on that game; the board doesn't jump |
| Timing log | Open Live Events after a few hours away, then send a log | One "events timing fresh=… capped=… held=… oldLive=…" line per open or wake (milliseconds to the first fresh board, whether the 18 s ran out, games held back, old live scores set aside); the "opened"/"woke after" lines in the playback log; counts and milliseconds only, no titles or links |


## Guide descriptions (Build 119)

Shows have their descriptions all day, not just for the first hours after the guide was read. Descriptions are now kept for programmes starting up to the time of the next guide read plus a margin (26 h for the free listings, which are read about every 20 h; 18 h for the provider's own guide, read every 12 h); sports keep theirs for the whole window as before. When the listings files haven't changed, the saved listings are reused as they were, and the files already on the TV are read again (nothing downloaded) shortly before their descriptions would run out. Where two guides list the same show at the same time, the one shown takes a missing description and episode title from the other (never replacing its own). A programme with several descriptions keeps the first one, or the first in English, instead of the last. A merged channel picks, between versions with about the same listings, the one with more descriptions. Each description is held once in memory however many channels and reruns share it, so a whole day of descriptions costs about the same as the few hours kept before.

| Area | How to check | Pass |
|---|---|---|
| Later today | Guide, mid-afternoon or evening, on a news channel, an entertainment channel and a movie channel | The show on now and the next few have descriptions in the info panel, as they do right after a guide update |
| Next morning | Leave the TV on (or come back) the next morning without a guide update in between; open the guide | Shows on now still have descriptions; none missing across a whole row of channels |
| Unchanged files | Help › playback log after a guide check that found nothing new, about a day after the last full update | "saved listings reused" earlier; near the end of a day, a line that the saved files are read again; no new download for it; descriptions continue past it |
| Two guides | A channel whose show comes from one guide while the other guide also lists it | The show has a description and episode title; a show the first guide describes keeps its own wording |
| Language | A channel whose guide gives descriptions in two languages (e.g. English and Spanish) | The English one shows, every time |
| Merged channel | A row made of several versions of one channel (HD, backup feeds) | The row shows descriptions when any version with the same listings has them |
| Sports | Live Events with a game on a local station | Still found by its venue, as before |
| Memory | Settings › Help & info › About › Memory in use, a few hours after launch with a big lineup | Within a few MB of Build 118's figure; peaks no higher |
| First launch | Install over Build 118 and open the guide | The guide shows the saved listings at once, then rebuilds them once in the background (no extra screens) |
| Channel report | Export it | now_start, outside_built_at and provider_built_at (times) and outside_described_next_3h / provider_guide_described_next_3h (counts) show for each channel; descriptions present for shows starting well after the build time |


## Player banner description (Build 119)

A show's banner carries what the guide says about it, at today's height: the episode follows the times on the times line ("8:30 – 9:30 PM · S5 E2 · Pies", quieter, cut at 360 px so the progress bar and "N min left" keep their place), and the description takes the empty band under that line (Regular 20 px, white 70%, two lines at most with an ellipsis, as wide as the title: it stops 36 px short of "Later on this channel" when that shows). The card's top and bottom, the title, the Later strip, the key line, the controls and the captions don't move. With a description or without, the banner stays up 5 s. A game's banner is unchanged.

| Area | How to check | Pass |
|---|---|---|
| Long description | Tune a channel whose show has an episode and a long description in the guide | Times, then "· S5 E2 · Pies" in quieter type, then the progress bar and "N min left"; two lines of description under them ending in "…", clear of the key line's rule |
| No description | A show with neither (a news hour) | Exactly Build 118's banner; "Hides in 5 s" |
| Episode only | A show with an episode and no description | The episode after the times; nothing under them; 5 s |
| Short description | A sports listing with a one-line description | One line under the times |
| Long title and episode | A long title, a long episode name, Later strip showing | The title and the description stop short of the strip; the episode ends in "…" and the progress bar and time left are where they always are |
| Hide time | Bring the banner up with Info (or tune) on a show with a description | "Hides in 5 s" counting down, then it hides after 5 s, as on a show without one |
| Controls | OK on a show with a description | The pills replace the key line as before; the description stays above them |
| Captions | Subtitles on, banner up | Captions sit above the card as before, not over the description |
| Game | Watch a game from Live Events | The game banner exactly as before (no description, 5 s) |
| Flipping | Up/Down quickly through channels | Each channel's episode and description show as it lands; no text from the channel before |

## Live Events rows (Build 119)

More events, Time not listed and Event channels are hidden on the Live Events board and its SPORTS column until switched on in Settings › Live Events › Show on the board (with "Games not on my channels", moved there). Installs from before get them hidden too. Only the board screen leaves them out: Search, Where to Watch and Up/Down while watching still find those games, and Search now finds event channels by name. Event channel tiles no longer say how many feeds they have.

| Area | How to check | Pass |
|---|---|---|
| Default | Update over Build 118 and open Live Events | The board ends with the last sport's row; no More events, Time not listed or Event channels, in the rows or in SPORTS |
| Settings | Settings › Live Events | Game schedule, Show scores, then SHOW ON THE BOARD: Games not on my channels (On), More events, Time not listed, Event channels (Off); then My leagues and the rest; each says in the right column what it is (no data-source names) |
| Switches | Turn each one on, open the board | Its row comes back at the end, in today's order, and in SPORTS; off again, it goes |
| Game schedule off | Turn Game schedule off | Event channels shows On, dimmed, "Always shown while Game schedule is off", and its row is on the board; More events and Time not listed still switch |
| Quick settings | Blue on the board | Four rows: Show scores, Show on the board ("Hidden: More events, Time not listed, Event channels"), My Sports, Refresh schedule; OK on Show on the board opens the four switches; switching one changes the board behind; Back returns to Quick settings |
| Empty board | Open the board when only hidden rows have games (out of season, or My Sports set to a league with no games this week) | "No games this week in your sports.", then "Time not listed and Event channels are hidden." (what is hidden and has something in it) and a focused "Show them in Settings" button; OK opens Settings › Live Events on Games not on my channels (the group's first row). With nothing in the hidden rows either, the first line alone |
| Filter | Filter › Live now with More events hidden | A live game from More events doesn't come back |
| Search | Search a game that is only in More events, and an untimed event | Found as before; OK plays it or opens its card |
| Search event channels | Search an event channel's name | It is among the channels with "Event channel" under it ("Event channel · Outside your lineup" unless one of its channels is in My Lineup); OK plays its first channel, Up/Down goes through the rest; no corner preview |
| Games rail | Play a game from More events (from Search), then Left | The rail opens on its row (More events shows in its SPORTS while you watch it); a game from a sport row: the rail's SPORTS has no hidden rows |
| Up/Down | Up/Down while watching a game from More events; while on an event channel | Live games of the same sport; the event channel's own group, as before |
| Tiles | Turn Event channels on | Its tiles say the group's name and "Up/Down to surf", never how many feeds; the row header shows no number |
| Typing speed | Type quickly in Search on a large provider | As fast as Build 118 |

## Responsiveness (Build 120)

Nothing on screen changes; the TV stops freezing for moments it used to. The channel rows' names, logos, sections and order are worked out once per channel list and lineup, on a worker, with the lineup; a tune, a favourite, a hidden group or a lineup edit only copies them and sets the stars, numbers and lists, on a worker too (before, every one of these worked out all of a big list's rows on the UI thread: about a quarter to half a second on the LG C1). The M3U stream links (tens of thousands for a big playlist) are read on a worker after the first frame and merged on the sort's worker after each refresh; the same links aren't written again. Favourites, recents and the playback memory are written on a worker (a burst once), so a tune no longer waits for flash. The provider's guide and the outside listings refresh by their own age (12 h and 20 h) on a TV left on, and on waking from standby, even when the channel list doesn't change.

| Area | How to check | Pass |
|---|---|---|
| Same rows | Compare the guide, My Lineup, All Channels and a provider group with Build 119 on the same provider | Same rows, order, numbers, names, second lines, logos and fallbacks, stars, East/West pairs |
| Tune | Pick channels from the guide, go full screen, Back, several times with a big playlist | No pause on the way in or out; Recent has them, newest first |
| Favourite | Toggle a favourite in the guide and in the player | The star changes at once, no freeze; Favourites lists it; still there after a restart |
| Lineup edits | Add and remove channels, switch East/West, change countries | The rows change as in Build 119, no freeze |
| Hidden group | Hide a provider group in All Channels | Its channels leave All Channels as before, no freeze |
| Launch | Restart the app with a big playlist | The first frame and the guide come up at least as fast as Build 119; a tune straight away plays (it reads the links then) |
| List refresh | Leave the guide on through a list refresh (30 min with Live Events in use, else 2 h) | No pause when it comes in; channels still play afterwards |
| Provider removed | Remove an M3U provider | Its channels go; the others still play |
| Saved lists | Toggle a favourite, tune, then pull the power after a second | Both are there after the restart |
| Guides on time | Leave the TV on the guide (or asleep) for over 12 h with the same channel list | The provider's guide refreshes ("Updating the provider's guide…" briefly), the outside listings after 20 h; listings never run out; a failed refresh is tried again after 2 h, not sooner |
| Timing log | Use the app a few minutes, then send a log | "store timing opened=…ms", once "store timing launch ready=… firstRows=… fullList=… rows=…", and at most one "store timing publishes full=… patched=… onWorker=… rowsMade=…" line a minute; counts and milliseconds only, no names or links |

### Redraws, log and timing

Screens draw only when something on them changes. While a screen only waits (the guide's corner preview resting after a focus move, a typed channel number's pause, the player's controls left alone, a notice, a toast or a reminder card's few seconds) the app sleeps until that moment and draws once; spinners ("Updating your channel list…", a channel opening, "Reconnecting…", signing in, building a report, looking up teams) draw at each of their eight steps a second instead of every frame. A channel opening is still looked at every frame, so its picture shows as soon as before. Slides, fades, the launch flower and the lineup progress bar move as before. The playback log is now kept as a journal: each save (at most every 10 s, on a worker) adds only the new lines; the file is written whole only when it reaches 8,000 lines or the log is cleared; the five-minute memory reading alone writes nothing (it goes with the next line, or when the app is left). The first start after the update reads the old log once and keeps it. Once a minute while something happens, the app log has a "loop" line: frames drawn, the slowest step and how many took over 50 ms (split into events, store, screen and draw), the screen on top, and the wakeups (and those with work) while hidden; counts and ms only.

| Area | How to check | Pass |
|---|---|---|
| Guide preview | Move the focus in the guide and wait for the corner preview | The preview starts after the same rest as before; the corner's spinner turns smoothly until the picture; nothing else on screen changes |
| Header spinner | Start the app on a saved channel list (or change a provider) and watch the guide's header | "Updating your channel list…" with its spinner turning as before; "Channel list updated" then fades as before |
| Numbers | Type a channel number in the guide, the player and the guide in the player | The digits show, then the channel tunes 1.5 s after the last one (or at once when no longer number starts with them); "No channel N" as before |
| Controls | OK while watching, then leave the remote | The controls hide after 12 s as before; with the pointer on them they stay |
| Opening | Tune a slow channel full screen and in the corner | The spinner turns steadily (centre, banner and corner); "Still trying…" after 7 s; the picture appears as quickly as before |
| Recovery | A channel that buffers or reconnects | "Reconnecting…" with its spinner while it lasts; "Back on …" for 2 s after a backup takes over; the banner hides as before |
| Notices | Green (CC), Pause, Back on a clear picture, a reminder coming due, a toast | Each shows and goes at the same moment as before |
| Panels | Sources while a channel opens; Settings › Providers while signing in; Help › About | "Opening…" turns into "Playing now"; signing-in and memory rows update as before |
| Setup | Phone sign-in, My Lineup setup, My Sports teams, Export | Spinners turn; the pause after "Your channel list is in" is the same |
| Playback log | Watch for an hour, export the playback log; restart the app and export again | The same lines (columns, order, memory lines every 5 minutes) both times; the file on the TV (playback-log.jsonl) grows by the new lines only |
| Update | Update over Build 119 and export the playback log at once | Everything logged before the update is there |
| Idle | Leave the guide idle for an hour | The playback log file isn't written (the memory lines show in an export all the same) |
| Loop line | Use the guide and the player for a few minutes, then send a log | One "loop" line a minute while you use it ("guide: N frames in M steps; slowest … ms (events …, store …, screen …, draw …); K over 50 ms …; hidden: …"); none while idle; no titles, names or links |

## Build 123 — Live Events

Opening Live Events on an old board (or before there is one) no longer shows part of the board and then moves it around: the placeholders stay, with no tiles and no hero game, until the whole board is in, and then it appears in one go. They stay 20 s at most; then the board shows whatever it has. The whole board also comes sooner: a game under way waits for its score only while one can still come (its league has a scoreboard that is answering and hasn't just answered without it); a game whose league has no scoreboard, whose source is refusing us for the day, whose last request failed, or that its league's answer left out goes by the clock at once, and the wait ends as soon as no game left out can still get a score (18 s at most, as before). A board already on screen never goes back to placeholders: scores and the clock change it in place, and a board kept from before (under 15 minutes old, the same day) still shows at once. Within 30 minutes of a game's start, OK plays it straight away when its best source is a channel (My Lineup or the guide, e.g. ESPN or a local station, which usually show the pre-game); a game only on provider event feeds or a team channel's guess still opens its card, as before.

| Area | How to check | Pass |
|---|---|---|
| Old board | Leave Live Events for over 15 minutes during live games (or until the next day); open it | Placeholders only (no tiles, no hero game, nothing pulsing between rows), then the whole board at once; no tile moves, appears or loses its LIVE pill in the seconds after |
| How long | The same, a few times, with the playback log on | The board usually in a few seconds (not ~20 s); the log's "board: … more coming, placeholders shown" line, if any, followed by the whole board's line ("board: … live now") |
| First start | Start the app and open Live Events at once | Placeholders until the board is whole, 20 s at most, then the board as it is (placeholder rows after it while the rest still comes) |
| Nothing comes | Open with the network unplugged | The board from what the TV has within a few seconds, or "No games this week in your sports." after 20 s; never endless placeholders |
| Refreshing | Stay on the board through several score rounds and a clock tick (30 s) | The rows stay; tiles change in place; no placeholders in place of the rows |
| Reopen at once | Close Live Events and open it again within a minute | The same board at once, no placeholders |
| Wake | Turn the TV off on Live Events for over 15 minutes, turn it on | Placeholders straight away, then the whole board in one go |
| Starting soon | OK on a game starting within 30 minutes on ESPN, a local station or another channel in your lineup or guide | It plays full screen at once (the pre-game) |
| Event feed only | OK on a game starting within 30 minutes only on a provider event feed | Its card opens (Remind Me, Where to Watch), as before |
| Later | OK on a game starting in over 30 minutes | Its card opens, as before; at 30 minutes before, OK plays it |
| Search | Search a team or game starting within 30 minutes on a channel | "OK: Watch" on it; OK plays it; a game only on event feeds still says "OK: Remind me" |
| Hold OK | Hold OK on a game starting soon | The Game menu (Where to Watch, Remind Me) as before |

## Build 123 — Channels

The same rule on every device decides what a TV keeps when the providers list more channels than it can hold: what is least useful goes first. With free channels off in My Lineup, free streaming and 24/7 groups (Roku, Pluto, Tubi, Prime, "24/7 …") now wait until after other countries' sports and event groups, so Live Events keeps those games; with free channels on they keep their place. Among other countries' sports groups, the ones made mostly of empty slots ("No Event Streaming", ended games) go first. A group is never split. A provider's single-show channels ("Blue Bloods", "Cheers", "Law & Order" in a TV shows group) are 24/7 channels now and follow the Free setting; real networks in such a group (Comedy Central, Bounce) stay channels. Logos: TMC Xtra, Starz Kids, All Reality WE tv, YES Network Latin, MeTV Plus and other names the logo library spells otherwise find its logo; a station whose name has no network (KFSF) shows its network's logo and line from the station data.

| Area | How to check | Pass |
|---|---|---|
| Live Events (LG) | Free channels off, both providers signed in; open Live Events next to the Apple TV's | About as many games and scores as the Apple TV (Swedish hockey, Spanish and Argentine football, F1 cameras, boxing); a few empty-slot groups may still be missing |
| Log | Send a log after a list refresh | "admitted N channels in G groups; X groups cut for room, Y other countries' left out" with far fewer groups cut than Build 122 (about 105 instead of 204 on the LG); no names or links |
| Free on | Turn free channels on in My Lineup, wait for the list to sort | The free streaming and 24/7 groups come back (All Channels), and some sports groups wait instead; turning it off again swaps them back |
| Apple TV | Same provider alone | Nothing cut, as before |
| Single-show channels | Free channels off: My Lineup on the Apple TV | Blue Bloods, Cheers, Law & Order, I Love Lucy, Bonanza, Criminal Minds, CSI: NY, Happy Days, Three's Company, Tosh.0, Bar Rescue, Ink Master, A Different World, Boy Meets World, Charmed, Jurassic World: Camp Cretaceous, The Andy Griffith Show are gone; nothing else changes |
| With free on | Turn free channels on | They come back (with a guide), filed under Free |
| Networks stay | All Channels › the TV shows group | Any real network in it (Comedy Central, Paramount Network, Bounce) is still a channel in its section |
| Logos | My Lineup: TMC Xtra, Starz Kids West, All Reality WE tv, YES Network Latin, MeTV Plus (WZME), TheGrio, Jewelry Television, Victory, CA Fox Sports Racing, Euronews | Each shows its own logo (library or public database) instead of the provider's; channel report logo_sources says "logo set" or "public database" |
| Station logos | KFSF, KPYX, KTVU Plus (KICU) | KFSF: the UniMás logo, call sign and "UniMás 66"; KPYX: call sign and "Ind. 44"; KICU: its network's logo and line |
| E! | E! and E! West | Unchanged: E!'s logo is tall, so it sits small next to the name |

## Build 123 — Cricket

Live Events reads cricket better. A senior national side never matches its A, U19, Women's, XI, Emerging or reserve side (nor the other way round), so a provider slot for "India vs New Zealand" two days ahead is its own tile at its own time, not the A-team match being played now. A feed is joined to a match over several days only when its time falls on one of the match's days of play (from 3 h before a day's start to 8 h after it), and to a one-day match only up to its end. Provider titles in the "live: A vs B _ Cricket (A vs B)" shape (and the same shape for other sports: field hockey, water polo, wrestling) show as "A vs B"; a "_ Women`s Cricket" tail keeps the WOMEN'S tag. A cricket channel (Willow, Willow Xtra, Sky Sports Cricket and the like) whose guide says "Live: T20 Cricket" (or Test, ODI) joins the one scheduled match in play then, or the one its description's match number or format ("3rd T20I.", "1st Test") names; with several in play and nothing to tell them apart it joins none. It is then an ordinary channel source: first in Where to Watch and first to play when it is in My Lineup, ahead of the provider's event feeds. The app log's matcher line counts these ("N on cricket channels").

| Area | How to check | Pass |
|---|---|---|
| A sides | With an A-team (or U19 / Women's) match and a provider slot for the senior sides on another day, open Live Events and export the Live Events report | The slot is its own tile ("India vs New Zealand") at its own time, not "Live now" under the A-team match; the A-team tile has no provider feed from it |
| Titles | Find the provider's cricket slots ("live: Zimbabwe vs Afghanistan _ Cricket …") and a field hockey one in the same group | Tiles, Where to Watch and the details card read "Zimbabwe vs Afghanistan"; no "live:", no "_ Cricket", no repeated short names; women's cricket tiles show the WOMEN'S tag |
| Willow | During a match on Willow (guide: "Live: T20 Cricket", "3rd T20I.") with Willow in My Lineup, open the match's tile | Where to Watch lists Willow first under Your channels, the provider's feeds under Event feeds; OK on the tile plays Willow |
| Two matches | While two T20s are on and Willow's description names neither | Willow is on neither tile (never a guess) |
| Still slate | Pick the provider's feed for a cricket match in Where to Watch when it shows a still slate (LG) | After 45 s "picture check · static 45 s; …" and "No picture on this source — trying 2 of 2", then Willow plays |
| Matcher log | Send a log after Live Events builds | The matcher line has "… by description, N on cricket channels)"; no titles or links |

## Build 124 — Live Events look

Each Live Events tile now starts with its sport's icon, white, in a small rounded chip of the sport's own colour (green soccer, orange basketball, ice-blue hockey, navy baseball and softball, olive darts…; none is red). The chip never changes when the game goes live: live is shown only by red status text, the game's clock or period ("2nd · 8:41", "Q3 · 4:12", "Day 2 · 31 ov") with no "LIVE" word, or a red "LIVE" when the game has no clock yet. An old score's live (by the clock) keeps its dimmer red. The look-alike icons are redrawn: baseball and softball a bat and ball, tennis a racket and ball, rugby goal posts and a ball, ice hockey a stick and puck, horse racing a horse's head, cricket stumps and a ball, wrestling a ring's ropes, athletics a runner, and "Other" a whistle. A TV show in "Time not listed" with no sport gets no chip (its line starts with its time); a game of a sport the app doesn't know keeps the dashed ring. The hero, a game's card and the games list in the player's guide show live the same way (red status, no LIVE pill). While the board loads, the hero shows a spinner and one line that says what it is waiting for ("Getting your channels…", "Getting this week's games…", "Matching games to your channels…", "Getting live scores…"); after 10 s a second line, "Almost there. The first load of the day takes longest." The tile placeholders below are as before.

| Area | How to check | Pass |
|---|---|---|
| Chips | Open Live Events with soccer, NBA, NHL, MLB and cricket on | Each tile's top line starts with a coloured chip (28 px, rounded) with the white icon; the status starts right after it and the competition stays at the right, as before |
| Live | A row with live and later games | Live tiles: the chip the same colour as on later ones; the clock in red ("2nd · 8:41"), no "LIVE" word, no red dot, no red icon; a live game with no clock: red "LIVE" |
| Old live | A game live only by the clock (scores off or old) | Its status in the dimmer red, as in Build 123 |
| Icons | Look at tiles of each sport from the couch | Baseball, tennis, rugby, ice hockey, field hockey, football, cricket, horse racing each tell apart at a glance |
| Dimmed | Games not on my channels on (Filter › Also); a finished game | The not-watchable tile's chip fades with its text; nothing else on it changes |
| Focus | Move through a row | The chip looks the same focused or not |
| Time not listed | Settings › Live Events › Show on the board › Time not listed; look at a TV show there | No chip, no whistle: its line starts with the state; a provider's game slot of an unknown sport still has the dashed ring |
| Hero | Focus a live game, then a later one | Live: the status in red, no LIVE pill; later: as before |
| Card and player guide | Hold OK › a live game's card; in the player, Left to the games list | The status in red, no LIVE pill; other states keep their pills |
| Banner | Watch a game | The player's game banner as in Build 112 (its own LIVE chip); its sport icon is the redrawn one |
| Loading | Leave Live Events for over 15 minutes (or start the app) and open it | Under "Live Events" a turning spinner and one line for the step it's on ("Getting your channels…", then "Getting this week's games…", "Matching games to your channels…" or "Getting live scores…"), changing as it goes; no grey bars there |
| Slow load | The same on a slow network (or the first open of the day) | After 10 s "Almost there. The first load of the day takes longest." under it; at most 20 s, then the board |
| Redraws | Watch the loading screen, then the board, and send a log | The spinner turns smoothly; once the board is in, nothing redraws while idle (the "loop" line as in Build 120) |

## Build 125 — Live Events from the schedule

Live Events tiles now come from the game schedule: each scheduled game is one tile, and every provider event feed that names it (its teams or players, at about its time, in its sport) is one of its sources, so one game never shows as several tiles. An event feed that matches no scheduled game is no longer a tile of its own. A new switch in Settings › Live Events › Show on the board, "Games without a schedule" (off), puts such events with a start time and a clear title in a row of their own, "Games without a schedule", at the end of the board (never in a sport's row, Live now or the hero), still without shows, replays, past dates and leftovers. It works on its own; More events is only sports you don't follow. A slot with a clock time but no date that no schedule has, and that was already there on an earlier day, is a leftover and stays out (the app remembers such slot titles for 10 days, with the saved board); one that names a scheduled game takes that game's day and time, even under another sport's name. High-school games and pro wrestling, which no schedule has, still show when picked in My Sports; Time not listed and Event channels are as before. While the schedule is still coming the board waits with its placeholders and loading line; only with Game schedule off, or when no schedule could be had at all (offline on a first launch), is the board the provider's events as before. The Live Events report has a last column, "schedule": "on the schedule", "no schedule" or "leftover".

| Area | How to check | Pass |
|---|---|---|
| One tile per game | A soccer or NBA game several provider groups carry (e.g. a Bundesliga or LaLiga game on DAZN, Soccer PPV and TSN+ slots) | One tile; Where to Watch lists every group's feed under Event feeds; no second tile for it in its row, Live now or More events |
| Default | Update over Build 124, open Live Events | Sport rows and Live now hold scheduled games only: no old boxing slot ("… UK 2:00pm") every day, no last month's European cup slots, no replays shown as upcoming; Time not listed and Event channels hidden as before |
| Settings | Settings › Live Events › Show on the board | Games not on my channels, More events, Games without a schedule (Off), Time not listed, Event channels; the right column says what it does, no data-source names, no counts |
| Switch on | Turn Games without a schedule on (More events can stay off) | A "Games without a schedule" row at the end of the board with the events no schedule has (with a time and a clear title); none of them in a sport's row or Live now; off again, the row goes |
| Game schedule off | Turn Game schedule off | Games without a schedule shows On, dimmed, "Always shown while Game schedule is off"; the board is the provider's events in their sports' rows, as before Build 125 |
| Quick settings | Blue on the board | Show on the board says "Hidden: … Games without a schedule …" while it is off |
| Leftovers | With the switch on, a slot with only a clock time (no date) that was there yesterday and names no scheduled game | Not in the Games without a schedule row; the Live Events report says "a leftover: not on the schedule" and "leftover" in the schedule column |
| Day guessed | A slot like "MNF 8:15pm Bears at Seahawks" posted the day before the game | No tile today; it is a source of the scheduled game's tile, at the game's day and time |
| My Sports | Pick High School or Pro Wrestling | Their games show in their rows (and Live now) as before |
| Report | Help › Send a report › Live Events | A last column "schedule" ("on the schedule", "no schedule", "leftover"; empty for slots that aren't events) |
| Speed | Open Live Events on the LG with the playback log on | The "rebuilt in …" and board times no slower than Build 124 (the board has fewer tiles) |
| Restart | Quit and reopen the next day | Yesterday's day-guessed slots no schedule has stay out (remembered with the saved board) |
| Small colleges | Volleyball, soccer and college basketball on; on a Monday, open Live Events and wait a few minutes (the rest of the week's schedule); find Flo slots like "volleyball:  Adelphi vs American International _ Women`s (Adelphi vs AIC)", "Southern New Hampshire vs Assumption", "Mount Holyoke vs Coast Guard Academy", "Mass_Liberal Arts vs VTSU_Castleton", a "hoops:" slot, and one stamped early in the morning ("… (2026-11-06 05:56:00)") | Each is a source of its scheduled game's tile (the scoreboard's "American Int'l", "Southern N.H.", "Coast Guard", "MCLA"; a game with no time set yet shows at its slot's time; the early-morning one joins that day's game); a "_ Men`s" slot never joins the women's game (nor water polo the volleyball game), a field hockey slot never the same schools' volleyball game, and "Roanoke vs Randolph" stays apart from Randolph-Macon. A game both the scoreboard and a small colleges' page list keeps its ESPN tile |

## Build 125 — Slot reading

Live Events reads provider event slots more carefully, so more of them join their real game and fewer games show twice. A slot whose service tag names a country (":Viaplay DK 03", ":Telia FI 13", ":TeliaPlay SE 22", ":Tv2Play NO", ":Tv4 Play SE", ":MTV FI", ":MAX ES"; ":VG 01" is Norway's) is read on that country's clock, AM/PM times included; slots in Danish, Swedish, Norwegian, Finnish and other European groups read unmarked times on their own clock too. A provider family's own country beats the group a copy is filed in ("AU (STAN 09)" in a US group is Sydney time, so both copies of a slot agree); Fanatiz slots are read as UTC−5 and Vix's as US Eastern. "all" in place of a competition is dropped and no longer makes a slot soccer ("Bears vs. Seahawks · all" in a soccer group now meets the NFL game). "A - B" and "A x B" read as a match ("SINNER - ALCARAZ", "MJÄLLBY - KALMAR"), "Surname, First vs Surname, First" as two players, and a competition glued to the teams goes after them ("LIGUE 2 REIMS - SOCHAUX" reads "Reims vs Sochaux · Ligue 2"; "… v …  UEFA Champions League Matchday 4 2026/2027" the same way). European spellings meet the schedule's ("Paris SaintGermain", "Olympiakos", "Olympique Lyonnais", "Deportivo A Coruña", "LASK"); copies of one match that differ only by competition or club initials ("VfL Wolfsburg v TSG 1899 Hoffenheim") are one tile. A women's side marked "(F)" reads "Women" and never joins the men's game of the same clubs. A Formula 1 slot naming a Grand Prix whose race has run (or one that isn't this weekend's, like July's British GP still in a slot) is left out as a replay; "Código F1", "Debrief", "Análise", "Analysis" and "Previo" slots are shows. The F group's driver feeds ("MAX VERSTAPPEN | RED BULL RACING | VER"), F1-TRACKER and F1-DATA are the race's cameras, not tiles of their own.

| Area | How to check | Pass |
|---|---|---|
| Country clocks | Open Live Events next to the Apple TV's; export the Live Events report | Viaplay DK / FI, Telia, TeliaPlay, Tv2Play, Tv4 Play, MTV, MAX ES and VG slots start within minutes of the game (e.g. "Florida vs Detroit @ Nov 3 1:05 AM :Viaplay DK" at the NHL game's time, not six hours off); no copy of one game in "Live now" while the others say later |
| Family clocks | The same report: Au Stan, Fanatiz and Mx Vix rows | Both Au Stan copies of a match the same time; Fanatiz and Vix slots join their games (Brazil, Argentina, Peru, Liga MX) at the game's time |
| "all" | SOCCER PPV slots "… · all" for NFL, tennis, basketball | No "· all" in titles; an NFL or tennis one joins its game, others sit in Other or Time not listed, not Soccer |
| Dashes | MAX FR tennis ("PLAYER - PLAYER"), VGTV and MTV soccer ("TEAM - TEAM") | Joined to the ATP match or the game; titles read "A vs B" |
| Duplicates | A busy evening of European football (Wolfsburg vs Hoffenheim from 10 groups, Mjällby vs Kalmar, Deportivo vs Osasuna) | One tile per match (two at most when one copy has no separator, "Mjallby AIF Kalmar FF"), with every group's feed under Where to Watch |
| Women's | Liga MX Femenil slots ("Pumas UNAM (F) vs. … (F)") the same day as the men's game | Their own tile ("Pumas UNAM Women vs …"), never under the men's game |
| F1 replays | The day after a Grand Prix: "2026 Formula 1: Mexico City Grand Prix" re-airs, "Código F1", DAZN "Debrief"; Viaplay's July "Storbritanniens Grand Prix … 2026" slots | None on the board; the report says "a replay: the race already ran" or "a studio show" |
| F1 cameras | The F group (one slot per driver, F1-TRACKER, F1-DATA) | No driver tiles in Time not listed; during a session, Down on the race shows them in the cameras panel; F1-TV stays one tile |
| Speed | Send a log after a rebuild on the LG | The rebuild takes no longer than in Build 124 |

## Build 125 — Schedules

Live Events looks further ahead and has today's games sooner. The week's schedule now reaches Thursday's and Friday's games from Monday: every soccer game listed eight days ahead (was five), Divisions II and III volleyball, Division III soccer and Divisions II and III basketball six days (were four or three), college volleyball and basketball five days (were three). The day's schedule asks for today's and tomorrow's games first (the month listing that has them, the first two days of day-by-day leagues, the current fight cards and race weekends) and saves them at once, so the board is made from them while the rest of the week comes in (last month's listing for last week's games, the later days, next week, next Sunday's race); the loading line ("Getting this week's games…") ends with today's part. On a TV that used Live Events in the last two weeks, the day's schedule is fetched in the background about 20 seconds after launch, once the channels are in, with Live Events closed: never while the list loads or sorts, a channel starts or keys are being pressed (it waits), one request at a time, never with Game schedule off. Opening Live Events then normally finds today's games already in. If the app closes before the rest of the week came, the next open asks only for the rest. Games of competitions the scoreboard doesn't list now come from a second calendar, and only when one of your provider's event feeds named the competition in the last two weeks and it fits your sports (soccer for the soccer ones, the NHL for Danish and Swedish second-tier hockey, Racing, Darts, UFC/PFL & boxing, NFL or college football for the CFL; snooker needs no pick): Kategoria Superiore, Mauritania's Super D1, Ekstraklasa, Superettan, Primera Federación, Burundi's Ligue A, A Lyga, Metal Ligaen, HockeyAllsvenskan, WEC, MotoGP and Supercars (every session of the weekend), PDC darts days, boxing and Oktagon cards, the CFL, snooker days, and UFC cards only while the scoreboard's own UFC isn't looked up. At most 40 requests a day to it, two seconds or more apart, after today's part; a refusal leaves it alone for the day, errors back off. Their games show only when one of your channels has them, live by the clock (no scores), with the competition as the tag; the source is never named.

| Area | How to check | Pass |
|---|---|---|
| Thursday games | On a Monday, open Live Events with volleyball and college basketball on; Filter › Also on, or a Division III volleyball provider slot for Thursday | Thursday's and Friday's games are on the board with their teams, not "Time not listed" slots |
| Soccer next weekend | On a Monday, a provider slot for Saturday's or Sunday's match of a league ESPN lists (Argentina, Brazil) | It joins the scheduled game (its tile has the match's time and both crests) |
| Background | Use Live Events one day; the next day start the app, stay in the guide for a minute, then open Live Events | The board comes at once (no "Getting this week's games…" line); the app log has "schedule: the day's fetch in the background" and "schedule: today's games in" before the open |
| Today first | Start the app and open Live Events at once (first open of the day) | "Getting this week's games…" for a shorter time than Build 124 (about a minute for many sports, seconds for a few), then the board; more games may join over the next minutes without placeholders |
| Busy TV | Start the app and start a channel straight away | The channel starts as fast as in Build 124; the schedule's requests begin only once it plays (log) |
| Off | Settings › Live Events › Game schedule off; restart | No schedule requests in the log, in the background or on open |
| New TV | A TV that never opened Live Events | No background requests; the first open fetches as before |
| Closed early | Open Live Events, close the app within 30 s, start it again and open Live Events | The board has today's games at once; the log shows only the later days asked for |
| Other calendars | With soccer picked and a provider carrying Superettan or Ekstraklasa (titles like "ÖSTER - HELSINGBORG SUPERETTAN"), open Live Events the next day | Those matches are tiles in the Soccer row with "Superettan" / "Ekstraklasa" as the tag, at the match's time; no score, live by the clock |
| Motorsport | Racing picked, a provider event naming WEC or MotoGP | The weekend's sessions (Practice, Qualifying, Race) as one race weekend tile, at their times |
| Darts, boxing, snooker | Darts or UFC/PFL & boxing picked, a provider showing PDC, boxing or snooker | The day's event as a title tile ("Players Championship 33", "Joshua Buatsi vs Willy Hutchinson 2"); snooker in More events |
| Not your sport | The same provider, soccer not picked | No Superettan/Ekstraklasa requests in the log, no such tiles |
| UFC | UFC/PFL & boxing picked | UFC cards come from the scoreboard as before, never twice |
| Budget | Send a log after a day of use | "calendars: N asked …, M today" with M at most 40; the schedule line's requests as before (about 135 a day for the owner's picks, 15 for NFL, NBA and MLB only) |
| Refused | (Developer) make the calendar host answer 429 | The calendar is left alone until the next day; the scoreboard's leagues still come |

## Build 126 — Guide top

The guide's top gives the picture more room. The preview is 640×360 at the top right (was 400×225), rounded with a faint outline as before and with no live mark on it; the clock sits left of it, at the end of the programme column. The column beside it (1024 wide) has the neighbourhood and the header's status line; the channel's logo, number, name, ★, feeds, EAST/WEST and "Also on … at 9:00 PM"; the title at 50 px; the status row: the time range in white, "· episode" and NEW, with no LIVE word or red mark anywhere in the top (the progress bar and "N min left" beside it say the show is on now); a later show keeps LATER and "Starts in 18 min" ("Starts in 1 h 20 min" an hour or more ahead; a show already over says "Ended"); the description in three lines at 22 px (was two at 20); and "NEXT 9:00 PM · Title · Episode", the show after the focused one on that channel. The preview always plays the channel's show on now, so while a later show is focused it says what it plays on a dark band at its bottom: "NOW · <title> · N min left" (no red dot). The ruler moves down to y 424 and the rows start at 476: five rows and a part (was six and a part). Live Events and Search keep their corner where it was, with its LIVE chip, so the Live Events "Same places" row above now holds for its own corner only. The in-player guide is unchanged. The top's words are made when focus moves to another show, the lists change or the minute turns, never per frame; the preview's rest and gap are as before. The picture's place snaps, never animates: back in the guide with a game still previewing in Live Events' corner, the picture moves into the large preview and the channel's card shows there until the TV has placed it (about 0.3 s).

| Area | How to check | Pass |
|---|---|---|
| Layout | Open the guide | The preview at the top right, its top level with the clock; the column at the left with nothing running under the preview; the ruler under the preview; five full rows and part of a sixth above the key bar |
| On now | Rest on a show on now | The time range in white, then the episode and NEW; no LIVE word or red mark in the top or on the picture; the progress bar with "18 min left" beside it |
| Later show | Right to a later show on the same row | LATER, "Tonight · 9:00 – 10:00 PM", "Starts in 1 h 20 min"; the picture keeps playing, with "NOW · <the show on now> · 18 min left" on a dark band at its bottom, readable over a bright picture |
| Another row | Down onto a channel, then Right to its later show | The band shows only once that channel plays in the preview (after the usual rest), never over the previous channel's picture |
| Next | Rest on a show on now, then on a later one, then on the last listed show | "NEXT 9:00 PM · Title · Episode" under the description for the show after the focused one; none after the last |
| Description | A show with a long description | Three lines at most, the last ending in "…", clear of the NEXT line |
| Channel line | A favourite East/West channel with feeds | Logo, number, name, ★, the feeds chip, EAST/WEST and "Also on … West at 11:00 PM" on one line, the "Also on" cut before the column's end |
| Chips | Tonight ▼ › Tomorrow Night, then Filter › Sports on now | "BACK returns to Now" and the filter chip on the top line, left of the clock, never under it |
| Grid | Scroll through a long lineup; CH+ / CH-; open the neighbourhood column; point at rows and cells (LG) | The focused row stays in view with one below; the column's header sits in the ruler row and its list starts with the rows; every row and cell is a click target where it is drawn |
| Digits | Type 2 0 6 | The number box under the preview's right edge |
| Player | OK on a show on now, then Back | Full screen on the same stream, no re-tune; back, it shrinks into the large preview |
| Live Events | Open Live Events from the guide | Its clock, corner (400×225 with the LIVE chip) and buttons where they were; the guide's preview doesn't carry into it, as before |
| Back from Live Events | Rest on a live game until it plays in Live Events' corner, then Back to the guide | The game moves into the large preview: the card (logo, spinner) for a moment, then the picture; never a cut-off picture or one framed in black; no animation |
| Search | Search, rest on a live channel until it plays muted, Back to the guide | Search's corner as before; back in the guide the picture moves into the large preview the same way |
| Previews off | Settings › Guide › Preview channels off | The large card with the channel's logo; no NOW band |
| In-player guide | Left in the player | As in Build 116 |
| Redraws | Leave the guide resting on a show on now and send a log | While idle nothing redraws except once a minute (the clock, the bar and "N min left"); the "loop" line as in Build 120; the preview starts after the same rest as before |

## Build 127 — Choosing your sports (screens)

My Sports' first step is one grid of sixteen tiles in a fixed order: NFL, College football, NBA, WNBA, College basketball (men's and women's together), MLB, NHL, Soccer, College volleyball, Tennis, Golf, Motorsport, Boxing and UFC, Cricket, Rugby and "More sports ›". Each tile says what it covers ("Pro football", "Top 25 and your schools"; Soccer lists its ticked competitions); no tile shows how many events. Until My Sports is set up it starts from the defaults for your languages, and a pick ticked for a language has a green line saying so ("For Español", "+ Liga MX and Mexico, for Español"). OK ticks; holding OK (☰ on Fire TV, click and hold on Apple TV) on Soccer opens "Which competitions?" (groups on the left, competitions in a grid, Back saves), and on a college tile opens "College sports": per sport, Top 25 and Every Division I game switches, "Your conferences ›" (a side panel to tick) and "Your schools ›" (conferences on the left; the first tile "All of the <conference>" follows all of it). "More sports ›" lists lacrosse, field hockey, softball, darts, pro wrestling, high school and any other sport. "I don't watch sports" saves no leagues: the board then has no sport rows and shows More events whatever Settings says. On the board, a row's title has no counts, only how it is filtered in gray ("College football · Top 25 and your schools"), a ranked school has its rank as a small gray number before its name, and Soccer and college rows end with "Choose competitions" / "Choose schools". A one-time note after the update stands where the game's hero is. The game card and OK held on a game offer "Follow <team>" for each team and "Hide <competition>"; hiding leaves "<name> hidden" with Undo at the bottom. Settings › Live Events › Hidden lists everything hidden (pro wrestling and high school too) with Show again.

| Area | How to check | Pass |
|---|---|---|
| First run, English | Set up with US English only | The sixteen tiles in order; NFL, College football, NBA, WNBA, College basketball, MLB, NHL, Soccer ("Premier League, Champions League, MLS, NWSL, USA") and College volleyball ticked; no green lines; no numbers on tiles |
| First run, Español and Hindi | Set up with US Spanish and an Indian language | Soccer's green line "+ Liga MX and Mexico, for Español"; Cricket ticked with "For Hindi"; the lead mentions the languages |
| Tick | OK on Tennis, OK on College basketball | Tennis ticked (green ring and check); College basketball ticks or unticks both basketballs |
| Soccer | OK on Soccer twice | Unticked, then the same competitions ticked again |
| Competitions | Hold OK on Soccer | "MY SPORTS · SOCCER", "Which competitions?", groups on the left (Popular first), a three-column grid; OK ticks; ◀ from the first column to the groups; Back returns with the tile's line changed; nothing saved until the last step |
| College | Hold OK on College football | "College sports" with Football, Men's basketball, Women's basketball, Volleyball on the left (lacrosse, softball and field hockey only when picked); Top 25 on, Your conferences, Your schools, Every Division I game off |
| Schools | Your schools › | "MY SPORTS · COLLEGE FOOTBALL · SCHOOLS", the conferences on the left (once the week's games are in), "All of the <conference>" first; ticking a school shows it on "Your schools" after Back |
| More sports | OK on More sports › | Lacrosse, Field hockey, Softball, Darts, Pro wrestling, High school; Back or Done returns to the grid on More sports |
| No sports | "I don't watch sports" | Setup moves on; the board has no sport rows and every game is in More events, even with More events off in Settings |
| Row titles | Open the board | No numbers after any row title; college rows say "· Top 25 and your schools" in gray |
| Rank | A college row with ranked teams | "4 Oregon": the rank small and gray before the name |
| Choose | Right to the end of Soccer and College football | "Choose competitions" / "Choose schools" tiles take the focus (the hero says what they change); OK opens the page; Back saves and the row changes |
| Note | A setup from before the update | The note in the hero's place with OK focused; OK closes it for good; Change opens My Sports; Up from the first row's filter returns to it |
| Follow | A game's card (an upcoming game), Down to the second row | "Follow <team>" for each team; OK follows ("Following <team>") and the pill turns to "Unfollow" |
| Hide | On the card, "Hide Liga MX" | "Its matches leave your Soccer row. Bring it back in Settings, Hidden." under the row; OK closes the card, the row loses its Liga MX games and "Liga MX hidden  Undo" shows at the bottom for a few seconds; OK there brings them back; any other key closes the note |
| Hold OK | Hold OK on a game | The panel names the game and its competition: Where to Watch, Remind Me, Follow each team, Hide (with the same line) |
| Hidden | Settings › Live Events › Hidden | "Hidden from Live Events" with Liga MX ("Hidden from a game card"), Pro wrestling and High school ("Hidden unless you turn it on"), each with Show again; Show again puts it back on the board |
| Pointer | Point and click (LG) on tiles, groups, options, the Choose tile, the card's pills, the note's buttons and Undo | Each takes the focus under the pointer and acts on click |

## Build 128 — Live Events top like the guide's; a game's sources ticked

Live Events' top takes the guide's layout (Build 126): the 640×360 preview at the top right with no live mark on it, the clock at the end of the column beside it, and the column at the left (1024 wide): the competition ("NFL · Week 9 · Sunday Night") where the guide has its neighbourhood; the channel's logo and name; the two badges and the matchup at 50 px; the status at 25 px (live is its red text, as on the tile); the progress bar; the where line in two lines; the venue and the tile's detail. The rows start under the preview (y 424, was 330). The one-time note and the "Choose" tile's words sit in the same column. Search keeps its 400×225 corner.

While a game plays, its Sources (the player's Sources pill, or Menu) is Where to Watch with checks like a channel's Sources: the source playing is ticked and says "Playing now" ("Opening…" while it opens); OK on another switches in place, the panel stays open and the tick moves. From the board or a game's card, Where to Watch is as before (OK starts the game).

| Area | How to check | Pass |
|---|---|---|
| Layout | Open Live Events | The preview at the top right, its top level with the clock; the column at the left with nothing under the preview; the first row's title and the filter button under the preview |
| Live game | Rest on a live game until it plays | The picture in the large preview with no LIVE chip; the clock in red text in the column; the progress bar |
| Upcoming game | Rest on a later game | No preview; the faded badges at the top right; the start time in the status |
| Guide and back | From the guide with a channel previewing, open Live Events; rest on a live game; Back | The picture stays in the same large preview, never a cut-off picture or one framed in black |
| Player | OK on a live game, then Back | Full screen on the same stream; back, it shrinks into the large preview |
| Sources ticked | Watch a game with two or more sources, open Sources | The source playing ticked with "Playing now"; the others with an empty check |
| Switch | OK on another source | "Opening…", then "Playing now" with the tick on it; the panel stays open; Back closes it to the same player (one Back to the board, not two) |

## Build 129 — My Sports teams: the league list scrolls

With many leagues picked (Build 127's defaults pick about a dozen), the teams step's rail drew only the first six and Down moved focus through the rest unseen, so focus seemed to vanish on the way to Continue. The rail now scrolls with the selected league, with ▲ / ▼ when more are above or below, and pointer clicks pick the league under the pointer. A tile focused that a fresh team list doesn't have moves to the last tile (or the rail).

| Area | How to check | Pass |
|---|---|---|
| Down to Continue | First-run setup with the default sports, teams step: Down from the first league to the end | Each league in turn is highlighted, the list moving up as needed; then "Remind me before my teams play", then Continue; focus is always visible |
| Up again | Up from Continue to the first league | The same in reverse, ▲ gone at the top |
| Pointer | Point at and click leagues after scrolling (LG) | The league under the pointer opens |

## Build 130 — First run waits for the guide

On a first run there is no saved guide, so after setup the guide showed grey placeholders for minutes. Setup now has an eighth step after Teams, "Getting your guide ready" ("STEP 7 OF 8 · YOUR GUIDE"): your channels, My Lineup and TV listings each tick in turn, the listings with a bar while they download when the provider says how big the file is (just the spinner otherwise), and it moves on to "You're all set" by itself. Only the provider's listings are waited for. A provider with no guide file (Emby, an M3U without a guide link) ticks the listings at once. If the download fails, or is still going 5 minutes after the step opened, the step turns amber, says so, and Continue appears; the listings keep loading either way. In the guide, rows still waiting say "Listings on the way" (the top too, with a small spinner) instead of pulsing grey, and "No guide info" once nothing more is coming; the top line says "Loading the guide for the first time…", "Updating the provider's guide…" or "Filling in missing listings…" with a spinner, and "Missing listings wait until you stop watching" while you watch full screen. Later launches are unchanged.

| Area | How to check | Pass |
|---|---|---|
| New step | First-run setup with an Xtream provider, through Teams | "Getting your guide ready" with "This happens once. Next time the guide opens straight away."; eight steps along the bottom, Your guide highlighted; no button |
| Ticks | Watch the step | Your channels and My Lineup ticked green; TV listings with a spinner; then all three ticked and, a moment later, "You're all set" with "My Lineup and your guide are ready" in its first box |
| Bar | A provider whose guide file gives its size | A bar under "Your provider's listings" that fills and never goes back; no time left or numbers anywhere |
| No guide file | First-run setup with Emby / Jellyfin, or an M3U without a guide link | TV listings ticks as soon as My Lineup does; setup moves on by itself |
| Failure | Setup with the TV's network cut just before the Teams step's Continue | TV listings turns amber with "Your provider's listings couldn't be loaded right now" and "They keep loading in the background. Channels play now; shows fill in as they arrive."; Continue focused; OK opens "You're all set" |
| Slow | A very slow provider guide (over 5 minutes) | The same amber step with "Your provider's listings are taking longer than usual" and Continue; if it finishes first, setup moves on by itself |
| Back | Back on the step, then Continue on Teams | Back to the teams; forward again, the step waits on where it was (the download never restarted) |
| Rows on the way | Start watching right after setup | Rows without listings yet say "Listings on the way" on a faint cell; the top says "Listings on the way" with a small spinner where the times go; the top line "Filling in missing listings…" with a spinner |
| Waiting | Watch full screen while the missing listings load, then Back to the guide | The top line says "Missing listings wait until you stop watching"; once they're done, rows with none say "Live · No guide info" |
| Later launch | Quit and open the app again | The saved guide shows at once; no setup step; the top line says "Updating the provider's guide…" with a spinner when it updates |

## Build 131 — Storage stays bounded; crash notes

On webOS every developer-mode app shares one small partition (2.7 GB on an LG C1). A week of crashes left a dozen core dumps of 91–429 MB in the app's install folder (its working folder) and filled the partition, which stops the TV installing or updating any app. The app now turns core dumps off at launch, works from `/tmp` instead of the install folder, and removes any `core` / `core.<number>` files an older build left there and in its data folder, along with writes a crash cut short (`*.tmp`, `*.part`). A write over 1 MB (a guide, the saved channel list, the provider's links) first checks the free space and is skipped when it would leave less than 100 MB free (50 MB on Android and Apple TV): the app keeps using what it has in memory, keeps the old file, and logs it once. Logos are not kept on disk while space is that short. The data folder has a budget (400 MB on the TV): files with no owner go (a removed provider's saved guide, a guide area no longer chosen, the channel list of an old set of providers, all outside guide files when "Fill in missing listings" is off), then, while over budget, the least recently used logos, then the backup guide files; this runs at launch and after each big save. `marigold.log` is rotated at 1 MB as before (now counting the lines it starts with). Instead of a core dump, a crash leaves `crash.txt` in the data folder (the signal, the build, how long the app had run and the raw backtrace; no links or logins) and the same lines at the end of `marigold.log`; the next launch logs "previous run crashed: <signal>", keeps the note one more launch as `crash-previous.txt`, adds "previous run crashed" with a one-line summary to the playback log (Help's export) and shows it on Diagnostics.

| Area | How to check | Pass |
|---|---|---|
| Old cores removed | On a TV with `core.*` files in the app's folder (or `touch core core.12345 corefile` there over the developer shell), open the app | `ls core*` in the app folder shows only `corefile`; `marigold.log` has "removed 2 core dump(s)" |
| No new cores | Use the app for a few days, then `ls core*` in `/media/developer/apps/usr/palm/applications/org.marigold.app/` | Nothing (and nothing new in the data folder either) |
| Working folder | `ls -l /proc/$(pidof marigold)/cwd` while the app runs | Points to `/tmp` |
| Data folder size | `du -sh` on the data folder (`.marigold` in the app's home, or `/media/developer/temp/marigold`) after a day with the default guide areas | Under 400 MB; `guides/` holds only the chosen areas' files |
| Area turned off | Settings › Guide › Guide listings: turn United Kingdom on, wait for the listings, turn it off, wait for them again | `guides/uk` is gone afterwards |
| Provider removed | Remove a provider with a guide file | Its `provider-….json` and the old `catalog-…` files are gone a moment later; the other provider's stay |
| Nearly full | Fill the partition to under 150 MB free (a large file in `/media/developer/temp`), then Settings › Guide › Guide listings › Refresh listings now | The guide still fills in; `marigold.log` says "… not saved: … free" once per file; no `*.tmp` or `*.part` left; delete the large file afterwards |
| Log cap | `ls -l marigold.log*` after a long session | `marigold.log` and `marigold.log.1` each about 1 MB at most, plus `marigold-previous.log` |
| Crash note | Over the developer shell: `kill -SEGV $(pidof marigold)` | The app closes; no core file; `crash.txt` in the data folder starts "Marigold crashed: SIGSEGV" with build, uptime and backtrace lines, under 64 KB |
| Next launch | Open the app again | `marigold.log` has "previous run crashed: SIGSEGV" and the note; `crash-previous.txt` replaces `crash.txt`; Help's playback log export has a "previous run crashed" row; Diagnostics shows "last run crashed: SIGSEGV" |
| One more launch | Quit and open the app again | `crash-previous.txt` is gone; no crash row is added |
| Keyboard guard | With the note's handlers in place, type in Search with the TV's keyboard (webOS 26) | Typing works as in Build 130; if the TV's keyboard fails, the app's own keyboard takes over without a crash |

## Build 132 — Search in the player

Search without leaving what you're watching. The in-player guide's lists (Left from the first show, then Left again) have Search at the top, with the same icon and name as the main guide's left rail: Search, My Lineup, Favorites, Recent, Live Events (when the game schedule is on), All Channels. OK on Search turns the guide's card into search, over the left two thirds of the picture, which keeps playing on the right with its sound: "SEARCH" with "◀ Lists" at the top, the Search screen's own keyboard on the left (letters and 123 pages, Delete · 123 · Clear, 🎤 Speak where the Search screen has it: not on Fire TV), and results on the right as you type, from the same search as the Search screen: the top result, then Channels, Games and Shows, your lineup first, never a count. Nothing previews and nothing tunes until you pick: OK on a channel or anything on now switches the picture in place (CH+ / CH- then go through My Lineup, and it goes into Recent) and closes the card; a game on now plays the game in place; a later show or game opens its details with Remind Me over the card, and Back there returns to the results. Back goes from the results to the keyboard, then to the lists; Back in the lists goes to the guide as it was (Back again closes it), and OK on Search there brings the keyboard back. What you typed stays for the next time you open search until you leave the player. The games rail's lists have Search too: it opens the guide in the player on search. The Search screen looks and works as in Build 131. No new buttons in the player or beside "Now ▼".

| Area | How to check | Pass |
|---|---|---|
| Lists | Watch a channel, Left until the lists open | Search first, with the magnifier icon; then My Lineup, Favorites, Recent, Live Events (with the game schedule on), All Channels (with its tab on) |
| Open | OK on Search | "SEARCH" and "◀ Lists" at the top of the card; the keyboard on "a"; the picture keeps playing on the right with its sound; the legend says "OK type · Back: lists" |
| Typing | Type "pulse" (a channel in your lineup) | Results appear as you type; the top result a moment after you stop; then CHANNELS, GAMES, SHOWS headers as there are results; your lineup's first; no counts; the legend adds "▶ results"; typing stays quick on an LG |
| Keyboard | 123, Delete, Clear, Space; Red on LG; number keys | As on the Search screen: 123 flips the page and stays on the key, Delete drops one letter, Clear empties the field and goes back to the letters, Space only after a word; Red clears; number keys type digits |
| Speak | 🎤 Speak on LG / Android TV / Apple TV; look for it on Fire TV | The TV's keyboard opens; what you say comes back into the field and the results follow; no Speak on Fire TV |
| Results | Right from the keyboard's right edge (or ▶) | Focus on the first result; Up/Down move, the list scrolls; Left (or Back) goes back to the key you left; the legend says "▲▼ results · OK watch here · ◀ keyboard · Back: keyboard" (or "OK details" on a later show or game) |
| Watch a channel | OK on a channel result | The picture switches to it in place (no second player), the card closes; CH+ / CH- go through My Lineup; it is first in Recent |
| On now | OK on a show on now, or a game on now | The picture switches in place to its channel, or plays the game (Up/Down through the other games, as from Live Events); the card closes |
| Later | OK on a later show; then on an upcoming game | The show's details with Remind Me over the card; the game's details card with Remind Me and Where to Watch; the picture never changes; Back returns to the same result |
| Back chain | Back from the results, the keyboard, the lists, the guide | Keyboard; the lists on Search; the guide as it was before search (same list, row and time); the guide closes |
| Lists over search | Left from the keyboard's left edge, or Back from it; OK on Search; then pick My Lineup | The lists slide over the dimmed search; OK on Search brings the keyboard back with the same query; My Lineup opens its neighborhoods as before |
| Query kept | Type "harbor", Back to the guide, close it; Left until the lists, Search | "harbor" and its results again |
| Query cleared | Leave the player (Back to the guide), play a channel, open search in the player | The field is empty |
| Pointer (LG) | Point and click keys, results, ▶ and "◀ Lists"; click beside the card | Keys type, a result plays or opens its details, ▶ goes to the results, "◀ Lists" opens the lists; beside the card closes the guide |
| Apple TV | Swipe across the keys, then on to the results | Swipes glide across the keys and stop at the keyboard's edge; the next swipe goes to the results (or the lists); Play/Pause flips 123 / abc; one swipe in the lists moves one list |
| Games rail | While watching a game from Live Events: Left until the lists open, OK on Search | The guide in the player opens on search; picks work the same |
| Search screen | Search from the main guide's rail: type, results, corner preview, OK, Back | As in Build 131 |
| Idle | Leave search open without typing (developer log) | The loop sleeps between redraws, as in the in-player guide |

## Build 132 — Sources in your languages first; sports channels from other countries

A game's ways to watch now go by your languages, the languages of the countries and languages you picked for your channel list (United States English, US Spanish, Mexico, United Kingdom, India's Hindi and English…; English when nothing says). Every channel and event feed is tagged with its language once, when Live Events reads your channels: from its country, the language the channel lists give it, and marks in its name ("(ES)", "Español", "English", "Hindi"); unknown when unsure. Channels filed under "AR" read as Arabic for the Gulf's sports brands (beIN, SSC, Alkass, AD Sports) and Arabic names, as Spanish for Argentina's (TyC, TNT Sports), unknown otherwise. A game's sources go: My Lineup's first (you chose them, whatever their language), then those in your languages, then unknown ones, then other languages; within each, channels, event feeds, then team channels, by station. Extra stations are counted per language, so another language's never push yours out; nothing else is removed. The tile names, and Watch starts on, a channel in your languages (or an unknown one, or My Lineup's), else your languages' event feeds; a game only on another language's channels names the first of them with "In Arabic" (or the language) under it. "Also in Spanish" still shows when a game is also on a US channel in another language (English or Spanish, or another of yours). Where to Watch lists "Your channels" (your languages and unknown ones), then a section per other language present ("In Spanish", "In German", "In Arabic"…, its channels and feeds), then "Event feeds from your provider". There is no setting.

Sports channels from other countries are now ways to watch too: the sports brands Live Events knows (Sky Sports, TNT Sports, Premier Sports, LaLiga TV, Movistar, DAZN, Sky Sport, Magenta Sport, ServusTV, Canal+, beIN Sports, Sport TV, Premiere, SporTV, TUDN, Azteca Deportes, TyC Sports, TSN, Sportsnet, RDS, TVA Sports, SSC, Alkass, AD Sports, Willow, Star Sports, PTV Sports, Ten Sports, A Sports, Fox Cricket, SuperSport, Eurosport, Tennis Channel, Sky NZ) among your channels from countries you didn't pick, with a guide id from your provider and not in a hidden group. Their listings are kept from your provider's own guide file, which is already downloaded whole (no other download, only the next day of them is kept); a game their listing names gets them as a source, last, "From another country", two at most per game. They join nothing else: not the guide's lists, My Lineup, search or the channel counts. Brand channels of a country you did pick are read the same way (as ordinary sources). Star Sports, PTV Sports, Ten Sports, A Sports and SuperSport now count as cricket channels ("Live: T20 Cricket" on them is the match in play).

| Area | How to check | Pass |
|---|---|---|
| Your language first | US English only; a game on an English channel and a Spanish one (Univision, ESPN Deportes) | The tile names the English channel with "Also in Spanish"; Where to Watch: "Your channels" with the English one, then "In Spanish" |
| US Spanish | Settings › My Lineup › Countries & languages: US Spanish only (US English unchecked); the same game | The tile names the Spanish channel ("Also in English"); "Your channels" lists the Spanish one first, "In English" after |
| Both | US English and US Spanish | Both in "Your channels" by station; no "In …" section for either |
| My Lineup first | Add a Spanish channel to My Lineup (English viewer) showing a game also on an English channel | The tile names the My Lineup channel ("In your lineup"); it's first in "Your channels" |
| Feeds | A game on an English event feed and a German one | Where to Watch: "Your channels" (if any), "In German" with the German feed, then "Event feeds from your provider" with the English one; Watch plays the English feed first |
| Other countries' sports | US only, with a provider whose list has UK or Arabic sports channels (e.g. "UK: Sky Sports Premier League") and its guide; open Live Events the next day after the guide updates | A Premier League game whose listing is on such a channel gains it in Where to Watch: under "Your channels" for an English-language one, "In Arabic" for beIN Arabic, with "From another country"; at most two such per game |
| Only abroad | A game no US channel or feed has, but a UK sports channel's listing names | The game is watchable; the tile names that channel ("From another country" in Where to Watch); Watch plays it |
| Nothing else | The guide, All Channels, My Lineup, Search for "Sky Sports" | Those channels aren't in the guide's lists or My Lineup; Search finds them only by their exact name, as before |
| Hidden group | Hide the group those channels are in, then Settings › Guide › Guide listings › Refresh listings now | Their games no longer list them |
| Arabic / Argentina | A list with "AR| beIN SPORTS 1" and "AR| TyC Sports" | beIN under "In Arabic", TyC under "In Spanish" |
| Wording | Every Where to Watch on the board | Section titles are "Your channels", "In <language>" or "Event feeds from your provider"; no channel counts anywhere |
| Old board | Update over Build 131 with Live Events used before | The saved board shows at once; Spanish sources still under "In Spanish"; the next refresh sorts by language |
| Speed | `marigold.log` timing lines after a day of use | "boardMax" as before; the provider guide's update a little larger at most |

## Build 135 — The game's Sources tick the source playing

In the player, Sources for a game (Where to Watch with ticks) ticked the channel at the top whatever source you picked: a channel showing the game has the game's event feeds as its backups, and the tick went to the first row whose feeds held the one playing. Now the tick is on the source you picked while it plays; if it fails and a backup takes over, the tick moves to the backup that plays. A feed you pick starts first even when this TV couldn't play it lately (it is then left at once for the next one).

| Area | How to check | Pass |
|---|---|---|
| Feed picked | A game on a channel (top of Sources) and on two or more event feeds; play it, open Sources, pick the second feed | The tick and "Opening…", then "Playing now", are on that feed, not on the channel |
| Each pick | Pick another feed, then the channel, then a feed again | The tick follows each pick |
| Backup | Pick a feed that doesn't start | After it fails, the tick is on the source that plays (usually the channel), not the dead feed |
| Watch | From the game's card, Watch, then Sources | The tick is on what plays first |
| Channel Sources | A channel (not a game), Red key / Sources, pick Feed 2 | As in Build 134: Feed 2 ticked, "Playing now" |

## Build 137 — Playback and sources: placeholder clips skipped, direct versions first, measured picture labels

A provider's placeholder ("slate") clip is served as the stream: each connection delivers the same file and closes, the relay reopens it, and it plays as a real picture. The picture check only moved on after 45 s, and never on a channel. Now two connections in a row that delivered the same sizeable file (to within a few KB) move to the next source at once, with no retries; on a channel with one version the "This channel stopped working" card shows instead of the clip looping. Sources says "placeholder clip" for it. A source whose connections keep closing now counts connections of up to 8 s (was 5 s), so sources that close every 4–7 s are caught too.

A channel's versions: the ones that start in about 2 s (Xtream, M3U) come before the ones opened on a server first (Emby / Jellyfin, about 10 s) even when a source's record would have moved a server one ahead, as long as a direct version hasn't failed lately. "Always start with" and the version that played last still start first. A game's event feeds are ordered by how well they have started (a feed that failed lately goes last, then the group's starts this week), and only then by picture quality; channels still come before feeds, and your languages before others.

Picture labels: Sources rows, a game's Where to Watch rows and the player's status line show the picture a version actually played ("1080p · 60", "720p", "4K · 50") once it has played 20 s on this TV; otherwise what its name or group claims ("FHD", "HD", "SD", "4K", superscript tags such as ᴴᴰ, ᵁᴴᴰ, ⁶⁰ᶠᵖˢ included, "· 60" for a 50/60 fps claim); otherwise nothing. An unmarked feed no longer says "HD". Never 8K, RAW or VIP. The measurements are kept for the 2,000 channels measured last (pictures.json: ids and numbers only).

Playback log: each start writes a "launch" line (build, version, system, and the device's maker and model on Android). Tune numbers go on from the highest in the saved log instead of starting at 1 again after a run that ended on a memory or load line.

My Lineup: ESPN8 The Ocho, Ryz Sports and ACC Digital Network are under Sports (were Entertainment), STARZ Kids under Kids. A network's feed tagged with a market's city in parentheses ("(Chicago) Telemundo East") is that city's station, not a copy of the national feed, so it no longer joins the national entry for viewers elsewhere. Universal Kids has shut down: it is no longer suggested (still listed, and can be added, while a provider carries it). The channel list sorts once more after the update.

| Area | How to check | Pass |
|---|---|---|
| Placeholder clip | Tune a game feed known to show a provider's "event not started" slate, with other sources | Within about 15 s of the picture it moves to the next source ("No picture…" / backup note), not after 45 s; Sources shows "placeholder clip" on it |
| One version | A channel with one version that serves a slate | The "This channel stopped working. The source only sends a placeholder clip." card, not a looping clip |
| Live stream | Watch a normal channel through the app for 10 minutes | Never moved off for "placeholder clip" or "source keeps closing" |
| Direct first | A lineup channel with both Xtream and Emby versions, Sources | Xtream versions listed first (unless one is set in "Always start with" or an Emby one played last after the Xtream ones failed) |
| Pinned | Set "Always start with" to an Emby version, retune | It starts first |
| Labels | Sources on a channel whose provider names have no quality word | No "HD" before the provider name; after it plays 20 s and Sources is reopened, "1080p · 60" (or what it plays) |
| Where to Watch | A game with channels and event feeds | Measured or claimed picture in the rows' detail lines; no 8K / RAW / VIP |
| Launch line | Help › Send a report › Playback log after a relaunch | A "launch" row with the build number and system at each start; tune numbers keep counting up |
| Lineup | Sports section; Kids section; Telemundo for a viewer outside Chicago | ESPN8 The Ocho / Ryz Sports / ACC Digital Network in Sports; the national Telemundo's Sources have no Chicago version; Universal Kids not suggested |

## Build 137 — Live Events: renamed slots reported as such; no rebuild when nothing changed; college soccer follows Top 25; UEFA slots in UK time

The LG report of Oct 28 – Nov 4 had most "LIVE | … | 8K EXCLUSIVE" and "NEXT | …" slots left out with "no tile took it". Matching takes them (the same slot names, given to Build 130 and to this build, go to their games: 61 of the 67 Europa and Conference League slots on the schedule). The report was made with Live Events closed after the channel list had been read again (every half hour, Live Events open or not): the provider renames those slots in place, and Live Events matches again only when it's open. The report now says so: "new or renamed since Live Events last matched (it matches again when opened)", last column "not matched yet"; a slot renamed from one game to another no longer shows the old game's tile. Opening Live Events matches them as before. Every slot matching leaves out has its reason; the old catch-all is gone.

With Live Events open, the guide's game listings are read again every 10 minutes, and each read was matched again (on the LG about 7 s each, often to the same result). Now a read that brings the same listings (those the half hour's matching can use), with nothing else new, isn't matched again; the log says "guide listings read again: the same, nothing new to match".

College soccer (Divisions II and III from the NCAA's pages, Division I from the all-soccer listing) follows the college rule in the Soccer row, as the other college sports do in theirs: a game shows there when a side is ranked 1-25, is one of your schools, or every game is on for it; otherwise it's in More events ("college game without a ranked team or one you follow"). Pro soccer still follows the competitions you ticked. The Soccer row looks as before.

The UEFA groups' slots ("UEFA | 08 - Team A vs Team B 20:00pm", "… 5:45pm") give UK kickoff times: 20:00 is the 8 PM UK kickoff (noon Pacific), not 8 PM Eastern (5 PM Pacific). The report's start time for them is now the kickoff's; their tiles already showed the schedule's time.

A slot listing several games ("A vs B • C vs D (A vs B)") names the one on now in brackets; it goes to that game, as before (checked: every such slot in the report names one).

| Area | How to check | Pass |
|---|---|---|
| Report, Live Events closed | Open Live Events, close it, watch a channel for over half an hour, then Settings › Help & info › Send a report | Slots renamed meanwhile say "new or renamed since Live Events last matched", last column "not matched yet"; none says "no tile took it" |
| Report, Live Events open | Open Live Events for a minute, then send the report | Those slots are on their games' tiles (or left out with a reason) |
| No rebuild for nothing | Leave Live Events open 20 minutes (developer log) | Every 10 minutes "guide listings read again: the same, nothing new to match" unless something changed; "rebuilt in" at the half hours and when the list, schedule or listings change |
| Board unchanged | Leave Live Events open over a half hour while a game starts | The game turns live at its time as before |
| College soccer | My Sports with "Every other competition" ticked, on a weekday in the fall | The Soccer row has the pro competitions and only college games with a ranked side or one of your schools; Division II/III games are in More events with the college reason |
| Your school | Follow a small college's soccer team from its tile | Its games are in the Soccer row (and Your teams) |
| Pro soccer | Only Premier League ticked | Soccer row: Premier League only, as before; college games in More events ("not in your soccer competitions") |
| UEFA times | The report on a Champions League or Europa League night | UEFA slots saying 20:00 start at noon Pacific (3 PM Eastern), 17:45 at 9:45 AM Pacific |

## Build 138 — The Mac app

Marigold for macOS 12 and newer, Apple Silicon and Intel: the `marigold-mac-NN` artifact (a `.dmg`). Install and keys: `docs/mac.md`. Built from the same code as the TV apps with the Apple TV's backends (Metal, Core Text, VLC); the window scales the 1920x1080 picture to its size. A computer's keyboard types straight into Search (the main Search and the in-player guide's), and Search on a Mac has no 🎤 Speak key; on the TVs nothing changes.

| Area | How to check | Pass |
|---|---|---|
| Install | Open the .dmg, drag to Applications, open; allow it in Privacy & Security | Marigold opens in a window with the setup's first screen |
| Setup | Go through setup with the keyboard (arrows, Return, Esc), type an account's server, user name and password (paste works) | Each field shows what you type; Return goes to the next; the guide fills as on the TV |
| Window | Resize the window, make it tall and narrow, then wide | The picture keeps its shape with black bars; nothing is cut off |
| Full screen | ⌘F (or the green button), quit, open again | Full screen, and it opens full screen again; Esc on the guide leaves it |
| Trackpad | Move the pointer over the guide, click a channel, two-finger scroll, two-finger click a channel | Hover highlights; a click opens it; scroll moves the list; the two-finger click opens its actions |
| Playback | Play a few channels and a game; Space; Page Up / Page Down | Picture and sound; Space pauses and plays; channels change |
| Search | Open Search and type a team's name; Delete; Down and Return on a result | Letters appear as typed; Delete removes one; the result plays |
| In-player guide | While watching, open the guide, go to Search, type | The letters go into its field |
| Sleep | Leave a channel playing 15 minutes without touching the Mac | The display stays on |
| Minimize | ⌘M while playing, then reopen from the Dock | The video stops while minimized and the channel starts again |
| Quit | ⌘Q while playing, reopen | Quits at once; accounts, My Lineup and settings are as they were |

## Build 141 — Live Events: live only when on air; tennis in the broadcast's order

The Apple TV report of Nov 5 (8:45 AM Pacific) had the World Wide Technology Championship in Live now while its three feeds said they start at noon, 12:40 and 1 PM Pacific ("Next | … | 20:00 (GMT)"; those times were read right). The tile was live because the tournament was: the schedule's round starts at 3 AM Pacific and the scoreboard said round 1 was in play in Los Cabos. Nothing you could tune showed it. A tile now says Live only when something it offers is on air: when every way to watch it has a known later start (a provider slot's own time, a guide listing's start), it is in Later Today (or its day) at the first of them, and turns live then. A game the scoreboard says is being played stays live (a feed's time is then the one that's off), except a golf round, whose coverage is a part of the day's play. A golf tournament's network channels with no guide listing don't count either way; any other way to watch with no known time keeps the tile live, as before. Across the whole report only one other tile was live against all its feeds' times, a WTA doubles watched at 6-4 4-4 that morning; it stays live (the scoreboard says it's in play), and its slot's time was wrong, below.

The tennis provider's slots ("Fils, Arthur vs Fritz, Taylor @ Nov 5 19:00 PM - ATP Paris :Tennis  18") give Central European time, not Eastern: the Paris night sessions at 19:00 and 20:30 are the schedule's 18:00 and 19:30 UTC, and all 19 of the day's slots were within an hour of their match's start read that way (six hours late read as Eastern). The report's start column, and the on-air rule, now read them so. Slots that are only a service tag (":Golf  02" … ":Golf  10", ":Tennis  40" …) are empty slots, no longer "Golf 02" tiles in Time not listed. The "group_starts" column ("2 of 2 started") counts the feeds of that provider group you played this week that started, not the event: it was right.

Tennis tiles listed the schedule's second player first, the reverse of every broadcast in the report (18 of 19 two-player slots). They now show the players as the feeds name them (most of the feeds that name both), else the draw's first-listed player first. Sets, flags, the tile's title, the hero, the details card, the in-player guide and the game banner follow. Other sports keep their order.

| Area | How to check | Pass |
|---|---|---|
| Golf before coverage | Live Events on a PGA Tour Thursday morning, round under way, feeds dated for the afternoon | The tournament is in Golf › Later Today at the first feed's time ("12:00 PM in 3 h"), not in Live now or the hero; its leaderboard may show |
| Golf on air | The same after the first feed's time (Live Events open, or reopened) | LIVE in Live now with "Round 1" |
| Golf on a channel | A tournament whose network's guide listing is on now | LIVE as before |
| Game in play | A game the scoreboard has live while a slot names a later time | Still LIVE |
| Tennis order | Tennis row, a match with a "Surname, First vs Surname, First" slot | The tile, its title and the details card list the players in the slot's order; the sets and flags beside each name are that player's |
| Tennis banner | Play that match | The banner and the guide's game list show the same order as the tile, and as the broadcast's scorebug |
| Tennis times | Help › Send a report on a tournament day | ":Tennis" slots' start column: a Paris 19:00 starts at 10 AM Pacific, not 4 PM |
| Empty slots | Report, GOLF PPV group | ":Golf  02" rows say "no event title in the slot's name"; no "Golf 02" tile |

## Build 141 — In-player guide: Live Events lists its sports first; doubles banner

Live Events picked in the in-player guide's lists was meant to open as My Lineup and Favorites do (Build 116): its SPORTS column first, the focus on it, the games following it behind. It didn't: picking it starts Live Events' board afresh, which shows placeholders (no sports) until a new board is made, so the column had nothing to open on and wasn't opened; and every new board after that (the first real one, then each score round) closed an open column. The viewer landed in the first sport's games. Now SPORTS waits for the loading board: the card widens at once with "LIVE EVENTS" and "Loading sports…" in the column, and the column opens on the first sports to come in (the sport of the game on screen, else the first), the focus there. A new board keeps an open column open on the same sports (found again by their row, not their position), and keeps the return rule's place; the lists over the column keep it for their Back. A key elsewhere while it loads (Left to the lists, Back, a click on the games or the list name) cancels the wait. The games rail's top line is a button for the pointer ("◀ LIVE EVENTS · NFL", opens SPORTS), as the guide's list name; with the pointer SPORTS' legend says what clicks and the wheel do; the lists' note says "Back: the games" when they didn't come from SPORTS. My Lineup, Favorites, Recent, All Channels and Search are unchanged.

The player's game banner for a tennis doubles match drew both pairs' flags (two 37 px flags 4 apart each side, 78 px a side) from x 96, the second pair from 182 to 260, and the title at the fixed x 236: over the second pair's flags. Now the title (and the league line under it) starts 12 px after the marks, as for singles: at 272 for doubles (254 with a pair on one side only). Singles (two 60 px flags, title at 236), team sports and races are unchanged. The board's tiles already put each name after its flags; the games rail, the detail card and Search draw badges of a fixed size, not flags.

| Area | How to check | Pass |
|---|---|---|
| Live Events from the lists | Watch a channel; Left until the lists; Live Events, OK (Live Events last opened over a minute ago) | The card widens with "LIVE EVENTS" and "Loading sports…", then SPORTS opens with the sports (Live now, Your teams, Soccer, NFL, …, "2 live" in red), the focus on the first, the red dot on it; the games of the focused sport dimmed behind. It stays open as the board fills and as scores come in |
| Walk | Up/Down in SPORTS | One sport per press (or swipe); the games behind follow; the top line names the sport; nothing tunes |
| Pick | OK or Right on a sport | Its games, the focus on its first; the column closes |
| Return rule | Open SPORTS from a game (Left), Down, Up, OK | Back on the same game, as it was |
| Back | In SPORTS opened from the lists, Back | The games as they were (the sport it opened on); Back again closes the guide |
| Lists | Left in SPORTS, then Back | The lists ("Back: sports"); Back returns to SPORTS on the same sport, also after a new board meanwhile |
| While loading | Pick Live Events, then Left (or Back) before the sports come | Left: the lists, and the sports coming in don't pull you out of them ("Back: the games"); Back: the guide closes |
| Watching a game | While watching a game: Left, Left, Live Events | SPORTS on that game's sport; Back puts the focus on the game playing |
| Same as My Lineup | Lists › My Lineup, Lists › Favorites | As in Build 116: their neighborhoods first |
| Pointer (LG) | Move the remote over the games rail; click the top line; hover and click a sport; turn the wheel; click "◀ Lists"; click a game | "◀ LIVE EVENTS · NFL" is a button that opens SPORTS; a soft highlight, the return rule; the wheel walks the sports; the lists; the column closes on that game. SPORTS' legend: "Click: its games · Wheel: walk · …" |
| Apple TV | Swipe in SPORTS | One sport per swipe |
| Doubles banner | Watch a tennis doubles match (two flags each side) | Both pairs' flags at the left, the title and the league line starting just after the second pair, never over a flag |
| Singles banner | A singles match; a team game; an F1 race | As in Build 140: title at the same place |
| Tiles | The doubles match on the Live Events board | Each pair's two flags, then the names; nothing overlaps |

## Build 141 — Live Events ready when opened

Opening Live Events showed placeholders (or "Getting this week's games…") on most opens, for up to half a minute on an Apple TV, and again on later opens. The Apple TV export of Nov 5 shows why. The app launched straight into the last channel, and the day's schedule waited for nothing to play full screen, so it hadn't been fetched when Live Events first opened. That open started a look-up of newly seen sports and the day's calendars ahead of the day's fetch and showed placeholders until its part for today was in (29 s). The look-up left the schedule yesterday's, so the next open started the day's fetch and waited again, as did every open until it was in: it moved on only while Live Events was open or nothing played full screen (4 min 47 s). An open more than 15 minutes after the channel list was read waited for the list again (4 s), and a board kept from before showed placeholders while it was made again once it was over a minute old.

Now the work starts at launch, in the background, on every TV (LG, Android TV and Fire TV, Apple TV, Mac: the same engine) and while a channel plays full screen too. A few seconds after the channels are in, the day's schedule is fetched, followed by any newly seen sports. The games are then matched and a board is made from them. While Live Events is closed they are looked at again every minute (matched again only when the list, the schedule or the listings changed, or at the half hour) and as soon as a new channel list is in, and the board is made again from the clock every 5 minutes. Scores get a first round as soon as the schedule is in, then one every 10 minutes at most while closed or while a channel plays over the board, never using more than half the day's requests (the rest stay for Live Events on screen). The board keeps its own pace while it is on screen. Opening Live Events now shows that board at once and refreshes it in place: the scores' round, the clock, a channel list read again (when the board's list was read in this run within 3 hours). Opening never starts the separate look-up ahead of the day's fetch any more. The matching and the board are made, and the scoreboards read, on a worker behind anything on screen. None of this runs while a key is pressed, a channel is starting or the channel list is being read or sorted, and the matching also waits while a guide is being built (one big job at a time on the LG). The guide's own builds still wait while you watch. The schedule is fetched no more often than before; the scores add at most a round every 10 minutes in the background, within half the day's cap; the request spacing, the daily caps and a refusing source's day off still apply. Nothing runs in the background with the game schedule off or on a TV that hasn't used Live Events in two weeks. There is no setting.

| Area | How to check | Pass |
|---|---|---|
| Launch into a channel | With Live Events used yesterday, launch the app (it starts the last channel) and keep watching 1 minute, then open Live Events | The board at once, with no placeholders and no "Getting this week's games…"; the log says "opened; the board is from … ago" with no "placeholders until new" |
| Playback | Watch the channel through the first minute after launch (developer log) | No stall or stutter you didn't see in Build 140; "schedule: the day's fetch in the background" within seconds of the channels |
| Later opens | Open and close Live Events several times over an hour while watching channels | The board at once every time; tiles change in place as scores come in |
| After watching a while | Watch one channel full screen for 40 minutes, then open Live Events | The board at once; if the channel list is read again, the board stays on screen with its small spinner and updates in place |
| Scores | A night with live games: launch, watch a channel 5 minutes, open Live Events | Live games show their clock and score within a few seconds of opening (finals at once); the log's scores requests are under 250 for the day while you mostly watch |
| First open of the day | Leave the app running overnight with a channel on, then open Live Events in the morning | Today's games at once (fetched in the background after midnight) |
| Woken TV | Turn the LG off for an hour on a channel, turn it on, open Live Events after 30 s | The board at once, or a few seconds of placeholders at most |
| LG | Launch on the LG straight into a channel and use the remote right away (developer log) | Keys answer as in Build 140; "uiMax" in the timing lines as before; memory after the first minute within 30 MB of Build 140's |
| Never used | A TV that hasn't opened Live Events in two weeks (or ever) | No schedule or score requests at launch; a first open loads as before |
| Schedule off | Settings › Live Events › Game schedule off, relaunch | No schedule or score requests |
| Report | Settings › Help & info › Send a report a minute after a list re-read, with Live Events closed | Renamed slots are already matched (no "not matched yet" unless the TV was busy) |

## Build 142 — Live Events: "Updating scores…" while the scores catch up

Since Build 141 Live Events shows its last board at once; its scores can then be a few minutes old (they're fetched in the background at most every 10 minutes) until the open's round of score requests is in, a few seconds later. Meanwhile the line at the top of the hero, after the competition (left of the clock), shows a spinner and "Updating scores…", as the guide shows "Filling in missing listings…". It goes as soon as the round is over and the tiles have their scores. It shows only when a game on the board is live or about to start (5 minutes before to 6 hours after its start, not final) and a league of such a game is about to be asked (not asked lately), with scores on in Settings. When the day's score requests are used up, "! Live scores paused until tomorrow" shows for 8 seconds after the open; when a game's score source is failing or refusing and none is being asked, "! Live scores unavailable right now" for 8 seconds. Placeholders keep their own line ("Getting live scores…").

| Area | How to check | Pass |
|---|---|---|
| Stale scores | While a game is live, watch a channel 15 minutes, then open Live Events | The board shows at once; "Updating scores…" with a spinner at the top for a few seconds; it goes when the scores change |
| Fresh scores | Close Live Events and open it again within a minute | No line (the scores are fresh) |
| No games now | Open Live Events when nothing is live or starting soon | No line |
| Scores off | Turn live scores off in Settings, open Live Events with games live | No line |
| On a game | Focus a live tile while the line shows | The line sits after the competition ("NFL · Week 10"), left of the clock; the competition isn't covered |
| Placeholders | First open of the day after a long sleep | The placeholders' own line as before, no second line |

## Build 143 — Live Events: team names, guide titles and clocks matched more often

The Apple TV export of Nov 5 had provider slots and guide titles naming a scheduled game that Live Events didn't recognise, so they were left out ("not on the schedule") or went to no tile. Now:

- **Clubs as other feeds write them.** A club's name without its initials or founding year counts ("Riga" for Riga FC, "Le Havre" for Le Havre AC, "New York City" for New York City FC, "Iberia" for Iberia 1999), and so does its nickname ("Thorns" for Portland Thorns FC), but only when no other team of that sport this week has that name. A hyphenated name run together counts ("AraratArmenia", "SintTruiden"); underscores are spaces ("New_York_City"); initials with stops count ("N.E.C."). Other countries' names: Crvena zvezda / Röda Stjärnan (Red Star Belgrade), Copenhagen / Köpenhamn / København, Köln (FC Cologne), Inter, Stade Brestois, Sporting KC, Iberia Tbilisi, Sky Italia's AEK Atene, Bayern Monaco, Magonza, and Ferencvárosi, Barça, Atleti. Liga MX clubs by the nicknames US Spanish-language TV uses: Chivas, Xolos, Rayados, Tuzos, Diablos Rojos, Gallos Blancos, Rojinegros, La Máquina.
- **Schools as guides write them.** "Saint" and "St." are the same ("Mount Saint Mary"), "Saint Peters" is Saint Peter's, a school without "University" or "College" ("Grace") or without its state in brackets ("Centenary", "Union") counts when no other school of that sport has the name, and the short-name table works both ways ("Mississippi" is Ole Miss, "Southern Mississippi", "Miami (Ohio)", "Texas-San Antonio"). The weaker names are checked against every team of the sport: on this week's games (2,925 teams in 23 schedules) none is shared by two teams that didn't already share a name.
- **Two schools' games an hour apart.** "Iowa State vs. UCF" at 6 PM (volleyball) and at 7 PM (women's soccer), with no sport in the title, was left out as ambiguous. Now the game at the slot's time goes (within 15 minutes, the other game 45 minutes or more away); at the very same time, on ESPN+ (which names the away team first), the game whose away team the title names first.
- **Any apostrophe.** "NCAA Men`s Basketball:" and "Women’s" read as College Basketball, not "Other".
- **European guide titles.** "Napoli - Lazio", "AEK Atene - Real Madrid" and Portuguese "Omonia FC x SL Benfica" on a sports channel (or in a sports slot) are read as a game when both sides are a scheduled game's teams; any other dashed title stays what it was. Competition titles ("Premier League", "LALIGA EA SPORTS", "Bundesliga", "Serie A", "UEFA Europa League", "Liga MX", "Fußball", "Calcio", "EFL", volleyball, softball) are sports titles whose description is read for the venue or the teams, and venues are read in German, Italian, French and Spanish ("Aus der … in …", "Dallo … di …", "Depuis le … à …", "Desde el … en …"). Previews, summaries and replays in those languages (Previa, Resumen, Repetición, Zusammenfassung, Sintesi, Résumé…) are left out like the English ones.
- **French feeds' clock.** "Montréal vs. Vancouver (French)" was read on Paris time (5:25 AM instead of 11:25 AM Pacific) and hidden as a replay: a language tag no longer sets a slot's clock; the channel's country does.
- **Brands as the provider writes them.** "SPORTSN ET", "EURO SPORT 1", "TNT SPORT 1", Ligue 1+, Canal+ Live / Premier League / Ligue 1, RMC Sport, Sky Calcio, M. Liga de Campeones and M. LaLiga, LaLigaTV Hypermotion, Fox Sports MX / ARG, Viaplay, Ziggo Sport, ESPN (Netherlands, New Zealand), Blue Sport, MySports, Sport1, Sportdigital, Eleven, TVP Sport and Polsat Sport are other countries' sports channels whose guides Live Events reads.
- **Tennis.** "Hongkong" is Hong Kong ("WTA: Kvartfinaler, Hongkong").

On the export (both builds run side by side): 1,539 → 1,595 slots matched to a scheduled game, "not on the schedule" 1,058 → 1,020 (Soccer 125 → 109), 34 more UEFA Europa / Conference League slots on their games' tiles; of the export's European guide titles, 112 → 135 of 140 name their game. Matching got faster: team names are read without the regular expression when plain ASCII (the team index 74 → 54 ms, a week's titles 121 → 75 ms on a desktop; the rebuild's longest 696 → 575 ms).

| Area | How to check | Pass |
|---|---|---|
| NYCFC | Report on an MLS weekend with Apple's "D.C. vs. New_York_City" slots | On the "D.C. United vs New York City FC" tile, English and Spanish |
| French feed | Same report, "Montréal vs. Vancouver (French)" | Starts at the game's time (11:25 AM Pacific), on the game's tile |
| UEFA | Thursday's Europa / Conference League slots (Viaplay, Disney+, Play+, Stan) | "Riga v Jablonec", "SintTruiden v Brann", "N.E.C. vs AC Omonia", "FC Köpenhamn - Iberia Tbilisi", "Röda Stjärnan - Inter Escaldes" on their games' tiles |
| Liga MX | A Liga MX weekend on Univision / TUDN | "Chivas vs. León", "Necaxa vs. Xolos" on the games' tiles |
| Two sports | ESPN+ "Iowa State vs. UCF" at 6 PM and 7 PM | The 6 PM slot on the volleyball tile, the 7 PM one on the soccer tile |
| Apostrophes | "NCAA Men`s Basketball: …" slots | Sport "College Basketball" in the report |
| Guide titles | A Sky Italia / DAZN channel showing "Napoli - Lazio" | The channel among the game's ways to watch; a "News - Weather" listing on it adds nothing |
| Schools | "Grace vs. Southern Utah", "Binghamton vs Saint Peters", "Southern Mississippi vs James Madison" | On their games' tiles |
| No new mix-ups | A busy college Saturday | No slot on another game of the same teams' names; two schools both called "Union" still need their state |
| Brands | Channel report, other countries' sports channels | "SPORTSN ET", "EURO SPORT", "TNT SPORT", "LIGUE 1+" channels read for Live Events |
| Tennis | "WTA: Kvartfinaler, Hongkong" slot | On that day's Hong Kong matches |
| Speed (LG) | Open Live Events on the LG with the full list (developer log) | "rebuildMax" no higher than Build 142's |

## Build 143 — Live Events: golf, racing, cricket and fight-night coverage

Event sports get their ways to watch from the guide and from more provider feeds.

- **Golf rounds.** A tournament's tile is the round on now, from its first tee time to the last group's finish (about 11 hours, read from the scoreboard's tee times; before they're out, 07:00 where it's played, from its name: Japan, Abu Dhabi, Mexico…, else 07:00 US Eastern for the US tours and Central Europe for the DP World Tour). It was 07:00 Eastern every day, wherever the tournament was. When a round's play is complete the tile moves to the next round ("Later", at its time) instead of ending; it ends after the final round. The scoreboard's tee times are Eastern clock times under a Pacific label (the groups at "14:44 PST" had played five holes by 21:19 UTC); they're read as Eastern. PGA Tour Champions and the Korn Ferry Tour are tours too.
- **Golf programmes on any channel.** "PGA Tour Golf", "LPGA Tour Golf", "Golf: Ladies PGA", "(Live) DP World Tour Golf", "LIV Golf", "PGA Tour Champions Golf", "Korn Ferry Tour Golf", a major by its name ("The Masters"), or "Golf" with the tournament in the subtitle: the tour's tournament in play then (the one the title names; with two of the tour's events on, the televised one), with the listing's own start and end, on Golf Channel, NBC, CBS, Sky Sports Golf or anywhere. "Golf Central", "Live From", highlights, classics, flashbacks and encores aren't the round. A listing the channel's guide doesn't mark live when it marks its live airings is a replay: not a way to watch.
- **Live only when on air** (Build 141) works with the real round: the tile is live while a feed or a listing shows the round, else "Later" at the next one's start.
- **Golf feeds** for a later day of the tournament ("World Wide Technology Championship: Day 2", "LPGA TOTO JAPAN CLASSIC" at 03:00 CET, "Toto Japan Classic vs 2. kierros") join the tournament's tile instead of being left out as unscheduled or "over".
- **Racing.** NASCAR's own race names ("NASCAR Cup Series at Miami") and its finale ("Championship Race") join the race; MotoGP, WEC and Supercars feeds join their sessions by the series' name and the session they name or their time ("MotoGP: Qatar GP vs Fri Training 1", "Race: 6 Hours of Monza WEC 2026"). The guide's programmes ("Live F1: Sao Paulo GP Qualifying", "F1: Rennen", "Live MotoGP: … - Sprint", "Live WEC: 6 Hours of Monza", "NASCAR Cup Series Racing") go to the sessions on during them, by the session words of several languages (Practice, Training, Libres, Essais, Qualifying, Qualifiche, Hyperpole, Sprint, Race, Rennen, Gara, Carrera, Course). Moto2, Moto3, F1 Academy and NASCAR's other series aren't the main class.
- **Cricket.** A Test's listing on its second or later day ("Bangladesh vs West Indies, 2nd Test, Day 2") is a way to watch it (it was only on the first two hours of the first day).
- **Fight nights.** "Live UFC Fight Night" (no fighters named) goes to that night's card, the prelims or the main card by its time.

Measured with the preview on the 5 November channel report (golf, motorsport, fighting and cricket picked; schedules fetched at 3:15 PM Pacific): golf feeds on the board with their tournament 2 → 10 and 12 more read as no known sport ("WORLD WIDE TECHNOLOGY CHAMPIONSHIP · DAY 1", "TOTO Japan Classic @ Nov 6 3:00 AM"); golf feeds left out 9 → 1. The World Wide Technology Championship's tile is LIVE, "ends ~4:44" PM with 13 feeds (was "Likely over, started 3:00" AM with 2); the TOTO Japan Classic's is round 2 at 4:00 PM with 9 feeds (its feeds were "over" or unscheduled); Abu Dhabi's round 2 is "Later" at 7:19 PM (was over). With the clock at 6:00 and 9:00 AM the World Wide Technology Championship is "Later today, 12:00 PM" (its feeds start then), at 1:00 PM LIVE. The MotoGP practice feed joins Qatar GP; the six WEC feeds join their sessions (the weekend shows once its session on has a way to watch). The guide listings were checked with made-up listings for the week's events (the report has no guide).

| Area | How to check | Pass |
|---|---|---|
| Golf round times | Open Live Events during a tournament in Japan or the Middle East | The tile's round starts around 07:00–09:00 local time there, not 07:00 Eastern |
| Between rounds | Open Live Events after a round's play is complete | The tournament is "Later" at the next round's time, with its leaders; not "Ended" |
| Final round | After the last round | The tile ends ("Final") |
| Golf listings | With Golf Channel's guide showing "PGA Tour Golf" | Golf Channel is a way to watch the tournament, from the listing's start; the tile is live only while it (or a feed) is on |
| Golf shows | "Golf Central", "PGA Tour Highlights" in the guide | Not a way to watch the tournament |
| Racing listings | Sky Sports F1's "Live F1: … Qualifying" | On the qualifying session's tile; a replay after the session isn't |
| NASCAR | A provider feed "NASCAR Cup Series at …" | On the race's tile, not its own |
| MotoGP / WEC feeds | "MotoGP: Qatar GP Free Practice 1", "WEC: 6 Hours of Monza Free Practice 1" | On that session's tile |
| Moto2 | A Moto2 feed or listing | Not on the MotoGP tile |
| Test cricket | A Test's "Day 2" listing | On the match's tile all day |
| Fight night | "Live UFC Fight Night" in the guide | On the card's main card tile (the prelims listing on the prelims) |

## Build 143 — Live Events: every network the schedule names; your team, Spanish and local channels

A tile lists your channels airing the game's networks. Many broadcaster names the schedule writes weren't read, so those games had no channel. Now one table (built once, looked up by letters and digits only) knows them: "Golf Chnl", NBCSN (a core network again), ION, the NBC Sports regionals as the schedule shortens them ("NBC Sports Phil", "NBC Sports BA +", "NBCSCA+", "NBC Sports BO"), MSG's networks apart (MSG Network: Knicks, Rangers; MSG Sportsnet, formerly MSG+: Devils, Islanders; MSGB: Sabres; MSG2/MS2), "Marquee Sports Net", MASN, SNY, YES (also "FOX SPORTS YES HD"), SportsTime Ohio, ABTV, Midco Sports Net, the FanDuel abbreviations ("FanDuel SN IN / WI / WIX / OK / DET / DET Ext / OH / MW"), and in Spanish ESPN Deportes, Universo, "Tele" (Telemundo), "UniMás", Fox Deportes, TUDN, Galavisión and beIN en Español. Local stations by their brand ("Utah 16" KUPX, "The Spot - MTN" WXPX, "Vegas 34" KMCC, CW33, PeachtreeTV, Atlanta News First, "CW PIX 11" WPIX, "Fox 5 New York") or by a call sign anywhere in the name ("KTVD-TV (My20)", "KUSA-TV (9NEWS)", "ALT2/KTVD", "KCTV5", "Fox 5 WTTG", "KFMB 8.1 (CBS)", "WTHR Channel 13"). A few names are a station only with the team: "Fox4" (Stars: KDFW, Chiefs: WDAF), "My27" (Stars: KDFI), "NBC 10" (Phillies: WCAU), "Scripps Sports" (Golden Knights: KMCC). Streaming services and team apps (ESPN+, Peacock, Prime Video, "Cardinals.TV", "Thunder App", B1G+, ACCNX…) are streaming, not unknown.

Fixed wrong matches: "FanDuel Sports Network South West" and "FOX SPORTS MID WEST" were read as West; West (Kings, Angels) and SoCal / Prime Ticket (Clippers, Ducks) were one network; "MSG SPORTSNET" and "MSG PLUS" were filed as MSG Network; "KING OF THE HILL" and "HONG KONG FIGHT CLUB" were read as the stations KING and KONG (the Kraken's tiles showed the show's name): a call sign that is also a word needs a network word or a channel number with it. A pro team's channel is never a college side's of the same place ("MLB SAN FRANCISCO GIANTS" on a San Francisco Dons volleyball game, "BALLY SPORTS ARIZONA" on a college soccer game).

Your channels: NHL and WNBA team channels ("US| NHL TEAM PPV", "WNBA TEAM PPV", the "AT&T: <TEAM>" channels) are sports channels now, so they're on their teams' games. Spanish stations (Telemundo, Univision, UniMás) are found by network and call sign like local ones, and the US channels in Spanish are read for Live Events whether or not that list is picked, ranked after English ones. TNT, TBS, truTV and USA Network's guide listings confirm games too. Big Ten Network's alternates and overflows back it up: when several Big Ten games are on at once, each tile lists Big Ten Network and its alternates as likely ("Several games on this network at once") and the guide listings say which. Event slots named after a network ("NCAAF 01: ESPN", "NCAAF 04: ABC") are a backup of it for that league's games.

Reports: the Live Events report ends with the broadcaster names not recognised this week (group "Broadcaster names not recognised": the name, its leagues, how many games; streaming left out). The channel report has a last column, "next_titles": a sports channel's next three programmes ("19:00 NHL Hockey | 21:30 Postgame | 22:00 Highlights").

| Area | How to check | Pass |
|---|---|---|
| NHL team channels | Live Events, NHL row, a game of a team you have a channel of ("US: BOSTON BRUINS", "AT&T: BOSTON BRUINS") | The team's channel is under Your channels ("Team channel") |
| NBA / MLB team channels | Same for an NBA game ("AT&T: ATLANTA HAWKS", "NBA: ATLANTA HAWKS") | The team's channel, as before or now |
| Regional networks | A Flyers, Devils or Islanders, Sharks game | NBC Sports Philadelphia / MSG Sportsnet / NBC Sports Bay Area under Your channels |
| Local stations | A Mammoth game ("Utah 16"), a Lightning game ("The Spot - MTN") | The station (KUPX, WXPX) under Your channels |
| Kraken | A Kraken game on KONG | The station shows as KONG, never "HONG KONG FIGHT CLUB"; KING never shows as "KING OF THE HILL" |
| Kings / Clippers | A Kings game on FanDuel SN West, a Clippers game on FanDuel SN SoCal | The West channel (FOX SPORTS WEST / Prime Sports West) for the Kings; Prime Ticket / SoCal for the Clippers |
| Spanish | A Premier League game on Universo, a LaLiga game on ESPN Deportes | NBC Universo / ESPN Deportes under "In Spanish", after the English ones |
| Big Ten | A Saturday with several Big Ten games at once | Each tile: Big Ten Network and its alternates as likely; a guide listing makes one certain |
| College soccer | A college soccer game (Arizona, Pittsburgh, Houston) | No MLB / NHL team channel, no FanDuel channel of that place |
| Unknown names | Send a Live Events report | Last rows "Broadcaster names not recognised" with names, leagues and game counts; no ESPN+, Peacock or team apps |
| Next titles | Send a channel report | "next_titles" filled for sports channels (three titles with times), empty for others |

## Build 144 — Settings pages open in place; provider membership dates

One rule now: panels float only over the picture (in the player: Sources, Aa, Quick settings). Everything in Settings opens in Settings' own middle column.

- **Pages in place.** A provider, Edit My Lineup, Channel by channel, Groups in All Channels, Guide listings, Home city, Hidden, Send a report to your phone and Status open in the middle column, one level deeper. The categories stay on the left, dimmed, with the current one still marked; the line over the title says where you are ("SETTINGS › PROVIDERS › BACKUP TV"); the right column explains the focused row. Back (or Left, off a row of choices) goes up one level and lands on the row you came from; a page opened from a page works the same way. The pointer, clicks and the wheel work as on the category rows; a click on a dimmed category goes to that category.
- **From outside Settings.** Quick settings' Edit My Lineup (in the guide) and a provider picked in the providers list outside Settings open Settings at that page; Back from it (or removing the provider) goes back where you were.
- **Asking on the row.** OK on Remove this provider or Clear playback log turns that row red: "Remove Backup TV?" / "Clear the playback log?" over "OK to remove · Back to keep it"; the right column says what happens and the key hints say "OK Remove · Back Keep it". OK again does it; Back, or moving to another row, keeps everything. No pop-up in Settings.
- **Reports beside the list.** OK on a report builds it ("Building the report…" with a spinner) and shares it: its code, link, time left and "Only devices on your home network can open it." in the right column, its row saying "Sharing now · 9:41 left". OK again stops; OK on another report shares that one instead. Leaving the page stops sharing, as the card did.
- **Real tasks stay full screen** (Add a provider, Add from your phone, the lineup steps, My Sports, Diagnostics, Getting around); Back or finishing returns to the row they were opened from, even when a provider was added above it.
- **Membership dates.** An Xtream provider's end date (and for an M3U playlist whose link is an Xtream panel's own, the same panel's, asked with the same login) is read at sign-in and with every read of the channel list (each launch, then every half hour while games matter, else every two hours, and Reload), so a renewal shows by itself (until now the date was only saved at sign-in). No date (or 0) shows nothing; Emby / Jellyfin has none. Settings › Providers: "Xtream · Signed in · Ends Mar 3, 2027"; in the last 7 days, in amber, "Ends in 5 days · Nov 10", "Ends tomorrow · Nov 10", "Ends today"; once it has ended "Ended Nov 10 · renew it with your provider" instead of "Couldn't sign in". A trial says "Trial · ends …". The right column: "Membership ends March 3, 2027 — 118 days left. Up to 2 streams at once." The provider's page: its kind and sign-in, the membership ("Membership ends in 5 days" in amber over the date and "Renew it with your provider to keep its channels."), "Streams at once", then Remove.
- **The guide's heads-up.** In the last 7 days and after the end, the guide's status line says "! Backup TV ends in 5 days" (tomorrow, today, "ended Nov 10") for 8 seconds the first time the guide shows each day, once per provider per day (kept across launches). Loading and updating lines keep their place; it waits its turn. Nothing pops up.

Days are counted on the TV's own calendar and time zone. Never a link, login or channel count on screen or in the log.

| Area | How to check | Pass |
|---|---|---|
| Provider page | Settings › Providers › a provider | Opens in the middle column; categories dimmed with Providers marked; "SETTINGS › PROVIDERS › <NAME>" over its name |
| Back | Back on that page | Providers' rows, the provider's row focused; Back again: the categories |
| Edit My Lineup | Settings › My Lineup › Edit My Lineup | In place; right column "You added it" / "You removed it" / "In My Lineup" for the focused channel |
| Nested pages | Help & info › Status, Guide › Guide listings | In place, live state updating; Back returns to the row |
| Pointer | LG pointer over a page's rows, wheel, click | Rows highlight, wheel scrolls the page, click acts; a click on a dimmed category goes there |
| Remove | Provider page › Remove this provider › OK | Row turns red "Remove <name>? · OK to remove · Back to keep it"; keys "OK Remove · Back Keep it" |
| Cancel | Then Back, or Up | Row back to normal, provider kept |
| Confirm | OK on the red row | Provider removed, back on Providers, "Provider removed" |
| Clear log | Help & info › Send a report › Clear playback log | Same red row "Clear the playback log?"; OK clears |
| Reports | OK on Channel report | "Building the report…" spinner, then the code, link and time left on the right; row "Sharing now · 9:59 left" |
| Next report | ▼ then OK on Playback log | The playback log's code replaces the channel report's |
| Stop | OK on the shared report | Sharing stops; the row's own line again |
| Tasks | Providers › Add a provider, then Back | Back on "Add a provider" |
| Quick settings | Guide › Quick settings › Edit My Lineup | Settings at Edit My Lineup; Back returns to the guide |
| First-run list | Providers list outside Settings › a provider | Settings at its page; Back returns to the list |
| Player panels | Sources, Aa, Quick settings in the player | Unchanged side panels over the picture |
| Date far off | Xtream account ending in months | "Ends Mar 3, 2027" on its row; right column "Membership ends …", days left, streams at once |
| Last week | Account ending in 5 days | Amber "Ends in 5 days · Nov 10"; page "Membership ends in 5 days" in amber with the full date |
| Ended | Account past its date that can't sign in | "Ended Nov 10 · renew it with your provider", not "Couldn't sign in"; page "Can't sign in: the membership has ended" |
| Unlimited | Account with exp_date 0 / null, Emby / Jellyfin | No date anywhere |
| M3U from a panel | M3U link to a panel's `get.php` with the login in it | Its end date shows like an Xtream account's |
| Renewal | Renew with the provider, wait for the next channel list read (or Reload) | The new date by itself |
| Heads-up | Open the guide with an account in its last week | "! <name> ends in 5 days" in the status line for ~8 s; not again that day, even after a relaunch; again the next day |
| Priority | Open the guide while "Updating your channel list…" | The update line stays; the heads-up follows after it |

## Build 145 — Marigold 1.0: the release, and a quieter playback log

- **1.0.** The version is 1.0.0 (the app's Help & info and the playback log's launch line; on LG and Android the package is 1.0.<build>). This version's first green build on master publishes release v1.0.0 with the four apps as direct downloads (docs/releases.md); later builds of 1.0 don't.
- **Playback log.** A stall is logged once it lasts long enough to show "Buffering…" (3 s), and its "recovered by itself" with it. The players' sub-second buffering blips, which nobody sees, are no longer logged: on the Nov 6 Apple TV export they were 1,780 pairs, 89% of the log, so it reached back only ~18 hours. Reconnects, failed starts and the rest are logged as before.

| Area | How to check | Pass |
|---|---|---|
| Version | Help & info | Version 1.0.0 (build N) |
| Release | The release page of v1.0.0 | marigold-lg-webos.ipk, marigold-android.apk, marigold-apple-tv.ipa, marigold-mac.dmg; each link downloads the file directly |
| Latest link | `.../releases/latest/download/marigold-android.apk` | The 1.0 APK |
| Short blips | Watch an hour of a stream that hiccups | No "stall" / "recovered by itself" lines for blips |
| Real stall | Pull the network for 5 s, then restore | "stall" then "recovered by itself 5.0s after stall"; "Buffering…" showed |
| Long stall | Pull it for 15 s | "stall", then "reconnect … not recovered after 10s" |

## Build 145 — Live Events: games with no time yet, "start:" slot times, "Surname-Surname" tennis slots

The Nov 6 export had provider slots left out or on the wrong tile for three reasons. Only which slots and listings go to which tile changes: every tile's time, row, day, order and title stay as they were.

- **Games ESPN hasn't timed.** ESPN lists a game whose time isn't set with "timeValid": false and a placeholder date of 05:00 UTC, midnight Eastern of the day it's played (15 of Nov 14's top college football games, Texas at LSU on ABC among them; the Paris semifinals; the Hong Kong and Chennai finals: the same 05:00 for games abroad, on their local date). The app took the placeholder for the start, so a slot at the real time ("Shelton, Ben vs Van Assche, Luca @ Nov 7 15:00 PM - ATP Paris") was hidden as "a replay: its game is on another day". Now such a game (team sports, college and tennis matches) counts as any time on its day: a dated slot on that Eastern day is it; one outside it is measured from the day (a match in Hong Kong at 11:00 is 10 PM the evening before in New York, within tennis's 8 hours), so the next day's slot is still another day's. A day-less slot ("Missouri at Georgia 3:30PM") finds it on its day when no timed game of the two is at that clock; its placeholder never teaches a group a clock offset, never makes it "before today", and guide listings and tennis coverage on its day count. The tile still shows the game as listed. Once ESPN sets the time, everything is as before.
- **"start:… stop:…" times are UTC.** "Brewers x Rays start:2026-10-28 00:00:00" is the 8 PM ET World Series game; "UFC FIGHT NIGHT … PRELIMS start:2026-10-31 21:55:00" comes before ESPN's first bout at 21:00 UTC (read as Eastern, the prelims started after the main card); "Players Championship 34 | Leicester … start:2026-11-05 13:00:00" is the 1 PM UK floor event. All such slots in both exports (MLB, NBA, WNBA, UFC, darts, Triller) fit UTC and not Eastern. They were read as Eastern (US groups) or UK time (UK groups, an hour off in summer); now UTC in every group. "(2026-11-06 18:00:00)" beside "6:00PM ET" stays Eastern, Stan's "(…)" stays Sydney, "@ 2026-11-06 14:00:00 :Tv2Play DK" stays the service's country.
- **"Shelton-Paul".** "ATP: Shelton-Paul, kvartfinale, Paris @ 2026-11-06 14:00:00 :Tv2Play DK  01" went to every Paris match that day (first on the list: the doubles quarterfinal "Cash / Nakashima vs Harrison / Skupski"). A hyphenated pair is now the one match whose two players have those surnames, either order; anything else stays whole: "Auger-Aliassime", "Melichar-Martinez", "Jan-Lennard", "Herbert P-H", three names joined, "Center Court-Paris", two players of different matches. Tennis is still one tile per match.

On the Nov 6 export (both builds side by side): the Shelton-Paul slot leaves the 7 other Paris tiles (Fritz vs Alcaraz, Ruud vs Borges, Assche vs Hijikata and four doubles) and stays on Shelton vs Paul; no other slot or tile changed (4,067 tiles, same titles, rows, status and order). The ten "start:" slots now read 4 to 5 hours earlier in the US groups (all long over on Nov 6). With Nov 14's untimed college games (slots renamed, the clock set to the evening before), "Texas Longhorns vs. LSU Tigers (2026-11-14 19:30:00)" and "Florida Gators vs. Kentucky Wildcats (… 12:00:00)" go to their games instead of "a replay"; the same slot a day later, and a timed game's, stay replays.

| Area | How to check | Pass |
|---|---|---|
| Untimed game | A slot dated on the day of a game ESPN shows with no time (a college game a week out, a tennis semifinal or final) | On the game's tile; the tile's time and place as before |
| Next day | The same two teams' slot dated the day after | Hidden: "a replay: its game is on another day" |
| Abroad | A Far East final with no time, its slot the evening before in the US | On the match's tile |
| Timed later | After ESPN sets the time | A slot more than 3 h (tennis 8 h) from it is a replay again |
| Clock offset | A group whose slots are mostly untimed games | No "+Nh" offset in the developer log for it |
| start: | "Brewers x Rays start:2026-10-28 00:00:00" in a US group (Live Events report, "starts") | 5:00 PM Pacific Oct 27 (was 9:00 PM) |
| start: UK | A UK group's "start:" slot in summer time | The UTC time, not an hour earlier |
| Others | ESPN+ "(… 18:00:00)" with "6:00PM ET", Stan "(…)", Tv2Play "@ …" | Unchanged |
| Shelton-Paul | "ATP: Shelton-Paul, kvartfinale, Paris" | Only on the Shelton vs Paul tile |
| Whole names | "Auger-Aliassime", "Melichar-Martinez" slots, "Center Court-Paris-ATP" | As before: the day's matches |
| Speed (LG) | Open Live Events with the full list (developer log) | "rebuildMax" no higher than Build 144's |

## Build 145 — Live Events: the scores you're looking at come first; race podiums after the race

**Scores in the player's Live Events column.** With a channel playing, Live Events picked in the player (the games rail and its SPORTS column) now keeps the live scores at full pace while it is open, as the board does with nothing playing: until now it counted as watching and got the background's pace (a look at most every 10 minutes, half the day's requests). Closing it goes back to the background's pace at once, so the network stays free for the picture. Watching with nothing open over the picture is unchanged. Still never a score in the player's banner.

**What you're looking at goes first.** Every league with a game on used to share one pace: on a busy afternoon (17 leagues on at once) a league was asked every 8 or 9 minutes. Now the leagues of the sport the focus is on (on the board, the row; in the player, the sport shown, Tennis is ATP and WTA), of the game playing under the column, and of your teams' games on now are asked at their own pace (once a minute while live with scores shown), and the other leagues share the pace among themselves. After 120 requests in an hour the others slow down first (each at least every half hour), and what you're looking at only as much as the hour needs (tennis: every 80 s). The day's caps don't change (500 requests a day, 250 of them in the background). Rows of many sports (Live now, Your teams, More events) aren't one sport looked at. The rows and tiles, their times and their sources don't change.

| Afternoon, 17 leagues, tennis live | Tennis asked every (median) | Requests 1st / 2nd / 3rd hour | Day's 500 used up |
|---|---|---|---|
| Board open, Tennis selected — before | 511 s | 116 / 123 / 113 | after 4.4 h |
| Board open, Tennis selected — now | 62 s, then 82 s past the hourly pace | 177 / 131 / 128 | after 3.5 h |
| Player's column open over a channel — before | 617 s (background) | 96 / 96 / 58 | never (stops at 250) |
| Player's column open over a channel — now | 62 s, then 82 s | 177 / 131 / 128 | after 3.5 h |
| Playing, nothing open — before and now | 617 s (background) | 96 / 96 / 58 | never (stops at 250) |

**Race podiums after the race.** A Formula 1 weekend's tile shows the top three of the Grand Prix itself, once the race is over; before that, no drivers (not after practice, qualifying or the sprint). The results of practice, qualifying and the sprint aren't fetched any more (the drivers list for the onboard cameras still is). NASCAR and IndyCar show their top three once the race is final, never while it runs, and never a qualifying's. Everything else on these tiles (title, track, the FP1…Race timeline, status, "Onboard cameras", the sources) is as before.

| Area | How to check | Pass |
|---|---|---|
| Player column, full pace | Play a game, Left to open Live Events in the player, leave it open on a live game's sport for 3 minutes | Its clock / score on the rail moves about once a minute |
| Closed | Close the column, keep watching a few minutes, open it again | Picture unaffected; on opening the scores catch up at once, then once a minute again |
| Board, sport first | Busy afternoon, Live Events, focus on the Tennis row for 3 minutes | Tennis matches' sets update about every minute; other rows still update |
| Many sports | Focus on Live now | Every league at the shared pace, as before |
| Your teams | A followed team's game live, Live Events open on another row | That game's score keeps a one-minute pace |
| Day's cap | Leave the board open all afternoon | "Live scores paused until tomorrow" no sooner than 500 requests; the background stops at 250 |
| F1 before the race | F1 weekend tile after practice, sprint qualifying, sprint or qualifying | No drivers on the tile; title, track, FP1…Race, status, "Onboard cameras", sources as before |
| F1 after the race | Grand Prix finished (results come about 10 minutes after the end) | The race's top three, "Race" under them |
| NASCAR / IndyCar | Race live, then final | No drivers while live; the top three once final |
| Banner | Watch any game | No score in the player's banner |

## Build 147 — 1.0.1: untouched Live Events slows its scores

Live Events on screen (the board, or the player's Live Events column) with nothing touched for 5 minutes (no key, pointer move, wheel or swipe) asks for scores once every 20 minutes instead of at its usual pace, and within the background's half of the day's requests. Nothing on screen changes: the scores stay, no note. The first touch brings fresh scores within a couple of seconds and the usual pace back. Watching a channel with nothing open is unchanged (the background's pace). The release links pick up 1.0.1 by themselves (a new version makes a release).

| Area | How to check | Pass |
|---|---|---|
| Untouched | Live Events open on a busy afternoon, remote down for 30 min | Scores change at most about every 20 minutes after the first 5 |
| Touch | Then press any key (or move the LG pointer) | Scores fresh within a few seconds, then updating at the usual pace |
| Player column | The same with Live Events open over a channel | Same |
| Watching | A channel full screen, nothing open | As before |
| Release | `.../releases/latest/download/marigold-android.apk` | 1.0.1 |

## Build 148 — Each picked area is its own neighborhood

With stations of two or more of your picked areas in My Lineup, each area is a neighborhood of its own in place of Local, named by its short name (the part before the dash, as Settings shows it: "San Francisco–Oakland–San Jose" is "San Francisco"). The home area comes first (Live Events › Home city; the first one picked unless you chose another), then the others in the order you picked them, each in channel-number order. A station in two of your areas goes with home when home is one of them, else with the one picked first. Local stations in none of your areas (added by hand, or another city's in All Channels) stay under Local, after the cities. With one area (or a second whose stations you've all removed) the guide is exactly as before.

Everything that names a neighborhood picks the city up: the grid's headers, the label above the preview, the NEIGHBORHOODS column (Left at Now, then Down to the next city), the player's guide (its top line and headers), Filter › Neighborhoods, Edit My Lineup's second line and the "My Lineup is ready" tiles. Changing the home city re-sorts the guide. A number two cities share (4, 13) tunes the home city's station. Live Events › Home city now says "Its stations come first, in the guide and on games". The player's guide writes its headers in capitals like the guide ("LOCAL", "NEW YORK"). A neighborhood picked in Filter that goes away (Local splitting into cities, or back) clears the filter.

| Area | How to check | Pass |
|---|---|---|
| One area | My Lineup with one area (New York), a station of another city added by hand | One LOCAL header, rows in number order, as before |
| Two areas | Pick New York and Indianapolis, keep some of each | NEW YORK then INDIANAPOLIS headers in place of LOCAL, each city's rows together in number order; no counts anywhere |
| Unpicked local | Also add a third city's station by hand | It sits under LOCAL, after the cities |
| Column | Left at Now | New York, Indianapolis, (Local), News…; Down then OK lands on Indianapolis' first station |
| Hero / filter / setup | Focus an Indianapolis row; open Filter; Settings › My Lineup › Edit My Lineup | Label above the preview says Indianapolis; Filter › NEIGHBORHOODS lists the cities; Edit My Lineup's second line names each station's city |
| Home city | Live Events › Home city › Indianapolis | Indianapolis comes first in the guide and the column; the row's description reads "Its stations come first, in the guide and on games" |
| Number keys | Type 4 with WNBC 4 and WTTV 4 in My Lineup | Home's station (WNBC with New York home, WTTV with Indianapolis home) |
| Player's guide | Open the guide over a channel | Top line "MY LINEUP · NEW YORK"; headers in capitals, the cities as in the guide |
| Back to one | Remove every Indianapolis station | LOCAL again, as before |

## Build 148 — Updates on Fire TV and Mac

Where Marigold can install itself (Android and Fire TV, the Mac), it looks for a newer release: once within a minute or so of opening (from 20 seconds on, when nothing is pressed or starting and the first channel list is in), then at most once a day, from GitHub's latest release, without signing in and with the device's ordinary agent. A newer plain version (`x.y.z` above the running one; 1.0.10 is above 1.0.9) with this device's file and its SHA-256 digest counts; prereleases and other tags don't. Nothing says what changed: a small card at the top right says "Update available" with **Update** and **Not now** (Left/Right; OK; Back is Not now). It comes up only on the guide or Live Events, with nothing full screen, after 2 seconds without a key, at most once a day (Not now, or 20 seconds untouched, waits for tomorrow), never in the run that began with the first-time setup, and never over a reminder card. An OK in its first second is ignored (it was meant for the guide). While an update waits, Settings › Help & info starts with an "Update available" row (OK updates). Updating: "Updating…" with a spinner on the card or the row; the file goes to the app's cache and must match the release's digest (no digest, or another file: refused). Android hands it to the system's installer (the "Install unknown apps" page for Marigold opens first where the system asks; the installer follows when you come back with it allowed). The Mac copies the new app next to the old one, quits, and a small helper puts it in the old one's place and opens it. Anything that goes wrong: "Couldn't update. Try again later." for a few seconds (the Mac, in a folder it may not change: "Couldn't update. Marigold's folder can't be changed.", or run from Downloads: "Couldn't update. Move Marigold to Applications first."). LG and Apple TV: nothing at all — no check, no request, no card, no row.

The updater ships in this build, so a device takes its first in-app update from the next version on (this one is installed the usual way). An Android update installs only over an APK signed with the same key (CI's release key if set, else its cached debug key); one signed otherwise is refused before the installer opens ("Couldn't update").

| Area | How to check | Pass |
|---|---|---|
| Check | Fire TV with an older version installed than the latest release; open, wait on the guide | Card "Update available" within a minute or so; no version or notes on it |
| Once a day | Not now; close and open the app again the same day | No card; Help & info has the "Update available" row |
| Next day | Open the app the next day | The card once more |
| Where | Open Search or Settings, or play full screen, when the card would come | No card there; it waits for the guide or Live Events |
| Setup | First-time setup on a fresh install with an update out | No card in that run |
| Update (Fire TV) | Card › Update | "Updating…" with a spinner, then the system's installer (after "Install unknown apps" for Marigold the first time); after installing, the new version in Help & info › Version |
| Update (Mac) | Card › Update, Marigold in Applications | "Updating…", the app quits and opens again as the new version, in the same place |
| Mac folder | Marigold run from a folder you can't change | "Couldn't update. Marigold's folder can't be changed." for a few seconds |
| Row | Settings › Help & info › Update available › OK | Same as the card's Update |
| Up to date | The installed version is the latest | No card, no row |
| LG, Apple TV | Any time | No card, no row, no request to GitHub (the log has no "update" lines) |
| Log | marigold.log (Android: also logcat; Mac: Console) | "update" lines with versions and status codes, never a link or a path |

## Build 149 — 1.0.3: favourites can be removed again

Favourites stuck: once a channel had been a favourite through a lineup rebuild (every launch, a channel list read, a My Lineup edit), it kept its star after "Remove from Favorites" until the next rebuild. My Lineup's build takes the viewer's favourites in (a favourite always joins My Lineup) and marked them on the lineup's entries as if the server (Emby / Jellyfin) had; the rows took that mark as a favourite of their own. Now the entries carry the server's flag only; the viewer's favourites still bring channels into My Lineup and All Channels. Removing a server favourite here removes it (and tells the server), instead of turning it into a local one. And the menus (hold OK in the guide, Search's channel menu, the player's ★) read the favourite from the store at once: they used to read the rows published on the next tick, so right after "Add to Favorites" the row still said "Add to Favorites", and a second OK quietly took it off again.

| Area | How to check | Pass |
|---|---|---|
| Many | Hold OK › Add to Favorites on five channels | Five stars; the Favorites list has five |
| Remove | Hold OK › Remove from Favorites on one | Star gone at once; Favorites has four |
| After relaunch | Relaunch, then remove another | Gone at once; stays gone after the next relaunch |
| Player | ★ pill on a favourite in the player | "Removed from Favorites"; the guide agrees |
| Emby / Jellyfin | Remove a server favourite | Gone here and on the server |
| Favorites screen | Favourite channels in My Lineup and outside it, one at a time | Each shows in Favorites (guide tab and the player's list) right away, and after a relaunch |
| Renamed row | A favourite on a channel whose row id changes (new matching rules in an update, a renamed channel) | Still in Favorites under its new id; recents follow too |
| Menu flips | Hold OK › Add to Favorites, keep the menu open | The row reads "Remove from Favorites" at once (a second OK no longer undoes the first unseen); Search's Favorites switch the same |

## Build 152: second provider countries, West feeds, VIX tile

A second provider that writes its countries in bars ("|FR| …", "|US| …") was read as having no country at all. Its French, German and other foreign copies joined the US rows (Discovery had 30 copies, Showtime held Canada's Showcase), skipped Live Events' language check, and took the room for the first provider's sports groups. Now "|XX|" reads like "XX|" ("|SW|" is Switzerland, "|AUS|" Australia, "|PPV|" no country). West feeds named with a marker after the word ("TBS WEST ᴿᴬᵂ") are West feeds again. A movie channel called "… Multiplex" is no longer a live soccer conference tile. A row whose name is a provider's ("BLOOMBERG TV+") keeps its curated logo. How well copies start is kept per provider. The playback log's first "tune" line no longer names the previous channel's version, and "gave up" is logged once.

| Area | How to check | Pass |
|---|---|---|
| Foreign copies | Two providers, one writing "|FR|" | Discovery Channel's Sources lists US copies only; the French ones sit under International |
| Live Events | Both providers on | The first provider's sports and event groups are back (soccer, motorsport, golf rows) |
| West | West coast; TBS West, HBO West | Each has the West copies of both providers |
| VIX | Live Events with the second provider | No "VIX Golden Multiplex" tile |
| Bloomberg | My Lineup › Bloomberg | Its logo |
| Log | Play a channel, then another | The second channel's first "tune" line has no version; one "gave up" line when a channel fails |

## Build 153: the channel list is the backbone

The viewer's channel list (paid national and regional channels and US stations, built into the app as the lineup reference) now decides what a US channel is. It's no longer only a reference beside the provider's group names.
- **Not on the list, and nothing calls it free streaming:** it's Other. Other is out of the guide, All Channels and Search unless Settings › Guide › Other channels is on, and it's never suggested for My Lineup (a channel the viewer added or made a favourite stays).
- **Free streaming:** a channel the list doesn't know is Free only when something says so: a copy in a free service's group, our free streaming names, or the channel directory's free tier. Before, a channel nothing knew counted as free.
- **Overflow and alternate feeds:** "Tennis Channel 3", "TUDN Zona", "Yes Network Latin" are rows of their own, named after the channel and filed with it.

The list itself gains:
- a stable ID per row (the ids the app already used);
- MSG Network and MSG Sportsnet as two channels;
- overflow feeds: NESN+, NBC Sports Philadelphia+, SportsNet Pittsburgh+, CHSN+, Altitude, Monumental, MLB Network and NHL Network alternates, TMC Xtra;
- DOGTV, QVC2, QVC3, getTV, Retro TV, 365BLK, Outlaw and TVP World;
- other names providers use: "Willow Cricket", "TVG", "Home Shopping Network", "SportsNet LA Dodgers", "Sho x Bet".

BlazeTV, Real America's Voice and Salem News Channel (streaming-only or free) are off it. tools/reference/pack.py rebuilds the data from the reviewed list.

| Area | How to check | Pass |
|---|---|---|
| Other hidden | Guide, All Channels, Search for a provider channel the list doesn't know | Not there |
| Other shown | Settings › Guide › Other channels on | An Other section at the end of All Channels; Search finds them |
| Never suggested | Rebuild My Lineup with Other on | No Other channels in My Lineup |
| Kept | A channel added to My Lineup or made a favourite before, now Other | Still in My Lineup |
| Free | Bob Ross Channel, Nosey, FailArmy | Under Free Streaming, not Entertainment |
| MSG | New York area | MSG Network and MSG Sportsnet are two rows |
| Overflow | A provider's "Tennis Channel 2" / "YES Network Overflow" | Its own row next to the channel, same section |

## Build 154: guide listings by channel identity

Channels sometimes showed another channel's schedule: a provider guide id pointing at a local station (CBS News 24/7 on WCBS, KNBC on NBC Connecticut, History on NYC Life), a UK or Canadian channel of the same name (Discovery, GET TV, YES Network), or the other coast's times (an East row on a schedule labelled West, a West row on East times with no shift). Now each row's schedules are weighed before one shows: another known channel, station or country is set aside; outside listings' coast labels are trusted and the provider's are checked by lining the schedules up at 0 and 3 hours; each schedule is moved to the row's coast; and of the distinct schedules left, the one most independent guides agree on shows (copies sharing one schedule count once). Hold OK › Listings look wrong? shows the row's distinct schedules as on now / next, to pick the right one; the pick stays until that schedule is gone. The channel report says which schedule each row shows and why (guide_choice) and what was set aside (guide_rejected).

| Area | How to check | Pass |
|---|---|---|
| Other channel | My Lineup › CBS News 24/7, History, KNBC (West lineup) | No Price Is Right on CBS News, no NYC Life on History, no Connecticut news on KNBC |
| Other country | Discovery, GET TV, YES Network with UK and Canada listings on | US programmes at US times |
| East coast | East lineup: Discovery, Food Network, HGTV, TNT | What the guide says is on matches the picture |
| West coast | West lineup: HBO West, Freeform West, National Geographic West | Listings 3 h after the East row's; matches the picture |
| Listings look wrong? | Hold OK on a channel › Listings look wrong? | Automatic plus each distinct schedule as "On now · …" / "Next · … time", no source names; the one shown is marked |
| Pick | Pick another schedule, then relaunch | The guide shows it at once and after the relaunch; Automatic goes back |
| Player | Sources (red key) › Listings look wrong? | The same panel over the picture |
| Report | Help › channel report | guide_choice and guide_rejected filled for My Lineup rows |
| Speed (LG) | Scroll the guide, change the time window | No pause; the guide header settles as before |

## Build 155: 1.0.4, more channels with their own logo

More rows show the channel's own logo instead of the provider's. The logo library's free streaming folder (misc/vod, same commit and CDN) is asked by the channel's exact name ("The Bob Ross Channel", "Vevo '70s", "AMC+"); names the library files under another name are found ("Family Movie Classics", IndiePlex, RetroPlex, LiveNOW from FOX, beIN SPORTS en Español, Jewelry Television, Sho x Bet, NBC Sports Bay Area and California, SportsNet LA); a numbered, extra or alternate feed shows its parent's ("BEIN SPORTS CONNECT 5", "BIG TEN ALTERNATE 2", "ESPN COLLEGE EXTRA 6", "Tennis Channel 2"). A station shows as a station in any section: a Telemundo station under Spanish has its call sign and Telemundo's logo. Other countries' channels get iptv-org's logo by their guide id (built in, packed: about 17,500 ids in 0.56 MB). A listed channel iptv-org has no logo for gets its Wikidata logo, rendered as a PNG by Wikimedia. Of a channel's iptv-org logos the best for the wide cell is kept (PNG, wide, large; never a search engine's thumbnail). Where the library has a horizontal version that fills the wide cell better (Syfy, Tennis Channel, SEC Network, Paramount Network…), that one. A FanDuel regional network with no other logo shows its Bally Sports file.

| Area | How to check | Pass |
|---|---|---|
| Free streaming | My Lineup: AMC+, EstrellaTV, Vevo '80s, The Archive, WildEarth | Each its own logo; channel report logo_sources "logo set, provider" |
| Other names | Family Movie Classics, IndiePlex, LiveNOW from FOX, beIN SPORTS en Español, Sho x Bet | Each its own logo |
| Feeds | beIN Sports Connect 3–8, Big Ten Alternate, ESPN College Extra 1–7, TUDN Extra | The parent's logo |
| Stations | Spanish: TELEMUNDO 52 [LOS ANGELES], NBC (KSTS) TELEMUNDO | Call sign, "Telemundo 52" and Telemundo's logo (not NBC's) |
| Other countries | A foreign channel with a guide id (Kanal 7, TRT Kurdî) | Its logo; logo_sources "public database, provider" |
| Wikidata | A listed channel iptv-org has no logo for (NTN24, TyC Sports) | Its logo; logo_sources "Wikidata, provider" |
| Wide | Syfy, Tennis Channel, Game Show Network | The horizontal logo fills the cell |
| Log | marigold.log at launch | "fallback logos: … by id, … by name, … abroad (… KB)" |

## Build 159: Right goes back to what you watched; sleep pauses recovery

Right (Rewind on Fire TV, the Back to button, the right edge with the pointer, the failed card's last button) goes back to whatever was on screen last, a channel or a game, however briefly it was watched: anything that went full screen and showed a picture counts, and Up / Down zapping still stops on one channel before it tunes. Something that never showed a picture is skipped. A game comes back on the source it played from (the feed picked in Where to Watch, or the backup that took over), with Up and Down going through the games again; a channel comes back with its own list (the guide list it was picked from too). Pressing Right twice swaps the two. Another source of the same game and the cameras panel don't change where Right goes. A game no longer on the board is found again by its schedule game; if it's gone, its channel plays with a short note ("That game has ended · back on ESPN"). Each Right adds a "back to" line to the playback log (channel or game, no links). A reminder set on a game plays the game, on the channel it was set for.

Leaving the app (home, another app, the Mac sleeping or hidden) pauses playback recovery: nothing is retried, logged as failed or held against a feed while the app is away. Back in the app, what was playing starts again on the same feed; a game that has left the board meanwhile ends quietly. The log says "paused" (app in background) and "resumed".

| Area | How to check | Pass |
|---|---|---|
| Brief channel | Watch A, Down to B, wait for B's picture (2–3 s), Down to C, press Right | Goes to B, not A |
| No picture | Watch A, Down to a channel that doesn't start, Down again before it does, press Right | Goes to A |
| Swap | Right, then Right again, several times | Swaps between the same two each time |
| Game and channel | Watch a channel, then a game from Live Events on a feed picked in Where to Watch, press Right, then Right again | The channel, then the game on the same feed (Sources ticks it); Up / Down go through the games |
| Two games | Game A, then game B from the event guide, Right | Back to A on its source; Right again goes to B |
| Hints | With a game as the target: the banner's key guide, OK (Back to button), the pointer's right edge, Back once on a clear picture | Each says "Back to Rams at Seahawks" (the matchup, no score) |
| Source switch | On a game: Sources › another source, then Cameras on a race, press Right | Goes to what was on before the game, not to another source or camera |
| Game ended | Watch a game on a channel, then another channel; after the game leaves Live Events, press Right | Its channel plays; "That game has ended · back on …" shows briefly |
| Up / Down after Right | Favorites › a channel, then a game, Right, then Up / Down | Up / Down go through Favorites; the guide in the player opens on Favorites |
| Reminder | Remind me on a game; OK on the reminder when it fires | The game plays (game banner), not just the channel |
| Log | Help › Send a report › Playback log after a few Rights | One "back to" line each (channel or game), no links |
| Apple TV home | While a channel stalls ("Buffering…"), press TV / home; come back after an hour | The channel starts again at once on the same feed; the log has "paused", then "resumed"; no "not recovered" or failure lines between them |
| Fire TV / Android | Home while watching, back after a few minutes | Same channel, same feed; no failures logged |
| Mac sleep | Sleep the Mac while watching, wake it | Starts again on the same feed; nothing marked failed in Sources |
| LG menus | The remote's Settings button while watching | Still plays behind the menu (no pause line); home and back starts it again |
| Game gone while away | Leave the app on a game; come back after it left Live Events | Playback ends quietly (no failure card, no failure in the log) |

## Build 160: sources in one language order; the part on now first

A game's sources now follow one language order everywhere: the board's list, what Watch plays first and its automatic backups, Where to Watch (also the player's Sources panel), the tile's and the details card's channel, and a reminder. First the viewer's first language (English), then sources of no known language (mostly English channels with no mark), then the viewer's other languages (Spanish when picked), then every other language a source names. My Lineup, the kind of source, a station's rank and a feed's record and picture only order sources within one language; a French copy that starts reliably no longer jumps above an English feed, and a Spanish channel in My Lineup comes after an English one that isn't. Where to Watch lists your channels and the provider's feeds in English or no known language first, then "In Spanish", "In French"… below.

Languages read better: a feed of Canada's (English and French) stays not known; a Quebec channel ("|CA| QUEBEC SPORT") is French, not English; a language named last in a feed's title ("em Português", "en Español", ": English", "- French") is read; a name's own country ("FR: …") counts where its group names none.

Multi-part events (UFC's early prelims, prelims and main card; a race weekend's sessions) start on the part on now, decided when you press Watch or open Where to Watch, never while something plays. The part on now: the scoreboard's live part; else the first part not final, moved on by the clock once the next part has started and the one before had half an hour to finish (none when its listings ended); a part that's final moves on. Then each source: a pick in Where to Watch; the source you were on, only when it was on this part or is on every part; the part on now with the whole-night feeds on it, the next part, the parts over last; then the language order. A feed that names no part counts for the part it starts at and every later one. Where to Watch lists the part on now first. A boxing card with one part on the schedule: a "Prelims" slot hours before the main event's goes after it once the main event has run half an hour.

| Area | How to check | Pass |
|---|---|---|
| English first | An NFL game with French and Portuguese DAZN copies and US feeds (Chiefs at Falcons) | Where to Watch: Your channels, Event feeds from your provider (the US feeds), then In French, In Portuguese; Watch starts on a US feed |
| Unknown second | A game on an unmarked channel (\|AF\| DSTV) and US feeds | Watch starts on the US feed; the unmarked channel next |
| Your others | Spanish picked too | English, unknown, then Spanish; "In Spanish" above other languages |
| My Lineup | A Spanish channel in My Lineup and an English one that isn't | The tile names the English channel; Watch starts there |
| Tile = Watch | Any game | The channel the tile and details card name is the one Watch opens |
| Main card | UFC with the main card on | Watch opens a main card source; Where to Watch lists Main Card first, Prelims last |
| Memory | Watch the prelims, leave, come back at the main card | A main card source, not the prelims feed; a whole-night feed you were on still leads |
| Late prelims | The prelims running past the main card's time, no score status | Still the prelims for half an hour, then the main card |
| Pick | Where to Watch › a Prelims source during the main card | That source plays |
| Race weekend | Qualifying on | Watch and Where to Watch start on qualifying's sources |
| No switching | Play a card while its part changes | Playback stays on what plays |
| Speed (LG) | Open Live Events, press Watch, open Where to Watch | No slower than before |

## Build 161: 1.0.5, shut-down and duplicate channels

Channels that have shut down no longer fill the guide, and several channels that showed twice (or inside another channel's row) are one row now.
- **Shut down:** a channel that has closed (HBO Family, MovieMAX, OuterMAX, ThrillerMAX, Universal Kids, NBC Sports Chicago, NBC Sports Northwest, Fox College Sports, Circle) is out of the guide, All Channels, Search and channel up/down unless one of its copies has listings (GolTV still shows matches) or the viewer added it or made it a favourite. It's never suggested. A shut-down channel the list knows the parent of (HBO Family: HBO) joins only that channel's row, and only when the provider's guide files it there.
- **Stations:** a station is its own network's, whatever else the name says: "NBC (KVEA) TELEMUNDO" and "TELEMUNDO 52 (KVEA)" are one row (14 Telemundo stations had two). Sister stations on subchannels stay apart. A US Spanish station without its call sign ("TELEMUNDO 48 [SAN FRANCISCO]", "UNIVISION 65 [NEW JERSEY]") is found by its network and number or city and belongs to its own market; "TELEMUNDO 47" (two stations have 47) is suggested only where one of them is the viewer's.
- **Brackets:** a place or team in brackets is part of the channel: "NBC SPORTS [BAY AREA]" is NBC Sports Bay Area, "SPECTRUM SPORTSNET [LA DODGERS]" is SportsNet LA, "FOX 5 [SAN DIEGO]" and "FOX 5 [LAS VEGAS]" are two rows. NBCSN (relaunched in 2025) keeps only its own copies. Tags in brackets ("[SLING]", "[EVENT ONLY]") still don't count.
- **Old names:** a channel the channel directory knows by an old name ("CSN Bay Area", "Fuel TV", "DIY", "H2") joins the list's row for it.
- **Other:** a network's name no longer lets a sports channel or event slot past the list ("Fox Sports Tennessee", "WNBA ON ION 1", "…: Nbc Sports 06"): only stations and network feeds ("UNIVISION EAST", "NBC LOS ANGELES NEWS"). FanDuel, Bally, Fox Sports and NBC Sports regional networks and Root Sports from the directory's free tier are Other, not Free. CBS Sports Golazo Network is free streaming (CBS Sports apps, Pluto TV, Paramount+): Free.
- **Overflow feeds:** "ESPN 3" is no ESPN overflow (ESPN has ESPN2). "MLB NETWORK ALT" and "NHL NETWORK [ALT]" are the alternates, "NFL NETWORK ALT" an overflow feed. An overflow feed (beIN SPORTS 1–8, NBC Sports California 2) is suggested only while it has a real programme, not "SIGN OFF".
- **Coasts:** a copy marked East, or with no coast, never joins a West row: "SHOWTIME EAST" and "|US| SHOWTIME" are Paramount+ with Showtime (East), not West. A coast that is part of a name ("FanDuel Sports Network West", "Max East") is matched only by a name that says it.
- **League channels:** "The NBA Channel", "The MLB Channel" and "NFL Channel" are the leagues' free streaming channels, not NBA TV, MLB Network or NFL Network: they no longer join those rows.
- **Report:** the channel report has row_category (the row's own category: Other and Free rows show) and row_closed.
- **Speed:** the lineup build no longer works out every TV market's first city for each bracketed city in a name: about a quarter faster on a 30,000-channel list.

The list gains NBC Sports Chicago (and Plus), NBC Sports Northwest and Fox College Sports Atlantic, Central and Pacific as shut-down rows, Circle as shut down, and other names providers use: "Fuel TV", "CSN Bay Area / California / Boston / Philadelphia / Chicago (Plus)", "Max East" / "Max West" (Cinemax's feeds only), "Cinemax 5 Star Max", "Discovery Espanol", "Univision TUDN", "Fox Sports Deportes", "Epix Drive In", "NESN Boston", "SNY SportsNet New York", "AT&T SportsNet Pittsburgh", "Starz Black" (Starz in Black). tools/reference/pack.py flags a channel shut down when its notes say so anywhere ("wound down", "went dark", "ceased", "shut down") and keeps each shut-down name's parent.

| Area | How to check | Pass |
|---|---|---|
| Shut down | Guide, All Channels, Search: HBO Family, Universal Kids, MovieMAX | Not there; channel up/down skips them |
| With listings | GolTV during a match | Shown, not in My Lineup |
| Kept | A shut-down channel added to My Lineup or made a favourite | Still shown and in My Lineup |
| Telemundo | Spanish: KVEA, KSTS | One row each, with every copy |
| Other markets | California viewer, US Spanish on | No TELEMUNDO 47 or UNIVISION 65 [NEW JERSEY] in My Lineup |
| NBC Sports | NBCSN, NBC Sports Bay Area, NBC Sports California | NBCSN has only NBCSN copies; the Bay Area and California copies are in their own rows |
| Dodgers | SportsNet LA | Has the "[LA DODGERS]" copies; Spectrum SportsNet (Lakers) doesn't |
| Showtime | West lineup | Paramount+ with Showtime West has only West copies; the East row has "SHOWTIME EAST" |
| Overflow | beIN SPORTS 5 at night ("SIGN OFF") | Not added to My Lineup; joins when a match is listed |
| League channels | NBA TV, MLB Network, NFL Network | Only their own copies; "The NBA Channel" etc. aren't in them |
| Report | Help › channel report | row_category and row_closed filled |
| Speed (LG) | Relaunch with the channel list cached, and a full refresh | Building your lineup takes no longer than Build 158 |

## Build 162: 1.0.6, which channels have catch-up (and what else providers say)

Xtream providers say per channel whether they keep a catch-up archive and for how many days (`tv_archive`, `tv_archive_duration` in the channel list the app already downloads); M3U playlists can say the same (`catchup-days`). The app now reads it and keeps it with the saved channel list. Nothing on screen changes and no archive is played: the channel report gains `catchup_days` (the days, 0 for none, empty for Emby, which has no catch-up for live channels).

The same channel list also says, per Xtream copy, whether the provider marks it adult (`is_adult`), its other categories (`category_ids`), when the provider added it (`added`) and whether it gives a second address for the stream (`direct_source`; only that there is one: the address is never kept or reported). The report gains `adult`, `more_categories`, `added` (a date) and `direct_source` (yes or empty).

| Area | How to check | Pass |
|---|---|---|
| Report | Help › channel report after a refresh | `catchup_days` filled for Xtream and M3U copies |
| Provider fields | Same report | `adult`, `more_categories`, `added`, `direct_source` filled where the provider gives them; no address anywhere in the report |
| Saved list | Relaunch with the channel list cached | Same values as before the relaunch |
| No change | Guide, playback, Live Events | Same as Build 161 |

## Build 163: 1.0.7, catch-up test and faster fallback

### Faster fallback

- A version that fails to start no longer gets two more tries before another version is tried (the playback logs: the retry played 18 times in 83 and failed again 42 times, about 12 s a try). A version from the other provider, with its own connection limit, is tried at once; with only the same provider's versions left, the failed one gets one retry first; the last version left still gets two.
- A direct feed the server closes before a picture ("stream ended") fails at once rather than after the 10 s wait.

| Area | How to check | Pass |
|---|---|---|
| Dead first copy | A channel whose first version is dead and has a version on the other provider (SportsGrid on the 1.0.6 log) | The other provider's version plays about 10 s after the tune, no "retry" line before the "switch" |
| Same provider | A channel whose versions are all on one provider, first one dead | One "retry … (attempt 1 of 1)", then the next version |
| One version | A channel with one version that won't start (Flix West) | Two retries, then the "stopped working" card, as before |
| Ended early | The playback log on a dead copy | "stream ended", then "start failed: ended before a picture" at the same moment |

### Catch-up test

Build 162 reads which channels say they keep catch-up (on the exports, one provider says 662 channels keep 2 days, another 133 keep 3). This build finds out whether that archive really plays, before Start Over or rewinding is designed. It is a test only: nothing else changes, and nothing runs unless the viewer opens it.

- **Offered:** OK in the player on a channel whose copy playing is an Xtream copy that says it keeps catch-up shows a "Catch-up: N days" pill in the info row and a "Test catch-up" button (with a small TEST tag) in the controls. Neither appears for a copy without catch-up, for M3U or Emby, before the picture, or on a race camera.
- **Panel:** the button opens a small side panel: "Test catch-up", "<channel> · <provider>, version i of n · archive: N days", and three choices: "10 minutes ago" ("Plays from 7:52 PM"), "Start of this programme" ("<title> from 8:00 PM", only when the guide knows what's on and it started more than 2 minutes ago) and "Yesterday, same time".
- **Playing:** a choice plays that copy's archive from that minute (`/timeshift/…/<minutes>/<YYYY-MM-DD:HH-MM>/<id>.ts`, MPEG-TS, from then until now plus 5 minutes, at most 3 hours). The top left says "CATCH-UP TEST" and how far behind live ("−10:00 behind live"). The picture: "Catch-up worked · picture in 2.3 s · playing from 7:52 PM · <provider>, version i of n", with Back to live (focused) and Test again.
- **Not playing:** no picture in 10 s, or a player error: "Catch-up didn't play on this copy", what the provider claims and what happened, with "Try another copy with catch-up" (only when another version of the channel says it keeps catch-up; the same moment on that copy) and Back to live. Nothing reconnects and no other copy is tried by itself.
- **Server clock:** the provider wants the start in its server's own time. The account check already made at every launch and refresh (`player_api.php`) gives `server_info` with `time_now` and `timestamp_now`: their difference, to 15 minutes, is the server's offset from UTC (else its `timezone` name, for the zones the app knows; else UTC). It is kept with the account, so it survives a restart.
- **Log:** every test is one "catch-up test" line in the playback log, worked or not, with the channel and the feed as other lines name them: "10 min ago (archive says 2 days): worked, picture in 2.3 s, 1920x1080; server clock UTC−5", "start of programme 19:00 (archive says 2 days): failed, no picture after 10 s; …", or "ended before a result (back to live)". Never a link or a login: the archive link goes to the player only, like a live one.
- **Leaving:** Back, Back to live or the same channel returns to it live on the copy that played before; channel up/down goes to the next channel live as usual; the app going to the background ends the test (live plays on return). A test never changes a feed's reliability, the source a channel starts with, what Right goes back to, the recovery or anything saved.
- **LG:** the archive is a continuous .ts link and plays like a live one (directly, or through the in-app relay with subtitles on or Play through Marigold).

| Area | How to check | Pass |
|---|---|---|
| Offered | OK on an Xtream channel whose report says `catchup_days` > 0 | "Catch-up: N days" pill and Test catch-up button |
| Not offered | OK on a copy with `catchup_days` 0, an M3U or Emby channel | Neither shows |
| Panel | Test catch-up | Channel, provider, version and days; times in your clock format; "Start of this programme" only with a listing that began over 2 min ago |
| 10 minutes ago | Pick it | Tag "−10:… behind live"; the picture from about 10 minutes ago, or the card after 10 s |
| Programme start | Pick it on a show that started a while ago | The show's start plays (the server's clock is right) |
| Yesterday | Pick it | Yesterday's programme at this time, or the card |
| Another copy | On the card, with a second copy that has catch-up | Same moment on that copy; the button goes once none is left |
| Back to live | Back, Back to live, the same channel | Live on the copy before; banner as after a tune |
| Channel keys | CH+ / Down during a test | The next channel, live |
| Background | Home during a test, then back | Live plays |
| Log | Send a report › Playback log | One "catch-up test" line per test with the result and the server clock; no link anywhere |
| Nothing kept | After tests: Sources, Right, relaunch | Same feed order, same Right target, nothing new saved |

## Build 164: 1.0.8, catch-up belongs to the channel

The catch-up test is offered on a channel when any of its own copies keeps an archive, whichever copy plays live. The test replays on the copy playing when that one keeps an archive, else on the first of the channel's copies (in the order they're tried live) that does; "Try another copy" moves on to the next one that does. Back to live returns to the copy that played live. The panel names the copy the test replays on.

| Area | How to check | Pass |
|---|---|---|
| Offered on any copy | A channel playing live on an Xtream copy without catch-up, with a Dream copy that has it (ESPN, CNN) | "Catch-up: 2 days" and Test catch-up show |
| Replays on the right copy | Test catch-up › 10 minutes ago | The panel and the log name the Dream copy; it plays from 10 minutes ago |
| Back to live | Back to live | Live again on the copy that played before the test |
| No copy with an archive | A channel none of whose copies has catch-up (KCBS) | Neither the pill nor the button |

## Build 165: 1.0.9, replay (pause, rewind, start over, go live)

The catch-up test becomes replay. It belongs to the channel: any of its own copies with an archive serves it (the one playing live when it has one, else the first in live order), and live stays on the best live copy. Channels with no copy that keeps catch-up look exactly as before; nothing new runs unless replay is used, apart from the guide keeping a few more hours for these channels.

- **Controls (OK):** at the front of the row, Pause, Rewind (a counter-clockwise arrow) and Start over (only when the programme's start is known and inside the archive), then today's buttons. Over the row, a thin bar for the programme on now: its start at the left, its end at the right, a white dot at now. The info line says "8:00 – 9:00 PM · started 38 min ago". A full row shortens "Back to X" to "Back", then Subtitles' second words, then Rewind and Pause to their marks alone.
- **Pause:** Play/Pause, Play or Pause (Space on a Mac, the Siri Remote's play/pause, Fire TV's play/pause, LG's Pause) or the Pause button. The picture holds where the player can (LG, Fire TV, Apple TV and Mac pause their player), dimmed, with a pause mark; about 3 s later the stream closes so the provider's connection is free (what stays on screen then is the platform's: a dark screen at worst). The panel: "Paused at 8:36 PM", "for 2:14 · CNN · <programme>", the programme's bar (where it paused, and live in red), and "Play from 8:36" (focused), "Go live", "Rewind". Play resumes from the paused minute (a few seconds early). On a channel without replay: "Live TV can't be paused", as before.
- **Rewind (the timeline):** the Rewind button, or Fire TV's fast forward. The picture holds; at the bottom, the bar from the start of the programme before this one (or 2 h back, whichever is earlier) to live, programmes by title, live in red, and the playhead's bubble ("8:21 PM · −17 min" and the programme then). Left / Right: 1 minute; held, 5, then 15. Up / Down: the start of the programme before / after. OK: play from there (at live: live). Back: as before it opened (live, the replay where it was, or paused). Moving before the bar scrolls it back, as far as the archive (2 days on Dream) and never past it. With the pointer, a click on the bar picks that time; "Play from" plays it. The labels come from the guide (it keeps 6 hours back for these channels) and, a moment after the timeline opens, from the copy's own past (one `get_simple_data_table` request per channel; if it fails the timeline still works by minutes).
- **Behind live:** a tag at the top right, the rewind mark and "−17:05 behind live". With OK: "Watching from 8:21 PM · live is 8:38 PM", the bar shows where it plays (blue) and live (red), and Go live (red dot) comes first, then (on LG) Pause, Rewind, Start over and the rest. The banner's tag says REPLAY in place of LIVE.
- **Start over:** the button plays the programme on (behind live: the one playing) from its start; on a game whose source is a channel with replay, from the game's scheduled start. After a tune, when the programme began more than 3 minutes ago, the banner shows one focused-looking button, "Start over from 7:30", where "Hides in" sits: OK takes it; any other key, or the banner hiding, keeps live. Not offered while channel keys are still settling, nor during a replay.
- **Playing a replay:** the archive copy from the minute asked, a stretch of up to 3 hours (or until now and 5 minutes more). When a stretch ends behind live the next one opens from where it ended, with no message. Reaching live (within a minute) goes live: "You're live". No picture in 10 s or an error: the next copy with an archive, same minute; none left: "Replay isn't available right now on this channel", and live.
- **Channels:** Up / Down, CH+ / CH- and the guide play the next channel live. Right goes back to the replay at the moment you left it while the archive still holds it (straight to the archive, no live start first), else live. The app going to the background: a replay plays on from the same moment on return; a pause stays paused.
- **Nothing charged:** live recovery stands aside during replay and comes back with live; a replay never counts for or against a feed, never becomes the source a channel or game starts with, and "Always start with" is unchanged.
- **Log:** one "replay" line per action, with the copy's feed and provider and, when a stretch is asked, the server clock: "start over 19:30; server clock UTC−5", "rewind to 20:21", "paused at 20:36 (live)", "resumed 20:36", "back to 20:21" (Right), "next stretch from 21:21", "another copy from 20:21", "failed: no picture after 10 s; back to live", "caught up", "back to live", "past listings: 47 h, 31 in archive". Each guide read adds a "guide" line: "guide past: provider file reaches back 26 h", "guide past: outside US national 5 h, …". Never a link.
- **LG relay:** a replay stretch through the relay (subtitles on, or Play through Marigold) is not reopened from its start when it ends; the session opens the next stretch.

| Area | How to check | Pass |
|---|---|---|
| Not offered | OK on KCBS (no copy with catch-up); Play/Pause there | The row as in 1.0.8, no bar; "Live TV can't be paused" |
| Offered | OK on ESPN or CNN (Dream copy, 2 days) | Pause, Rewind, Start over first; the programme's bar over them; "started N min ago" |
| Pause from live | Play/Pause (or the Pause button) | Picture held and dimmed, pause mark, "Paused at …"; after ~3 s the stream closes (the playback log, the provider's connection count) |
| Resume | Play/Pause or "Play from 8:36" after a minute | Plays from the paused minute, the tag "−1:… behind live" |
| Rewind | Rewind; Left a few times, hold Left, Up / Down, OK | 1 min steps, then 5 and 15 when held; programme starts; plays from the playhead |
| Rewind far back | Hold Left for a long time | The bar scrolls back, stops at 2 days |
| Rewind Back | Rewind, move, Back | As before: live, or the replay where it was |
| Pointer | LG Magic Remote / Mac: click on the bar, then Play from | The playhead jumps there; it plays from there |
| Apple TV swipes | Rewind, swipe left several times | Each swipe moves the playhead |
| Labels | Rewind on a channel with listings | Titles on the bar, more after a moment (the copy's own past); "past listings: N h, M in archive" in the log |
| Start over | Start over on a show begun 30 min ago | Plays from its start; tag and "Watching from …" |
| Start over on the banner | Tune a channel whose show began over 3 min ago, OK while the banner shows | "Start over from 7:30" button; OK starts it; any other key leaves it live; not during fast channel flipping |
| Game | Start over on a Live Events game on a Dream channel | From the game's scheduled start |
| Go live | Go live (first button), or the channel again from the guide | Live on the live copy, as after a tune |
| Next stretch | Replay from 4 h back, watch past 3 h (or check the log) | "next stretch from …", no message, the picture goes on |
| Caught up | Replay 2 min back, pause a moment, resume, wait | Goes live with "You're live" (or "caught up" in the log) |
| Failure | A channel whose archive doesn't play | Next copy tried; then "Replay isn't available right now on this channel" and live |
| Channel keys | Up / Down during a replay | Next channel live |
| Right | Replay ESPN, Down to CNN, then Right | ESPN's replay from where you left |
| Background | Home during a replay, then back; during a pause | Plays on from the same moment; the pause stays |
| Nothing charged | After replays: Sources, the source a channel starts with, relaunch | Unchanged |
| Keys | Fire TV play/pause, fast forward; Siri Remote play/pause; Mac Space; LG Play / Pause | Pause and play on; fast forward opens Rewind on Fire TV |
| Guide data | Send a report › Playback log after a guide update | "guide past: …" lines; memory readings as before |
| Log | Playback log | One "replay" line per action; no link anywhere |

## Build 166: 1.0.10, replay fixes and source scores

A third provider (Trex) keeps a one-day archive on many lineup channels, next to Dream's two days; a request for exactly 24 hours back failed on it. Replay now keeps an hour clear of every archive's end, and plays on the copy with the longest archive that holds the moment asked. Each copy also keeps a record of how its real plays went, which orders a channel's copies when one is clearly better and lets the providers be compared in the playback log. No new connections are made for any of it.

- **Replay margin:** the timeline, Rewind, Start over and Right's return stop at the archive's days less one hour (they stopped a minute short). On a one-day copy: 23 hours back at most. Start over isn't offered on a programme that began before that edge; Right goes live when the moment left is past it.
- **Longest archive:** of the channel's own copies whose archive holds the moment, the longest archive plays it, then the copy playing, then a clearly better measured copy, then live order. The timeline reaches as far as the longest archive among the copies, whichever copy plays live. A copy that fails tries the next one whose archive holds the same minute; a moment only the longer archive holds goes live with "Replay isn't available right now on this channel" when that copy fails.
- **Copy records:** every copy (a channel from one provider) keeps the median of its recent times to picture, its start failures, its stalls per hour played, its measured picture and when it was last seen. Older results count less: every figure halves each week. The records are saved in their own small file (the newest 3,000 copies; ids and numbers only, no links) and read at launch. A replay counts for nothing, as before.
- **Order:** a copy whose record is clearly better than another measured copy goes ahead of it; a copy with no record keeps its place (the order as in 1.0.9), and "Always start with" or the copy that played last still goes first. After a failure the next copy still prefers another provider.
- **Test all sources (1.0.11):** its probes go into the same records (a probe counts as half a play), with what the archive did.
- **Providers in the log:** "providers" lines in the playback log, one per provider and one per pair of providers: plays, median time to picture, start failure rate, stalls per hour, share of plays at 1080p or more, and the shares of My Lineup with a copy from that provider, with a copy from it alone and with replay from it; for a pair, the share either covers, with and without replay. For example "Trex · plays 12.4, picture 2.1 s median, start failures 5%, stalls 0.5/h, 1080p+ 75%; lineup 70%, only here 20%, replay 50%" and "Dream + Trex · together: lineup 90%, replay 60%". Written when the playback log is sent, and once a day at most after the app starts (once My Lineup is known). Percentages and plays only; never a link.
- **Channel report:** five new columns per copy: measured_start_ms, measured_failures, measured_stalls_per_h, measured_picture, measured_plays (empty for a copy never played).

| Area | How to check | Pass |
|---|---|---|
| One-day edge | Rewind on a channel whose only archive is Trex's, hold Left | Stops at 23 h back, not 24 |
| Two-day edge | The same on ESPN (Dream copy) | Stops at 47 h back |
| Start over at the edge | A programme that began 23 h 30 min ago on a Trex-only channel | No Start over |
| Longest archive | ESPN playing live on its Trex copy; Rewind past 24 h | The timeline reaches 47 h; the log's "rewind to" line names the Dream copy |
| Failover | A replay whose Dream copy fails within the last 23 h | "another copy from …" on the Trex copy, same minute |
| Past the shorter archive | A replay 30 h back whose Dream copy fails | "Replay isn't available right now on this channel", live |
| Right at the edge | Replay on a Trex-only channel near 23 h back, change channel, wait 10 min, Right | Live, not the old moment |
| Order unchanged without history | A channel never played since the update: Sources | Same order as 1.0.9 |
| Clearly better first | A channel whose first copy keeps failing and another starts fast at 1080p, after a few plays | The good copy is tried first; a pinned copy still leads |
| Records kept | Play a few channels, relaunch, send the channel report | measured_* columns filled for the copies played, empty elsewhere |
| Providers lines | Send a report › Playback log | A "providers" line per provider and per pair, percentages only, no link |
| Once a day | Relaunch twice within a day | One set of "providers" lines from the launches (more only when the log is sent) |
| Replay charges nothing | Replays, then the channel report | measured_* for the replayed copies unchanged by them |

## Build 166: 1.0.10, channels

The TV keeps up to 70,000 channels (was 30,000). With three providers the list a US viewer's rules let in is over 64,000 channels, so at 30,000 every other country's sports group, and some US ones, were cut. Nothing new to set: the same rules decide what comes first when a list is larger still.
- **Cap:** measured on a desktop build with a list put together from the last five channel reports (72,560 channels, 64,276 let in): the app's peak went from 215 MB to 345 MB, sorting from 3.1 s to 5.1 s, rebuilding My Lineup's rows from 0.15 s to 0.5 s, and the saved channel list from 20 MB to 41 MB (4.5 MB as saved, compressed; read in 0.5 s). The TV (32-bit) holds less per channel than a desktop. If the LG is restarted for memory, 50,000 still holds every group of the viewer's countries, unmarked groups and other countries' sports; above that come only free streaming groups (with Free off) and groups of empty slots.
- **What comes last:** a group named by its country in words ("SWEDEN HD & HEVC", "NORWAY VIAPLAY PPV", Denmark, Finland, Iceland), or headed by a "#####" line, is that country's, as "SE|" is: its sports groups are kept after the viewer's country's, the rest left out (before, about 4,600 of one provider's Nordic channels came ahead of every other country's sports). A group of four in five or more empty slots and headings ("NO EVENT STREAMING", "#####", a slot dated "2098-12-31") comes after everything else unless My Lineup has one of its channels; when its games are listed, the next refresh puts it back in its country's place.
- **Names:** a provider's prefix with a bar goes as "TV:" does: "TV| A&E", "GOLD| …", "4K| …", "NFL TEAMS| …", "NBA TEAM| …" (a team prefix keeps the row a team's channel). The country is still read from the group and the name. Resolutions and more quality words leave the name: "1080", "720", "2160" at the end, "1080p", "3840P", "4K+", "HDR". So one provider's 549 "TV| …" copies join their channels' rows (A&E, AMC, CNN, CNBC, ESPN2, FS1, HBO Latino, TUDN, Universo…), and "|US| GOL TV ᵁᴴᴰ 1080" is GolTV. A row that changes id this way keeps its old one as an earlier id, so picks, removals and favourites carry over.
- **Other:** fewer rows end up there. A channel the channel directory knows keeps its section; a regional sports network (FanDuel, Bally and Fox Sports names, MSG and other alternates) and a team's own channel (NBA, NHL, MLB, NFL, MLS, WNBA teams) are Sports; US Spanish channels (TUDN Extra, Zona TUDN, ViX Deportes, Telemundo Deportes Ahora, Cinemax Latino) are Spanish. Streaming services' show loops (Hulu, Peacock, Paramount+ originals, Cinemania, Big Brother cameras) and actors' film loops stay in Other.
- **Event feeds:** "ESPN PLAY EVENTS 12 HD", "<brand> EVENTS 3" and "ESPN COLLEGE EXTRA 5" are a brand's numbered game feeds, in Live Events with the other event feeds, not channels in the guide.
- **Other names:** a name of two or more words that shares no word with a channel's is taken for one of its other names only when written like a channel's (in capitals, or with a number or a word such as TV, Network, HD): a "Jennifer Lopez" film loop is no longer The Weather Channel (a meteorologist of that name is among the directory's other names for it). Renamed channels written that way (Spike, DIY, EPIX HITS, Fuel TV) still find their rows.
- **Locals:** a US station named by its region ("NBC BAY AREA HD") is the market's station (KNTV) and joins its row, keeping its own row's id for picks. A local with no call sign but a place goes to the picked area whose place it is ("Bay Area": San Francisco); a place two areas share goes to home. "Local" holds only stations of no picked area. An area picked from the station lists (written with en dashes) now also counts the built-in list's stations of that market.
- **Saved list:** a channel list with Other rows was read back as damaged, so every launch sorted the whole list again before the guide showed; it is read now, and a launch whose download hasn't changed skips the sort. The saved list's version changes, so the first launch of 1.0.10 sorts once.

Measured with this report's channels through the rules (each provider as its own account): Other rows 1,956 → 1,112; Sports 309 → 722; Spanish 136 → 176; My Lineup 349 rows → 343 (the "TV|" copies of HBO Latino, TUDN, Universo, ABC News Live, Fox News and NBC News Now, GolTV and NBC Bay Area joined their rows; none left in Other); one provider's US channel copies with a row the channel lists know 39% → 62%, in Other 62% → 19%.

| Area | How to check | Pass |
|---|---|---|
| Cap | Help › playback log after a full refresh | "admitted N channels in G groups; 0 groups cut for room" (N above 30,000 with three providers) |
| Memory (LG) | Settings › Help & info › About › Memory in use, after a full refresh and after an hour | Not restarted; within about 150 MB of 1.0.9's figure |
| Speed (LG) | "sorted N channels in X ms" in the log; edit My Lineup | Sorting under a minute; the lineup updates within a few seconds |
| Relaunch | Relaunch with nothing changed at the provider | The guide shows without "Sorting your channels…"; the log says the list is unchanged |
| Nordic groups | All Channels, Search: SVT 1, TV 2 | Not there (another country's non-sports channels); their sports events in Live Events |
| TV\| copies | A&E, CNN, ESPN2, HBO Latino, TUDN, Universo | One row each, with the "TV\|" copy among its sources; no second row |
| GolTV | Search "GOL" | One GolTV row |
| Other | Settings › Other channels on, then All Channels › Other | No FanDuel, Bally, team or TUDN Extra rows there; Hulu / Peacock originals and film loops still there |
| Sports | All Channels › Sports | FanDuel Sports Network rows, team channels (Boston Bruins, Bears) |
| Spanish | All Channels › Spanish (Spanish on) | TUDN Extra 1–11, Zona TUDN, ViX Deportes, Cinemax Latino |
| Event feeds | Live Events with Event channels on | "ESPN PLAY EVENTS" feeds there; none in the guide |
| Weather Channel | Search "Weather" | The Weather Channel row has no film loop among its sources |
| Bay Area | My Lineup with San Francisco and Los Angeles picked | NBC Bay Area's copy is in KNTV's row under San Francisco; no "Local" row for it |
| Picks kept | A row added, removed or starred before (HBO Latino's "TV\|" row, NBC Bay Area) | Still added, removed or starred after the update |

## Build 166: 1.0.10, Test all sources

Settings › Providers gets a "Test all sources" row. It checks every source (copy) of every channel in My Lineup, on every provider, once: how fast it starts, whether it plays, its real picture and how far back its replay really goes. The results order each channel's sources and rank the providers. It runs only when asked, pauses while anything plays, and makes one connection per provider at a time.

- **The row:** "Test all sources" with "Last tested: never", "Last tested: today 1:12 AM" (or "yesterday 9:40 PM", "Nov 14"), "Testing now · 38%" or "Paused while you watch · 38%". Dimmed with "Add a provider first" / "Set up My Lineup first". OK opens the testing screen while a test runs, the results once one has ended, else the start panel.
- **Start panel:** over Settings, "Test all sources", what it checks, "About 3 to 4 hours. Leave the TV on; the screen dims." (worked out from the lineup: the longest provider's tests at about 11.5 s each), "Start watching any time: the test pauses and carries on after.", "One connection per provider at a time, gently spaced.", and Start / Not now.
- **What is tested:** every copy of every My Lineup row, each once, on every Xtream or M3U provider (Emby / Jellyfin streams are opened by their server, so they're left out). Live, and for a copy that says it keeps catch-up: a stretch of its archive two hours back, then one near its edge (its days less an hour and a half: 22 h 30 min back on a one-day archive, 46 h 30 min on two days) to confirm the depth. The edge isn't asked for when two hours back didn't play.
- **The probe:** the same link and headers the player gets, read over HTTP for a few seconds, no player: the time to the first byte and to the first key picture, and the picture's size and frame rate from the H.264 / HEVC sequence header in the MPEG-TS (the same reader as the LG picture check). A copy that answers with an HLS playlist is followed to its highest-bandwidth variant, then to the segment a player starts with (three from the end), read the same way (an fMP4 or encrypted segment counts as playing, at the size the playlist gives). No key picture within 12 s is a failure.
- **Pacing:** each provider one connection at a time, the providers side by side, 5 to 10 s (jittered) between one test and the next. About 1,630 copies over three providers (with their archive tests) is about three hours.
- **Refusals:** a provider answering 403, 429, 451, 458, "max connections" or refusing the connection is left alone for 15 minutes ("It refused a connection. Trying again in 15 min; the others carry on."); refusing again straight after leaves it out of this test.
- **Watching:** anything playing (the guide's corner too) or the app going to the background pauses the whole test at once (a probe under way is dropped and asked again later); it carries on 10 s after watching stops. Back from the testing screen leaves the test running in the app.
- **Kept across restarts:** which tests are done is saved (source-test.json: copy ids and results, no links); the app starting again carries on where it was. A test begun more than two days ago is dropped instead.
- **Testing screen:** "Testing your sources", "38% · about 2 h 10 min left" (from the pace measured), the bar, a card per provider with its share done and "Now: CNN · live" / "Now: CNN · replay 2 h back" (or paused, stopped, done), "FOUND SO FAR" with up to three plain sentences ("Some sources didn't start; they'll be tried last.", "A few say HD in their name but play at 720p.", "Replay goes back further on Dream than on Trex for the channels both carry.", "Replay didn't play on some sources that say they keep it."), and Stop (no question; what was found is kept). The screen stays on (webOS: the TV's screen saver request is answered "hold", as during video; Android: FLAG_KEEP_SCREEN_ON; Apple TV: the idle timer off; Mac: a display sleep assertion) and dims after a minute without a key; any key brings it back.
- **Into the records:** each copy's live probe goes into its record (a probe weighs half a play) with what its archive did. A copy probed in the last two weeks is ordered by its record at once (played copies still need two starts), so a copy that didn't start goes after the others and a clearly better one goes first; a pinned source still leads. The picture measured shows on the copy's rows as a play's does.
- **Results:** "Your providers, ranked", "Tested today, 1:12 AM · every channel now starts with its best source". A row per provider: Has your channels, Only here, Replay (the share of My Lineup with replay from it that the test didn't find broken, and the typical depth), Starts in (median), Didn't play (start failures, plays and probes together), Real full HD (its measured sources at 1080p or more). Ranked by 0.50 × works + 0.15 × speed + 0.15 × picture + 0.12 × lineup + 0.08 × replay, where works = 1 − 5 × the failure rate (0 from one start in five), speed = 1 at 1.5 s or less down to 0 at 10 s, picture = the full HD share (0.5 for anything not known yet). Buttons: Keeping one or two, Done, Test again.
- **Same service underneath:** two providers are flagged when at least half of the smaller one's lineup source names are the same once the country prefix's separator ("US|" / "US:") and spaces are evened out, unless enough of their sources failed to tell and the failures don't go together. The results then say "Trex and Xtream look like the same service underneath: when one stops, the other may too. Backups come from Dream first.", and a channel whose first source is on one of them gets its backup (second source) from another provider when it has one.
- **Keeping one or two:** every pair and every single provider with its lineup and replay shares and "Two separate services", "Likely the same service underneath" or "No backup"; the best pair first (separate services, then the lineup to five points, then replay, then reliability). On the right, the My Lineup channels the focused choice would lose, by name (up to seven, then "and a few more"). "Nothing changes until you remove a provider yourself."
- **Sources panel:** each source's line says what was measured: "Main feed · 1080p 60 · starts in 1.4 s · replay 1 day · Trex", "720p 60 (says HD)", "Didn't start · tried last" (amber); STARTS WITH on the first, BACKUP on the second; "tested today" / "tested Nov 14" at the top right once the channel was tested.
- **Log:** "test" lines in the playback log: started, each provider's start ("Dream: started", or "carried on"), a refusal ("Trex: paused, it refused a connection (http 429); trying again in 15 min", "Trex: stopped, it refused again (http 429)"), "paused while you watch", "carried on", "Dream: done", "finished" or "stopped by you", and a summary ("summary: Dream: started 82%, picture 2.0 s median, 1080p+ 70%, replay worked 95%; …"). Never a link, a user name or a password; no channel counts on screen.

| Area | How to check | Pass |
|---|---|---|
| Row | Settings › Providers | "Test all sources", "Last tested: never" |
| Start panel | OK on it | The panel over Settings; the estimate in hours; Start / Not now |
| Running | Start | Testing screen; a card per provider with "Now: … · live" or "· replay 2 h back"; the percentage and time left move |
| One connection | Watch each provider's connection count (or the provider's panel) during the test | Never more than one from the test per provider; tests a few seconds apart |
| Refusal | A provider at its connection limit (watch on another device) | Its card: "It refused a connection. Trying again in 15 min; the others carry on."; the others go on |
| Watching | Back, play a channel, then stop | The test pauses at once ("Paused while you watch" on the row), carries on about 10 s after |
| Screen | Leave the testing screen alone for a minute; then any key | It dims; the key brings it back without acting; no screen saver for the test's length (LG, Fire TV, Apple TV, Mac) |
| Restart | Quit during a test, start the app again | The test carries on; tests already done aren't asked again (playback log) |
| Stop | Stop | The results with what was found so far |
| Results | When it ends | Providers ranked, the six columns in percentages and seconds, the ranking note, the same-service note |
| Keeping one or two | Keeping one or two; move Up / Down | BEST PAIR on the first; the channels lost by name change with the focus |
| Sources | Red key (or Sources) on a tested channel | "tested today"; measured lines; STARTS WITH and BACKUP; a dead source "Didn't start · tried last" and last |
| Backup | A channel whose first source is on one of two look-alike providers | Its second source is from the other provider |
| Pointer | Magic Remote / Mac mouse on the buttons and the pairs; Apple TV swipes | Hover focuses, a click acts, the wheel moves through the pairs |
| Log | Send a report › Playback log | "test" lines and a summary; no link, login or password |

## Build 167: 1.0.11, replay marks and the replay banner

Channels with replay now say so where channels are listed, quietly, and the banner tells you what you are watching during a replay instead of what is on live. A channel has replay when any of its own copies keeps an archive; its depth is the longest of them. Test all sources counts too: a copy whose archive didn't play there doesn't count, and a copy whose archive it found shallower than its provider says counts for what was found. Nothing changes for a channel without replay, and nothing new runs: the rows work this out once, when the lineup changes or a test result changes it.

- **The mark:** the app's replay icon (the counter-clockwise arrow) in replay blue, about 16 points, never with a number. On a guide channel cell with a logo, it sits in the bottom-left corner (the star is top right, EAST / WEST bottom right). Only a logo whose fitted shape would reach that corner (one that fills the whole logo area) is drawn about 15% smaller, nudged up, to clear it; wide and square logos keep their size. On a named or local cell, the mark goes before the second line. Edit My Lineup's rows read "Sports · ↺ Replay"; Search shows the mark after a channel's name (and after the channel's name in a show's line); the guide in the player shows it under the channel's number.
- **The focused show:** after the channel's name (star, feeds, EAST / WEST), one outlined chip "↺ Replay" in the same plain look as "2 feeds": white, no capitals, no days. The guide never mentions Start over.
- **Can go back:** the guide's filter gets "Can go back" with the quick filters (shown when the list has a channel with replay): the channels with replay.
- **Hold OK → Start over:** in the guide's hold-OK menu, after Watch: "Start over" with "Started 7:00 PM", only on a programme on now that began more than 3 minutes ago and whose start the archive still holds (its days less an hour). OK plays the channel full screen from the programme's start, straight from the archive with no live start first (the log says "start over 19:00"). A channel already playing in the guide's corner replays from there.
- **About in Edit My Lineup:** under the row's usual text, "Replay" with the mark and "2 days on Dream, 1 day on Trex. Pause, rewind and start over work on it, and its games can be watched from the start." (provider names as you gave them; never how many channels).
- **The banner during a replay:** behind live, the banner names the programme at the moment you're watching: its title (with REPLAY), times, episode and description (one line), the bar in replay blue where it plays, "21 min in · 39 min left" within that programme, and under it one muted line "● Live now: Anderson Cooper 360 · 59 min behind". The list at the right becomes "NEXT IN THE REPLAY", starting after the programme on screen. The programme comes from the guide's listings, the past the guide keeps for replay channels (now with episodes and descriptions) and, once Rewind has opened, the copy's own past. When none is known for that moment: the channel's name, "Watching from 7:21 PM" and the live line. With the controls open, "Watching from … · live is …" as before. Caught up or live: the banner as before.

| Area | How to check | Pass |
|---|---|---|
| Logo cells | Guide on ESPN, CNN, a logo filling its whole cell (Food Network), TBS (starred, EAST), KCBS | The mark bottom left on the first four; only the full one smaller and higher; star, EAST and the mark each in its own corner; KCBS as before |
| Named cells | A channel with replay shown by name (no logo), a local station with replay | The mark before the second line; the line shifted, not cut short |
| No replay | A channel none of whose copies keeps an archive | Exactly as in 1.0.10 everywhere |
| Focused show | Focus ESPN in the guide | "↺ Replay" outlined chip after the name, white, no days; no Start over sentence |
| Filter | Filter › Can go back | Only channels with the mark; the chip at the top says "Can go back"; Back clears it |
| Start over (menu) | Hold OK on ESPN's programme begun 40 min ago | "Start over · Started 7:00 PM" after Watch; OK plays from 7:00 full screen, REPLAY on the banner |
| Not offered | Hold OK on a later programme, on one begun 2 min ago, on KCBS, on a Trex-only channel's programme begun 23 h 30 min ago | No Start over |
| Edit My Lineup | Settings › My Lineup › Edit My Lineup, focus ESPN | "Sports · ↺ Replay"; the About names each provider's depth ("2 days on Dream, 1 day on Trex") |
| Search | Search "warriors", Search "ESPN" | The mark after ESPN in the show's line and after the channel's name |
| Player guide | Left while watching | The mark under the numbers of channels with replay |
| Tested | After Test all sources found a copy's archive broken (or a day short) | That channel's mark and depth follow what was found (gone when it was its only archive) |
| Banner in a replay | Start over on CNN at 7:00 while 8:00 airs; Info at 7:21 | "Erin Burnett OutFront", 7:00 – 8:00 PM, blue bar, "21 min in · 39 min left", "● Live now: Anderson Cooper 360 · 59 min behind", NEXT IN THE REPLAY from 8:00 |
| No listing | Rewind on a channel with no guide for that hour, OK, Info | The channel's name, "Watching from 7:21 PM" and the live line |
| Caught up | Go live (or catch up) | The banner as before: the live show, LIVE, "Later on this channel" |
| Controls | OK during a replay | "Watching from … · live is …" as before |
| Log | Playback log after the menu's Start over | "replay: start over 19:00 …"; no link anywhere |

## Build 167: 1.0.11, replay in Live Events

A game on a channel whose copy keeps a catch-up archive can be watched from its start, straight from Live Events. The board works out which games have replay when it is made, never while drawing. Scores behave exactly as before (Show scores decides), and nothing new is set anywhere.

- **The rule:** a game gets replay only when all of these hold. One of its sources is a guide channel (My Lineup or your channels; not an event feed, not a team channel's "likely") whose guide listing shows the game at its start (the listing begins no later than 5 minutes after the scheduled start and hasn't ended by then). One of that channel's own copies keeps an archive that Test all sources hasn't found broken. The scheduled start less a minute is inside the archive as replay uses it (the days less an hour): on a one-day archive a game that started up to 23 hours ago, on two days 47 hours. The channel is in the language of the source OK plays (a channel with no language mark counts as English). The game is live, likely over or final (not later, postponed or without a time). When several channels qualify: the longest archive, then My Lineup, then the one OK plays.
- **Tiles:** a small blue replay mark at the end of the tile's top line, before the competition. Games only on event feeds and games still to come have none.
- **The focused game:** one quiet outlined "Replay" chip after the "On …" line under the matchup (on its own line when that line is full).
- **Holding OK on a live game:** "Start over" comes first, with "Started 7:10 PM" under it, and " · on ESPN" when the replay comes from another channel than the one OK plays. Where to Watch, Follow and Hide are as before.
- **A Final:** OK opens its card as before; with replay, "Watch replay" is the card's first button and has the focus, and Where to Watch stays. Holding OK on a Final offers "Watch replay" first too.
- **Starting it:** the channel's archive copy plays from a minute before the scheduled start, with no live start first, and the game banner shows. Only that channel's own copies play it; a copy that fails gives way to the next at the same minute. When none plays it: "Replay isn't available right now on this channel", then live (a live game) or back to the Final's card.
- **A live game started over:** reaching live goes live with "You're live", as on a channel; Go live plays the source OK plays. Start over, Rewind and Pause work as on a channel.
- **A Final's replay:** the tag at the top right reads REPLAY and how far into the game it is ("48:10 in"), and Go live is named for the channel ("NBA TV live"). Once the replay passes the moment the app saw the final, an "End of the game" card (the game and the channel, "Back to Live Events" focused, "Keep watching") shows for 15 seconds while the replay plays on. It never shows when the app didn't see the final itself. Paused so long that the archive no longer holds the moment: back to the Final's card.
- **Full games:** a Final of a team you follow, or of a league in your picks, that still has replay stays on the board past the usual 12 hours, in a row called "Full games" (newest first), until its start leaves the archive. The row is there only when it has a game. The app keeps these Finals in memory while it runs; after a restart only those the schedule still holds come back.
- **The game banner during a replay:** behind live, a REPLAY chip and "38 min in" next to the title; nothing from the live clock ("likely over"), no "was scheduled until", nothing after the game. At the right the channel replaying and one muted "Live now: …" line with the channel's programme on now ("Live now: this game" for a live game started over). Caught up, the banner is as before. It never shows a score.
- **Log:** "replay" lines as for channels: "game from 19:09 (Live Events, live)" or "(Live Events, final)" with the server clock, "another copy from 19:09", "game from 19:09: no copy of ESPN holds it; live", "past the final (21:31)", "failed: …; back to the game's card". Never a link.

| Area | How to check | Pass |
|---|---|---|
| Mark | Live Events with a live game on ESPN (Dream copy) and one on KCBS | The blue mark on ESPN's game only; none on event-feed games or later games |
| Chip | Focus the ESPN game | "Replay" chip after the "On ESPN" line; no chip on KCBS's game |
| Start over | Hold OK on the ESPN game | "Start over" first, "Started 7:10 PM"; Where to Watch, Follow, Hide below |
| Other channel | A game OK plays on a regional channel but listed on ESPN too (Dream) | "Started 7:10 PM · on ESPN" |
| Plays from the start | Start over | The game from a minute before its start, no live picture first; the game banner with REPLAY and "N min in"; "game from …" in the log |
| Live game, Go live | Go live during it | The source OK plays, live |
| Caught up | Start over a game begun 3 minutes ago, wait | "You're live" |
| Final's card | OK on a Final with replay | "Watch replay" first and focused; Where to Watch beside it |
| Final's hold OK | Hold OK on that Final | "Watch replay" first |
| Final's replay | Watch replay | Tag "REPLAY · 0:… in" counting from the start; Go live reads "<channel> live" |
| End card | Rewind to just before the game's end, play on | "End of the game" after the final's moment; hides after 15 s, the replay plays on |
| No end card | A Final the app never saw end (opened the next day) | No card |
| Edge | A Final 23 h 30 min ago on a one-day channel | No mark, no Watch replay |
| Failure | A channel whose archive doesn't play | Next copy; then the notice and live (live game) or the Final's card |
| Full games | Follow a team, the day after its game (two-day channel) | "Full games" row with the game; gone once its start is 47 h back |
| Scores | Show scores on and off | Tiles and cards as in 1.0.10 |
| Log | Send a report › Playback log | "replay" lines for game replays; no link |

## Build 169: 1.0.12, replay fixes

Four changes after testing 1.0.11. Nothing new runs; channels without replay look exactly as before.

- **The banner after a tune:** no "Start over from 7:30" button any more. OK on the banner opens the controls, as it did before Build 165.
- **The controls:** no "Start over" button. Live: Pause, Rewind; behind live: Go live (then Pause on LG), Rewind. To start a programme over: Rewind (the timeline), or hold OK on it in the guide, or on a game in Live Events (Start over / Watch replay), which work as before.
- **The guide's focused show:** the outlined chip shows the replay window, "↺ 2 days" or "↺ 1 day" (the channel's longest usable archive), in the same quiet look. Edit My Lineup and elsewhere still say "Replay".
- **The tag at the top right** ("↺ −17:05 behind live", or a Final's "REPLAY · 48:10 in"): shows for 5 seconds when a replay starts and after every jump (Play from on the timeline, resuming from pause, Right back to a replay, Start over from a menu, a game's replay), then fades out (under half a second). Its 5 seconds start once the banner and the controls are down, and it never shows while either is up (the banner says how far behind itself). The next stretch of the same replay doesn't bring it back. "End of the game" keeps its place.

| Area | How to check | Pass |
|---|---|---|
| Banner | Tune CNN 40 min into a programme | No Start over button; "Hides in" as usual; OK opens the controls |
| Controls live | OK on CNN live | Pause, Rewind, then Subtitles…; no Start over |
| Controls behind | OK during a replay (LG and another remote) | Go live (Pause on LG), Rewind; no Start over |
| Guide chip | Focus ESPN (two-day copy), then a one-day-only channel | "↺ 2 days", then "↺ 1 day"; no chip on KCBS |
| Edit My Lineup | Focus ESPN there | Still "Sports · ↺ Replay" |
| Menu Start over | Hold OK on ESPN's programme begun 40 min ago › Start over; Live Events, hold OK on a live game › Start over, a Final › Watch replay | Plays from the start as before |
| Tag after a jump | Rewind, Play from 7:00 | Banner for its few seconds, then the tag for 5 s, then it fades; nothing in the corner afterwards |
| Tag hidden | OK (controls) or Info while it shows | The tag gone while they're up |
| Other jumps | Pause and play; Right back to a replay; the menu's Start over; a Final's Watch replay | The tag for 5 s after each (REPLAY · N in for the Final) |
| Long replay | Watch a replay across a stretch boundary (3 h) | The tag doesn't come back by itself |

## Build 169: 1.0.12, controls with icons and the replay bar

OK brings up a new row of controls. Every button is a round icon, in three groups with a thin line between them: what you're watching (Live when behind live, Pause, Rewind: only on channels with replay), this channel (Subtitles, Sources, Watch East or West when the channel has both, Cameras when a race has them), and elsewhere (Back to the last channel or game, Guide or Games, Favorite, Settings). Only the focused button opens into a pill with its name and what it's set to ("Subtitles English", "Sources Dream · 1080p", "Back to TNT", "Watch West", "Unfavorite", "Live"); the rest stay icons, so the row always fits and nothing is shortened any more. With the Magic Remote, the button under the pointer opens the same way. Left and Right move along the row, OK uses the button, Up and Down still change channel, and the colour keys work as before (Green turns subtitles on or off, Red opens Sources, Blue Settings).

Subtitles and Aa are now one Subtitles button. OK on it opens one panel: Off and the languages the stream carries (the one on ticked), then Look (size, style, letters, position) with a preview of a caption at the bottom, then Timing. A small green dot on the Subtitles icon says they're on.

The programme bar over the buttons (channels with replay) works the same live, with a line under it: "AC360 · 20 min in · 40 min left". Behind live the bar now runs from the start of the programme you're watching to live, and live is always the right end (a red dot and LIVE). Blue is how far you've got, with a white dot and the time under it ("7:21 PM"); grey is what's left to catch up on. A thin tick marks every programme that starts between you and live; only the first one is named (the same names as the banner's), cut short with "…" to fit before the next tick, and left out when there's no room. The paused panel's bar also ends at live; the Rewind timeline is unchanged.

| Area | How to check | Pass |
|---|---|---|
| Row live | OK on CNN live | Pause, Rewind, line, Subtitles (focused, "Subtitles Off" or the language), Sources, line, Back, Guide, Favorite, Settings |
| Row without replay | OK on a channel with no replay | No Pause or Rewind; Subtitles first |
| Row behind | OK during a replay (LG and another remote) | Live first and focused ("Live", red dot); Pause after it on LG only; Rewind |
| Final's replay | OK during a Final's replay from Live Events | The first button says "NBA TV live" (the channel's name) |
| East/West, cameras | OK on a channel with both versions; on a race with onboard cameras | "Watch West" (or East) after Sources; Cameras after it |
| Focus | Left and Right along the row | Only the focused button shows its name and state; the row never runs off the screen, "Back to" keeps its full name |
| Pointer | Move the Magic Remote over the buttons | The one under the pointer opens; moving right doesn't flicker between two buttons |
| Keys | Green, Red, Blue, Play/Pause with the row up | As before |
| Apple TV | Swipe along the row; click | One button per swipe; the click uses it |
| Subtitles | OK on Subtitles | One panel: Off, languages (✓ on the one on), LOOK with the preview, TIMING; picking a language turns them on; a dot on the icon while on |
| Green | Green from anywhere | Subtitles on or off, as before |
| Bar live | OK on CNN live | The show from start to end, white to now, "AC360 · 20 min in · 40 min left" under it |
| Bar behind | Rewind to 7:21 during Erin Burnett OutFront, OK on the controls | 7:00 PM at the left, LIVE at the right end, blue to the dot with "7:21 PM" under it, a tick at 8:00 named "AC360" |
| Long name | Replay from early afternoon (several shows behind) | A tick at every show's start; only the first named, cut with "…" before the second tick |
| No room | Replay from a few minutes before a show starts, close to live | The tick shows, no name |
| Paused | Pause during a replay | The panel's bar ends at live (red dot, LIVE at the right) |
| Rewind | FF or Rewind | The timeline exactly as before |

## Build 169: 1.0.12, sources start on the best working picture

After Test all sources, channels still started on a 720p copy ahead of a 1080p one. The picture made a tenth of a copy's score, so it never moved a copy by itself (a slightly faster 720p even won); the copy that played last stayed first whatever was measured since; and direct copies (Xtream) always went ahead of server-opened ones (Emby / Jellyfin). Now a channel starts on the copy with the best picture among those that work.

- **Works:** a measured copy works when its starts show a picture (at least 0.8 by (shown + 0.9) / (all + 1), a probe counting as half a play, so the one probe Test all sources makes is enough but one failure in four starts is not) and its median time to picture is 4 seconds or less.
- **Order:** the copies that work first, by measured picture: 1080p at 50 / 60 fps, then 1080p, then 720p at 50 / 60, then 720p, then lower (4K counts as 1080p). Within the same picture: faster and smoother first where clearly better, else the catalog's order. Copies that fail or start slowly go after them, as before.
- **Copies never measured** keep their places among themselves. A working copy moves ahead of one only when that one's name claims a lower picture (HD or SD below a measured 1080p; SD below a measured 720p). A name's size counts at its best (FHD as 1080p at 60), and a copy whose name claims nothing isn't passed.
- **Last played:** stays first only while no working copy has a better picture measured. "Always start with" (a pinned copy) still always goes first.
- **Direct before server-opened:** only within the same picture (or where nothing is measured): a direct 720p no longer goes ahead of a server-opened 1080p that works, and a direct copy that fails doesn't pass a server-opened one that works. A server-opened copy that played last leads the server-opened ones, as before, but not past a better picture.
- **Unchanged:** the backup from a separate service (Test all sources), copies this TV couldn't play going last, and the Sources panel's STARTS WITH and BACKUP tags, which read the same order.

| Area | How to check | Pass |
|---|---|---|
| 1080p first | A channel with a 720p copy and a 1080p copy, both working in Test all sources (the 720p faster) | Sources: STARTS WITH on the 1080p copy; it plays first |
| 60 before 30 | A channel with 1080p50/60 and 1080p30 copies, both working | The 50/60 copy starts |
| Failing 1080p | A 1080p copy that failed in Test all sources, a working 720p | The 720p starts; the 1080p is further down |
| Slow copy | A copy that takes over 4 s to a picture, another at 720p within 2 s | The fast 720p starts |
| Last played | A channel whose last play was on its 720p copy; then Test all sources finds its 1080p copy working | Next tune starts on the 1080p copy |
| Last played kept | Play a channel's 1080p copy, leave, tune back | The same copy starts |
| Always start with | Sources › Always start with the 720p copy | It starts first whatever is measured |
| Never measured | A channel nobody played or tested since the update | Same order as 1.0.11 |
| Server-opened 1080p | A channel with a direct 720p and an Emby 1080p that starts within 4 s | The Emby 1080p starts; with both at 720p, the direct one does |
| Backup | Three providers, two of them the same service underneath | BACKUP still on a copy from the separate one |

## Build 170: 1.0.13, Start over back in the controls

Start over is back in the OK controls as a round icon (a bar and a left triangle, |◀) in the watching group, right after Rewind: live, Pause, Rewind, Start over; behind live, Live (Pause after it on LG), Rewind, Start over. Focused, it reads "Start over · from 7:30" (the programme's start), or for a game "Start over · from 7:10" (its scheduled start); OK plays the replay from that minute, as the guide's hold-OK Start over does.

It's there only when it can work: the channel has replay, the start of what's on screen is known (live: the guide's programme on now; behind live: the programme at the moment you're watching; a game: its scheduled start once it began), that start is more than 3 minutes before the moment shown, and the usable archive still holds it (with its hour in hand). Otherwise the button isn't in the row at all. The tune banner stays as Build 169 left it (no Start over), and nothing else in the row changes.

| Area | How to check | Pass |
|---|---|---|
| Live | OK on ESPN (replay) 40 min into a programme from 7:30 | Pause, Rewind, Start over, line, Subtitles (focused)…; Start over focused reads "Start over · from 7:30" |
| OK | OK on Start over | The replay from 7:30; banner as after the guide's Start over; the tag for 5 s once it's down |
| Behind live | Rewind to 7:45 during the 7:30 programme, OK | Live, Rewind, Start over ("from 7:30"); LG: Live, Pause, Rewind, Start over |
| Just begun | OK within 3 min of a programme's start (live, or a replay 2 min into one) | No Start over; Rewind is last in its group |
| No listing | OK on a replay channel with no guide data for now | No Start over |
| No replay | OK on CNN (no archive) | No Pause, Rewind or Start over |
| Outside archive | Replay on a one-day channel from 23 h back, into a programme that began earlier | No Start over |
| Game | OK on a game from Live Events that began at 7:10 (its channel has replay) | "Start over · from 7:10"; OK plays from tip-off |
| Banner | Tune ESPN 40 min into a programme | No Start over on the banner; "Hides in" as in 1.0.12 |

## Build 170: 1.0.13, replay plays the moment picked

On some channels a replay started earlier than the moment picked. An Xtream provider's replay link asks for a moment in the provider's own clock, and the app took that clock from what the server says about itself. Some channels' archives are kept on another clock (a channel's guide in another time zone, or a different machine behind the same address), so the moment asked for was off by the difference, usually an hour.

Now each channel's own guide with its past (the one Rewind's timeline reads) also tells the clock its archive is on: every programme in it has its start written in the provider's local time next to the exact moment, and the difference between the two, to the nearest quarter hour (the most common one when they disagree), is that channel's archive clock. Replay links for the channel ask in that clock. A channel whose own guide hasn't been read yet uses the clock most of the same provider's channels showed; a provider none of whose channels has been read yet uses what its server says, as before. What was learned is kept, so it holds after the app restarts.

While nothing has been learned for a provider yet, the channel's guide is read in the background as soon as its live picture shows, so a replay rarely has to wait. If it hasn't come in when a replay starts, the replay waits for it up to 3 seconds, then plays in the server's clock; the guide that comes later serves the next replay.

Test all sources reads the guide of every channel with replay just before testing its archive, so its archive is tested in its own clock and every replay channel's clock is known up front. A provider that refuses that request is paused as it is for a refused test. When a provider is done, its playback log has one line in words, such as "Dream: archive clocks: UTC+1 on all replay channels; server says UTC", or "UTC+1 (most), UTC (some)".

In the playback log, each replay line says the clock used. "archive clock UTC+1" means the channel's own clock (or the provider's learned one); the first replay on a channel whose archive clock differs from the server's adds "(server says UTC)". "server clock UTC−5" means nothing was learned and the server's own was used. The safety margin of an hour at the archive's far end is unchanged.

| Area | How to check | Pass |
|---|---|---|
| Picked moment | On a news or sports channel with an on-screen clock, on each provider: Rewind, pick a time 30 minutes back | The on-screen clock shows the time picked (within a minute), on every provider |
| Start over | Menu › Start over on a programme that began 20 minutes ago | It opens at the programme's start, not an hour before or after |
| Sports channel | The same on a Dream sports channel that played too early in 1.0.12 | The moment picked |
| Log | Playback log after a replay on a Dream sports channel | The first replay line names the clock found, with what the server says when they differ ("archive clock UTC+1 (server says UTC)"), later ones the clock alone; no links |
| Same clock | A channel whose archive is on the server's clock | "archive clock UTC" with nothing after it |
| First replay | Fresh install, add a provider, tune a replay channel, Start over within a few seconds of the picture | It starts within about 3 seconds, at the right moment |
| Restart | Quit and reopen, replay on a channel never rewound before on that provider | It starts at once, at the right moment; the line says "archive clock" |
| Test all sources | Run it with a provider whose channels keep replay | Its "done" line is followed by "archive clocks: …" in words, no counts; replay worked as before or better |
| Second clock | After Test all sources, replay on a channel of the provider where the log said "(some)" | The moment picked |

## Build 171: 1.0.14, the game banner during a replay of a channel showing a game

A game watched through its channel (tuned from the guide, or a channel showing a game) kept the game banner's live look during a replay: the banner chose the game by the clock now and only treated it as a replay when the game itself had been tuned from Live Events. The banner now chooses the game on at the replay's minute, and any replay behind live shows the replay version (REPLAY and how far in, no clock status, "Live now" in one muted line). Rewound to before the game began, the channel's own replay banner shows instead.

| Check | How | Pass |
|---|---|---|
| Game on its channel | Tune a channel showing a live game from the guide, then Rewind or Start over | Banner: REPLAY · "N min in", no LIVE, "Live now" line |
| Game tuned from Live Events | Start over from the hold-OK menu | Same as before (REPLAY) |
| Before the game | Rewind the channel to before the game's start | The channel's replay banner with the show on then |
| Caught up | Go live | The game banner as before |

## Build 172: 1.0.15, replay lands on the minute picked

Providers keep their archives on different machines. Asked for a moment, some start the replay at exactly that minute; others start it at the beginning of the chunk the moment falls in (10, 30 or 60 minutes, say) or at the start of the programme; and on some channels the recording runs a minute or two off the clock its programme table implies. So on some channels a replay started further back than the time picked. Now each channel's archive is measured once, and every replay is adjusted for it, without anything to set.

- **The measurement.** For a channel with replay, the app reads its live picture and two moments of its archive about two hours back, eleven minutes apart (a few seconds each), and compares the timestamps inside the video. Where the provider keeps the broadcast's own timestamps, that tells where each archive request really starts: the same lag behind every moment asked is an exact archive (and the lag, to the minute, is the recording's offset); starts that fall on the same boundaries are a chunked archive, of the size every start fits (a third short read when two sizes fit); starts on the programmes' starts are programme-aligned. The second read also asks for a byte range, to learn whether the archive can be entered part-way (seekable), and the reads give its bytes per second. Timestamps that start near zero or can't belong to the broadcast are "rewritten": that channel is left as it was. What was learned is kept with the archive clocks (archive-clocks.json, ids and numbers only) and measured again by the next Test all sources.
- **When.** Test all sources measures every channel with replay once its archive played, in a turn of its own, with the same gaps between tests, the same pause when a provider refuses, and the same saved progress. A channel never measured is measured the first time it is replayed, within the few seconds Build 170 already allows for the archive clock; if that isn't done in time, the replay starts as it did before and the next Test all sources fills it in.
- **The replay.** Exact: as before, asked a minute later where the recording runs a minute behind. Chunked or programme-aligned: the app asks for the chunk and starts the player at the minute inside it. Where the provider answers byte ranges, it asks for the stream from about the right byte (from the bytes per second), checks the first picture's time there and asks again nearer if it's off; where it doesn't, it reads the stream through and drops what comes before the minute, when the minute is at most 4 minutes into the chunk (and for at most 12 seconds). Further into a chunk that can't be entered, the replay plays from the chunk's start, and the banner, the bar, the tag and the controls say so ("Watching from 8:00 PM"). Whatever happens, the time shown is where the picture really started.
- **How the player starts inside.** A small relay in the app (127.0.0.1, the same code on LG, Android / Fire TV, Apple TV and Mac) reads the provider's link and hands the player a stream already starting at the minute, tables first. No player is asked to seek or to send a byte range itself: VLC (Apple TV, Mac) can't pass one, ExoPlayer replaces it on its own requests, and LG's player reads it through the LG relay like any other stream. The relay takes plain http links (as Xtream archives are); an https archive plays from its chunk, said honestly.
- **The Rewind timeline on a chunked channel that can't be entered.** The playhead moves in the channel's chunks: Left and Right go from one start to the next (held, two, then four at a time), Up and Down go to a programme's start and then to the start the replay really has at or before it, and a click with the pointer snaps to the nearest start. The bar shows faint ticks at those starts, and the bubble says the real start ("8:00 PM"). Where the app can read through, each start also allows the next 4 minutes. Start over on such a channel goes to the start at or just before the programme's start (or the programme's start itself when it is within those 4 minutes); the controls' "Start over · from …" says that time. Exact and seekable channels keep minute steps as before.
- **The playback log** ("replay" lines, never a link). Once per channel, how its archive starts: "archive: exact", "archive: 30-min chunks, skips to the minute", "archive: 60-min chunks, reads through up to 4 min", "archive: 60-min chunks, no skip", "archive: recording 1 min behind", "archive: timestamps rewritten, not measured". Each replay says what was asked: "rewind to 20:21 (chunk 20:00, skipped 21 min)", "rewind to 20:25 (chunk 20:00, no skip)", "rewind to 20:21 (recording 1 min behind)", then the clock as in Build 170; "it starts at 20:19" when the stream began elsewhere than planned. Test all sources adds one line per provider when it's done: "Dream: archive timing: exact on all replay channels", or "exact 80%, 30-min chunks (skips to the minute) 20%", in shares, never counts.

| Check | How | Pass |
|---|---|---|
| News clock, each provider | Test all sources; then on a news channel with an on-screen clock, on each provider: Rewind to a time 40 minutes back, and to 2:21 past an hour | The on-screen clock shows the minute picked (within a minute), on every provider |
| Sports channel | The same on a sports channel of the provider that started too early before | The minute picked; the log's first replay line for it says how its archive starts |
| Chunked, can't enter | On a channel whose log says "no skip": Rewind | Left / Right jump between chunk starts; faint ticks on the bar; the bubble and, after OK, the banner say the chunk's start ("Watching from 8:00 PM"); the picture matches |
| Read through | On a channel whose log says "reads through": Rewind to 3 minutes past a chunk start | Plays that minute after a few seconds more than usual |
| Start over | Controls › Start over on a programme that began 40 minutes past the hour, on an exact channel and on a "no skip" one | Exact: from :40. No skip: from the chunk start before it, and the button said so |
| First replay | Fresh install, no Test all sources: Rewind on a channel with replay | Starts within about 3 s; the log says how its archive starts (or the next replay does) |
| Log | Playback log after a few replays | "archive: …" once per channel, "rewind to … (chunk …, skipped … min)" lines; no links |
| Test all sources | Run it | After each provider's "archive clocks" line, "archive timing: …" in words and percentages; no counts |
| Other platforms | The news clock check on Android / Fire TV and Apple TV | Same minute as on LG |

## Build 173: 1.0.16, replay's hour comes from the video itself

On some providers every replay played exactly an hour before the time picked: rewound to 9 PM just before 10, CNN and Fox News showed 8 PM on their own on-screen clocks. Those providers' servers say their clock is UTC, and each channel's own guide with its past (Build 170) says the same, but the archive behind them runs an hour ahead (UTC+1, say). Asked for 9 PM in UTC, it served what it calls 9 PM, an hour earlier. Build 172's measurement could have seen that, but only once a channel had been measured: on a first replay it had less than three seconds for a live read and two archive reads two hours back, which rarely finished in time; the replay then played in the wrong clock and the channel wasn't tried again that run. Where it did finish, the hour was kept as that one channel's "recording 60 min behind", none of the provider's other channels learned from it, and an archive running behind what was asked was taken as "timestamps rewritten".

Now the hour comes from the video. The app compares the time inside the live picture with the time inside the archive's picture: when the archive starts a whole number of quarter hours (to within a minute) before or after the moment asked, anywhere from 15 minutes to 14 hours, that is the archive's clock, not the recording running late. It becomes the channel's archive clock, over what its guide or its server says, and the provider's other channels take it at once: a channel whose own guide said the same wrong clock is moved by it too, a channel whose guide says something else keeps that, and when the provider's measured channels disagree the clock most of them showed is used. A lag of a few minutes is still the recording's offset, as in 1.0.15.

- **The first replay on a provider.** The app reads the channel's live picture and one moment of its archive near the time picked (7 minutes past a quarter hour, where no chunk of any size starts), up to the first picture of each, and starts the replay in the clock they show, all within 4 seconds. If they don't tell (a chunked archive, a programme's start) the reads carry on as in 1.0.15 within the same time. If nothing is in by then, the replay plays as before; on an account that allows a second connection the reads carry on in the background for the next replay, and otherwise the next channel's first replay measures.
- **The next channels.** Once one channel on the account has been measured, its others start at once in the learned clock; where every archive measured on that account starts at the minute asked, they don't wait for their own timing either.
- **What was kept before.** Clocks kept by 1.0.13 and 1.0.15 came from the channels' guides and are kept as such; a measurement replaces them. A 1.0.15 "recording N min behind" of a whole number of quarter hours was the clock, not the recording: it is dropped and the channel is measured again. archive-clocks.json keeps the measured clocks apart from the guides' ("measured"), ids and numbers only.
- **Test all sources.** Its timing turn (two hours back, as before) finds the same clock and sets it for the channel and its account, also when the archive is on a clock behind what was asked or keeps its files in chunks on another clock. The provider's "archive clocks" line says the measured clock ("Dream: archive clocks: UTC+1 on all replay channels; server says UTC").
- **The playback log.** The first replay on a channel in a measured clock says so: "rewind to 21:00; archive clock UTC+1 (measured; server says UTC)", or "archive clock UTC (measured)" where the server was right; later ones the clock alone. No links, as always. Nothing on screen changes except that the times are now right.

| Check | How | Pass |
|---|---|---|
| CNN, first channel | Fresh install (or 1.0.15's saved clocks), no Test all sources. On each provider, CNN with replay: Rewind to the hour before the last (9:00 PM just before 10) | The on-screen clock shows 9:00 (within a minute), not 8:00; the replay starts within about 4 s |
| Fox News, second channel | Straight after, on the same provider: Fox News, Rewind to 9:30 PM | The on-screen clock shows 9:30; it starts at once, with no extra wait |
| Same, other provider | Both checks on each other provider with replay | The time picked on every provider |
| Log | Playback log after the CNN replay on the provider that played an hour early | "archive clock UTC+1 (measured; server says UTC)" once; the Fox News line says the same once; no links |
| Restart | Quit and reopen, Rewind on CNN | The time picked, at once, no wait |
| Small offset | A channel whose 1.0.15 log said "recording 1 min behind" | Still the minute picked; the log still says "recording 1 min behind" |
| Chunked | A channel whose log says "60-min chunks", Rewind 2:21 past an hour | As in 1.0.15 (the minute picked where it skips, the chunk's start where it can't) |
| Not in time | A provider slow to answer, first replay | Plays after about 4 s as 1.0.15 did; the next channel's (or, with two connections, the next) replay is right |
| Test all sources | Run it, then check its log | Each provider's "archive clocks" line names the clock measured; replays afterwards land on the time picked on every channel |
| Other platforms | The CNN and Fox News checks on Android / Fire TV and Apple TV | Same as on LG |

## Build 174: 1.0.17, replay clock checks that keep trying

On the LG TV, 1.0.16's check of a provider's archive clock never once succeeded, and nothing said why. On the account whose server says UTC while its archive runs on UTC+1 every replay still started an hour early (9 PM picked, 8 PM played); Test all sources said its replay worked 0% while the TV played the same kind of replay links fine. The check reads the archive with the app's own HTTP client, not the player's path, and that client behaved differently. On LG it refused, before reading a byte, any answer that said it was bigger than the read's limit: an archive stretch says its whole size (an hour of video, gigabytes), a live stream says none, so live reads worked and every archive read failed, in the check and in Test all sources alike. It also named itself with another app's agent where the player's path sends a browser's, it asked for a compressed body, it wouldn't follow an https link that redirects to http, it decided from the first piece of the answer alone whether it was a stream at all (a few bytes first, or a stream whose first packet starts further in, and it gave up for good), and on a first replay it had about two seconds for the archive after the live read. A failed check was then never tried again until the app restarted.

- **Asked as the player asks.** The check's reads (and Test all sources' replay probe) now read an archive whatever size it says (up to what they need), send the player's agent, ask for no compressed body, follow an https to http redirect as the player does, send the archive no headers the player doesn't, tell what an answer is from its first 4 KB together, and follow an HLS answer to its media (an archive's first segment). The replay probe in Test all sources waits up to 20 s for its picture (an archive the server builds first), and its timing turn has about 12 s a read.
- **Every check in the log.** Each check says how it ended, on the copy's provider: "clock check: UTC+1 from the video (server says UTC)", or why not and what happens next: "clock check: no answer in 3 s (archive); replays use the server's UTC for now; checking again in the background in 2 min". The reasons: no answer in N s, http NNN, not a video stream (text/html, playlist, empty, unknown data...), no picture, out of time, refused, no connection, a redirect it couldn't follow, secure connection refused; "(live)" or "(archive)" says which read. Never a link, host, path or login.
- **It keeps trying, in the background.** A failed check is retried per account, not once a run: after 2 min, then 10 min, 30 min, then hourly, also after a restart (archive-clocks.json keeps how many failed in a row and when, numbers only). A background check reads one channel of an Xtream account with replay whose clock no video has shown yet (or whose last check failed), about a minute and a half after the lineup is in and then on that backoff, one at a time, with up to a minute for its reads. On an account that allows one connection it waits until nothing of that account is playing, and one already reading stops at once when you tune that account; not during Test all sources. Its end is in the log: "clock check (background): UTC+1 from the video (server says UTC); the account's replays ask in it now". What it finds teaches the account's other channels, as in 1.0.16.
- **The first replay still starts quickly.** It waits at most 5 s for its own check. If the check fails, the replay starts at once.
- **The copy trusted most replays.** Where a channel's copies on several providers reach the moment picked, a replay plays first on a copy whose clock the video has shown, then on one whose provider's checks never failed, then the rest; within each, as before (the longest archive, and so on). A copy whose archive doesn't reach the moment is never picked. The log says it once when this changed the provider: "replay from Trex: its clock is confirmed; Dream's isn't yet", or "replay from Trex: Dream's clock check failed". Nothing on screen names a provider.

| Check | How | Pass |
|---|---|---|
| CNN, the provider an hour early | Fresh start of the app. Watch CNN live a minute, then Rewind to 9:00 PM (the hour before last) | The on-screen clock shows 9:00 (within a minute), not 8:00; it starts within about 5 s |
| Its log | Playback log, the lines after that replay | A "clock check: …" line on that provider: either "UTC+1 from the video (server says UTC)", or a reason and "checking again in the background in 2 min". If the check failed and another provider has CNN, "replay from <other>: <provider>'s clock check failed" and the replay line on the other provider |
| CNBC, a few minutes later | Leave the app on any channel of another provider (or on the guide) 3–5 minutes, then CNBC: Rewind to 9:30 PM | The on-screen clock shows 9:30; the log has "clock check (background): UTC+1 from the video (server says UTC); the account's replays ask in it now" before it, and the replay line says "archive clock UTC+1 (measured; server says UTC)" |
| Still failing | If the background line gives a reason instead | Note the reason (it says what came back: "http 403 (archive)", "not a video stream (text/html) (archive)", "no answer in 20 s (archive)"...); "checking again in 10 min", then 30 min, then 1 h |
| One connection | On an account that allows one connection, keep watching one of its channels 5 minutes | No "clock check (background)" line for it while it plays; one comes within a minute or two after switching to another provider's channel |
| Other provider right | FOX News on the provider whose server says UTC+1: Rewind to 9:00 PM | 9:00 on screen; its own clock check line says UTC+1 |
| Restart | Quit and reopen; Rewind on CNN | The time picked, at once; no new clock check for that provider once it was measured |
| Test all sources | Run it | The provider's replay result is no longer 0% where the TV plays its replays; a provider whose replay clock couldn't be read says why once: "Dream: a replay's clock couldn't be read (…)" |
| No links | Export the playback log | No link, host, path or login in any "clock check" or "replay" line |
| Other platforms | The CNN check on Android / Fire TV and Apple TV | Same as on LG |

### Build 174: Test all sources carries on where it stopped

A test stopped part way (Stop, or the providers left turned it away) keeps its place for a week. Its results page reads "Your providers, ranked so far", "Stopped at 62% today, 7:51 AM · about 1 h 20 min left; carry on any time this week", each provider with its share tested under its name, and the buttons Carry on, Keeping one or two, Done, Start over. Carry on tests only what's left and gives a provider that refused another try; Start over tests everything again. Settings › Providers says "Stopped at 62% · carry on any time".

| Check | How | Pass |
|---|---|---|
| Stop and carry on | Start Test all sources, Stop it after a few minutes, open it again | "Stopped at N%"; Carry on is focused; Carry on resumes at about N% (log: "carried on from N%"), not 0% |
| Across a restart | Stop it, quit and reopen the app | Settings line "Stopped at N% · carry on any time"; Carry on still there |
| Start over | Stop it, then Start over | It starts from 0% |
| Refused | A provider that turned the test away | The run ends "ended part way (providers turned it away)"; Carry on tries that provider again |
| A week on | A test stopped more than a week ago | No Carry on; log: "a test stopped more than a week ago was dropped" |

## Build 175: 1.0.18, Rewind doesn't pause before a time is picked

Opening Rewind used to hold the picture at once, before any time was picked. Now what's playing (live or a replay) keeps playing, dimmed, under the timeline; the timeline's mark follows it until you move. Only picking a time changes what plays. Back, or OK without moving, closes the timeline and it plays on (a paused replay still plays on from where it paused).

| Check | How | Pass |
|---|---|---|
| Live | Watching live, open Rewind | The picture keeps playing under the timeline; the mark stays at live |
| Back | Then Back | The timeline closes; live plays on with no new start (no black, no reconnect) |
| Pick a time | Open Rewind, move left, OK | The replay starts at the time picked |
| In a replay | In a replay, open Rewind and wait 10 s | It plays on; the mark moves with it; OK without moving keeps playing from there (no jump back) |
| Paused | Pause, then open Rewind, then OK | Plays on from where it paused |
| Other platforms | Android / Fire TV, Apple TV | Same |

## Build 176: 1.0.19, Live Events by day

Live Events gets a day button, "Today ▼", beside Filter (Up from the first row; Left / Right between the two, as the guide's "Now ▼"). OK opens GO TO, "A day of games", on the left: the past days as far back as your channels keep replays, Today (checked while shown), then only the days ahead the schedule has games for. A past day says "Replay" when all of it is still in an archive, "Replay on some" when only part of it is. The note at the bottom: "Past days go back as far as your channels keep replays."

- **A past day's board.** Its games are fetched when the day is picked (one request per league that can be asked for a date, a moment apart, never in the background), with their finals and scores from that day's own answers; they're kept for the rest of the session (picked again within the hour: no new request). While they come: "Getting Monday's games…" under the title. If they can't be had: "Monday's games couldn't be loaded." and Try again. The date is at the top right ("Monday, Nov 16"), no live picture, no scores line. Rows: Your teams, then each sport; no Live now, Starting soon or Later today. In each row the games you can watch from the start come first, the rest dimmed at the end. A tile's footer: the start time and the channel with the replay mark; "replay ends in 2 h" (amber) when its replay leaves the archive within three hours; dimmed ones say "No replay" or "Not on your channels". The focused game: the matchup, Final with the score (as your score settings show it) and the venue, "On ESPN, 5:15 – 8:31 PM · replay until tomorrow 4:15 PM", and Watch from the start. OK plays it from its start (as Watch replay on a Final's card); on a dimmed game OK opens its card.
- **Which games have replay.** The same rule as today's Final: a guide channel whose listing had the game at its start and whose archive still holds it, the longest archive first. Where a game was at its start is kept for a week (replay-sources.json, ids, times and channel ids only), so a past day finds it after the guide has moved on; a game the app never saw while it was current (it wasn't running) can show "No replay" though a channel of yours aired it.
- **A day ahead.** That day's games from the week already fetched, by sport, as upcoming tiles. Nothing is asked for.
- **Back** on another day goes to Today first. Leaving Live Events and coming back shows Today.
- The log, once per fetch: "live events: Monday's games, 14 leagues, fine" (or "fine, 2 failed", "failed"). Never a link.

| Check | How | Pass |
|---|---|---|
| The button | Live Events, Up from the first row | "Today ▼" focused, left of the filter button; Right goes to the filter, Left back; Down returns to the games |
| The panel | OK on it | GO TO, "A day of games"; past days oldest first with "Replay" / "Replay on some", Today checked, then only days with games ahead; the note at the bottom; no counts, no source names |
| Archive depth | On a lineup whose longest archive is 2 days, in the evening | Two past days: the day before yesterday "Replay on some", Yesterday "Replay". No archive at all: only Today and days ahead |
| Yesterday | Pick Yesterday | The button reads "Yesterday ▼", "Getting Monday's games…", then the board; top right "Monday, Nov 16"; no picture in the corner; finals with their scores; replayable games first in each row, dimmed ones last with "No replay" / "Not on your channels" |
| Replay ends | A game on a one-day archive that started about 22 h ago | Its footer says "replay ends in 1 h" (or "in 40 min") in amber |
| Watch | OK on a replayable game | It plays from a minute before its start on the channel named; the player shows REPLAY |
| Dimmed | OK on a dimmed game | Its card opens (no Watch replay) |
| Scores off | Settings › Live Events, scores off; Yesterday again | No scores on tiles or in the focused game's line |
| Picked again | Back (Today), then Yesterday again within the hour | Shown at once; the log has no second "…'s games" line for it |
| Can't be had | Unplug the network, pick a past day not picked yet | "…'s games couldn't be loaded." with Try again; plug it back, Try again: the board |
| A day ahead | Pick Thursday | Thursday's games by sport, upcoming tiles; no request in the log |
| Back | On another day, Back | Today's board; Back again opens the menu as before |
| Close and reopen | On Yesterday, go to the guide and back to Live Events | "Today ▼" and today's board |
| Log | Export the playback log | "live events: Monday's games, N leagues, fine"; no link, host or login |
| LG | On the LG TV, pick a past day and scroll the rows | No pause in the UI while it loads; the board scrolls as today's |
| Other platforms | Android / Fire TV, Apple TV, Mac | Same |

## Build 177: ad marker check

A quiet diagnostic for one question: do the providers' streams carry SCTE-35 ad markers (the broadcast's own cues for its commercial breaks)? With them, a replay could one day skip a break exactly; without, it can't. This build only looks and writes what it found in the playback log. Nothing changes on screen and nothing is skipped. It reads only bytes the app already reads (no new request): the PMT says whether a stream for the markers is declared (stream type 0x86, or a "CUEI" registration), and the cues themselves (splice_insert, time_signal, splice_null) are counted where they come.

- **After a replay's clock check.** The "clock check" line of the replay log now ends with what its reads found: "; ad markers: none", "; ad markers: declared, no cue in the first seconds", or "; ad markers: 2 cues (splice_insert, out of network, break 120 s)". The check reads a few seconds of each stretch, so a cue is rarely in them; "declared" is the useful word. "(live: ...)" after it when the live read found otherwise, "(live only)" when only live was read.
- **After a replay that went through the replay relay.** When the replay ends or another replaces it: "replay: ad markers seen 3 times in 41 min (splice_insert, out of network)", or "ad markers declared, none seen in 41 min", or "no ad markers in 41 min" (minutes of video the relay passed on). The relay is in the path only for a stretch started inside an archive chunk (a chunked archive, the moment past the chunk's start, an http link), so most replays have no such line.
- **Live on LG through its relay.** Once a tune, when the LG relay reads the stream (subtitles on, Play through Marigold, HLS feeds): a "video" line "ad markers: declared (SCTE-35 in the stream's tables)" or "ad markers: none declared". Live played directly has none.
- **After Test all sources.** At the end of the run, before its summary, two lines per provider: "Dream: ad markers in replays: declared on 40% of copies tested, cues seen on 0%" and "Dream: ad markers live: declared on 0% of copies tested, cues seen on 0%". A copy counts as tested when a probe (or its timing turn) read its stream's tables. Percentages only.

| Check | How | Pass |
|---|---|---|
| Clock check | Fresh start; Rewind on a channel whose clock hasn't been checked (CNN) | The "clock check: ..." line ends with "; ad markers: ..." |
| Relay replay | On a chunked archive, Rewind to a minute well inside a chunk (the replay line says "skipped N min"), watch a few minutes, then Go live | A "replay: ad markers ..." line after "back to live", with the minutes watched (about) |
| Replaced | In that replay, Rewind again | The ad markers line for the first replay comes before "rewind to ..." |
| LG live | With subtitles on, tune a channel | One "video: ad markers: ..." line for the tune |
| Test all sources | Run it to the end (or Stop it) | Before "summary:", an "ad markers in replays" and an "ad markers live" line per provider tested, percentages only |
| No links | Export the playback log | No link, host, path or login in any ad markers line; no count of channels |
| Nothing else | Watch, replay, Rewind as usual | Nothing new on screen; replays play exactly as in 1.0.19 |
| Other platforms | Android / Fire TV, Apple TV, Mac | Clock check and Test all sources lines the same; the relay line where the relay was used; no "video: ad markers" line (no LG relay) |

## Build 177: Skip break

In a sports game's replay, the breaks (commercials, timeouts, the ends of periods, halftime) can be skipped, inside the controls you already have. No key changes, no setting, never automatic. It works where the app knows the game: a replay started from Live Events (a live game's Start over, a Final's Watch replay, a past day's tile), or a channel's replay whose listing at that moment is a game on the board (the game banner shows it). Football (NFL, college), basketball (NBA, WNBA, college) and hockey (NHL, college); not soccer, baseball, racing, tennis, golf, fights or cricket.

- **The data.** While such a replay plays (or opens), the game's play-by-play is asked for once in the background, from the same site as the scores, with their politeness: one request at a time, spaced, at most 120 a day, backed off after a failure, and a site that refuses (429, 403) the scores or these is left alone for the rest of the day. A game still on is asked again every 2.5 min while its replay is behind live. Nothing about you is sent. The answer (up to about a megabyte for football) is read on a worker; only the breaks are kept.
- **A break.** Two plays at least 75 s apart where the first is a timeout, the end of a period or half, a score, the two-minute warning, an injury or a review (or any two plays 150 s apart), from 10 s after the first to 10 s before the next, at least 45 s long. Halftime: after the second quarter (the first half in men's college basketball).
- **The tag.** When a break begins (or a jump lands in one), the top-right tag shows for its usual few seconds with a white BREAK chip: "about 2 min · OK to skip". A break that runs to live: "BREAK · live is in it too".
- **Skip break.** OK opens the controls on Skip break ("Skip break · about 2 min"), after Start over in the first group. OK on it plays from just before the next play (the minute at or before 10 s before it; on a channel whose archive comes in chunks, the start at or before that), with no banner. The notice: "Skipped a 2 min break" / "OK again to go back"; OK while it shows plays from where you were.
- **Near live.** In a break that runs to live there is no Skip break: the controls open on Live, as behind live.
- **Rewind.** The timeline's bar shows the breaks hatched, halftime named under its block, "Breaks" in the key line, and ▲ ▼ stop at the end of each break as well as at programme starts ("▲ ▼ end of a break or start of a programme, before / after"). The controls' replay bar shows the same blocks (with "in a break" under the moment during one); under a game's banner, as before, there is no bar.
- **No data.** A league without play-by-play, a game without times, or no answer: no tag, no button, no blocks.
- The log: "breaks: 7 found in NFL play-by-play" once per game per session (or "breaks: no play-by-play for MLB", "breaks: NFL play-by-play has no times"), "skipped a break (2 min 10 s)", "went back to the break". Never a link.

| Check | How | Pass |
|---|---|---|
| A break starts | A football game's replay from Live Events (Start over on a live game, or a Final's Watch replay); wait for a timeout or a score | The tag at the top right for a few seconds: BREAK chip with the double chevron, "about 2 min · OK to skip"; then it fades |
| The controls | OK during the break | The row opens on Skip break, after Start over, reading "Skip break · about 2 min"; Live (and LG's Pause) still first |
| Skip | OK on Skip break | The game resumes a few seconds before the snap; no banner; the notice "Skipped a 2 min break" / "OK again to go back"; the REPLAY tag after the jump |
| Go back | OK while the notice shows | It plays from where the break was; the BREAK tag again |
| Not too soon | Press OK twice quickly on Skip break | One skip; the second OK isn't taken as "go back" |
| Late in a break | Wait until under half a minute of the break is left, OK | No Skip break; the row opens on Live as behind live |
| Halftime | Skip at halftime | "Skipped a 15 min break" (or so); lands just before the second half's kickoff |
| Basketball / hockey | An NBA or NHL game's replay | The same at timeouts and between periods |
| A channel's replay | Rewind on a channel airing a game the board shows (the game banner shows it) | The same tag and Skip break |
| Near live | A live game about 1 min behind live, in a commercial | Tag "BREAK · live is in it too"; OK opens on Live; no Skip break |
| Rewind | Open Rewind during a game's replay | Hatched blocks on the bar, "Halftime" under its block, "Breaks" in the key line; ▲ ▼ stop at each break's end and at programme starts; "▲ ▼ end of a break or start of a programme, before / after" |
| Controls bar | OK during a channel's replay of a game (channel banner) | The same blocks on the bar over the buttons; "in a break" under the moment during one |
| Chunks | A channel whose archive comes in chunks (◀ ▶ "10 minutes, where this channel's replay starts") | Skip break plays from the chunk start at or before the next play, or isn't offered when that is before the break |
| No data | A baseball or soccer game's replay | No tag change, no button, no blocks; log "breaks: no play-by-play for MLB" once |
| Live game | Watch a live game's replay 10 min behind live through a few timeouts | Breaks newer than when the replay started show on the Rewind bar within about 3 min, and can be skipped |
| Log | Export the playback log | "breaks: N found in NFL play-by-play" once; "skipped a break (2 min 10 s)"; "went back to the break"; no link |
| LG | On the LG TV, a football replay | No pause in the picture or the UI when the play-by-play comes in |
| Other platforms | Android / Fire TV, Apple TV, Mac | Same |

## Build 178: 1.0.21, the background clock check reads a replay that works

On the LG with 1.0.20, Dream's clock never got confirmed: its background checks picked an obscure channel whose replay came back empty, and went back to the same one 30 minutes later, while Test all sources had already found which of Dream's replays play (38%). Now a background check reads a copy whose replay Test all sources saw play first, then lineup copies, then the rest; never one whose replay it saw fail; a different copy after each try (round again once all were tried). A provider whose replays all failed (or that keeps none) isn't checked (looked at again in an hour). One success confirms the clock for all the provider's channels.

| Check | How | Pass |
|---|---|---|
| Dream's clock | After Test all sources has run a while, leave the app a few minutes | A "clock check (background): UTC+1 from the video (server says UTC)" line on Dream, on a channel whose replay plays (not CANAL+ Sport 360 or another that failed) |
| A new one each try | If a check fails | The next names another channel |
| No replays | cf.hrshsauras (no working replays) | No background check lines for it |
| Dream-only channel | Rewind on a channel only Dream has | The on-screen clock matches the time picked |

## Build 179: 1.0.22, Rewind names what the guide does

On KNTV the guide showed Jazz at Clippers while the Rewind bar named "49ers Now" and "Dubs Talk" (NBC Sports Bay Area's shows). The guide picks a channel's listings by its identity (the schedule most of its copies agree on, Build 154); Rewind took the copy whose guide reached furthest back, whole, and one of KNTV's copies has NBC Sports Bay Area's guide filed under it. The replay copy's own list (its provider's archive table) was mixed in the same way. Now the Rewind bar starts from what the guide shows for the channel; another copy's schedule only fills what it lacks, and only when it is the same schedule where both have listings (the same titles at the same times, within 10 min); the replay copy's own list likewise. When the copy's own list names other shows, the log says so on its "past listings" line.

| Check | How | Pass |
|---|---|---|
| KNTV | Watch KNTV, open Rewind | The bar names what the guide shows (tonight's game, the news before it), not 49ers Now / Dubs Talk |
| Its log | Playback log after opening Rewind | "past listings: … ; they name other shows than the guide, so the guide's are used" when the copy's own list disagrees |
| The picture | Play from a moment on KNTV's bar | It's KNTV's programme (if it's NBC Sports Bay Area's, that copy's archive is another channel: say so) |
| Others | NBC Sports Bay Area, a few locals and news channels | Their bars match their guides |

## Build 180: 1.0.23, a replay that stalls is reconnected, and every replay is measured

Before, once a replay showed its picture nothing watched it: a player that buffered forever or a picture that froze stayed that way, with no message and nothing in the log. Now a replay playing is watched as live is (every platform: it's the engine's):

- **A stall.** Buffering for 10 s (live's stall time), the picture frozen while the sound plays for 6 s (the player's frame counter, or the LG relay's picture check), or the player neither playing nor showing a picture (its position standing still) for 8 s. From the third second the banner comes up with "Buffering…" and its spinner where "Hides in" shows, exactly as live does; the picture stays where it is.
- **Reconnect.** The first stall reopens the same copy at the moment showing (the stream is closed first, so a one-connection account isn't refused). "Buffering…" stays until the picture is back.
- **Another copy.** A second stall within 3 min of a reopen, or a reopen that shows no picture in 10 s, moves to another copy of the channel whose archive holds the moment (the usual order). With none, the same copy once more; if it still stalls, live with "Replay isn't available right now on this channel".
- **Goes on by itself.** A stall that clears before that isn't acted on; one of 3 s or more is counted.
- **Never** while paused, while the replay waits for its archive clock, or while a clock check holds the account's connection.
- **Figures.** Every replay ends (Go live, caught up, Rewind again, another channel, the app leaving, a failure) with one summary line. Each copy's record (feed-scores.json) keeps its replays apart from live: starts, start failures, time to picture, stalls and their seconds, minutes watched. The daily "providers" lines add how a provider's replays went. When two copies of the same archive length and clock standing both have three replays or more, one whose replays clearly go better (start failures plus stalls an hour) is picked first.

The playback log ("replay" lines; never a link, a host or a login):

- `stalled 10 s (buffering); reopened from 20:42; server clock UTC−5` (or `(picture frozen)`, `(playback stopped)`)
- `stalled again; another copy from 20:42; ...` / `the reopen showed no picture (no picture after 10 s); another copy from 20:42; ...`
- `stalled again; no other copy; reopened from 20:42; ...`, then `failed: the archive keeps stalling; back to live`
- `buffered 4 s, went on by itself` (at most one a minute; `picture froze 4 s, ...`, `stopped 4 s, ...`)
- `summary: Dream → Trex, 34 min watched, started in 2.1 s, 2 stalls (17 s), 1 reopen, 1 other copy` (`no picture` when none came)
- `replay from Trex: its replays go better than Dream's (start failures 0% vs 40%, stalls 0.5/h vs 3.0/h)`, once per pair, when the figures chose the copy
- providers: `...; replay: starts in 2.4 s median, start failures 12%, stalls 1.3/h`

| Check | How | Pass |
|---|---|---|
| A stall | During a replay, unplug the TV's network for about 15 s, then plug it back | The banner comes up with "Buffering…" and its spinner (where "Hides in" shows) after about 3 s; the replay reconnects by itself at the same minute; log `stalled 10 s (buffering); reopened from HH:MM` |
| Frozen picture | A replay whose picture freezes while the sound goes on (if one does) | After about 6 s, the same reconnect; log `stalled 6 s (picture frozen); ...` |
| Second stall | Unplug again within 3 min of the reconnect | Another copy of the channel takes over at the same minute (on a channel with two providers' archives); log `stalled again; another copy from HH:MM` |
| No other copy | The same on a channel only one provider keeps | `stalled again; no other copy; reopened from ...`; a third stall goes live with "Replay isn't available right now on this channel" and `failed: the archive keeps stalling; back to live` |
| Short blip | A network blip of 3-5 s | "Buffering…" for a moment, no reconnect; log `buffered 4 s, went on by itself` (not more than once a minute) |
| Paused | Pause a replay for a few minutes | No "Buffering…", no reconnect; play resumes as before |
| Summary | Watch a replay a few minutes, then Go live (also: Rewind again, change channel) | One `summary: ...` line per replay, after "back to live" (before the new "rewind to ..."), with the provider, minutes watched, time to picture, stalls, reopens, other copies |
| Providers | Export the log the next day (or export now: the export writes them) | Providers with replays have `; replay: starts in N s median, start failures N%, stalls N/h` |
| Choice | After several failed replays on one provider's copy (Dream), Rewind on a channel both providers keep for as long | The other copy is used; `replay from Trex: its replays go better than Dream's (...)` once |
| No links | Export the playback log | No link, host, path or login in any of these lines |
| Live as before | Watch live, unplug briefly | Live's own "Buffering…", "stall", "recovered by itself" as in 1.0.22 |
| Other platforms | Android / Fire TV, Apple TV, Mac | The same lines and the same "Buffering…" |

## Build 181: 1.0.24, This feed is wrong…

A feed (a copy of a channel) that is wrong can be set aside for that channel, with why. Hold OK on a feed in Sources (Red key) for a small menu: Play this feed, Always start with this feed, This feed is wrong…. The reasons: It's a different channel, Its replay is a different channel, Wrong city or feed, Wrong language, Its listings don't match what's on, It doesn't work, Something else. "It's a different channel" asks which one (optional): the channels whose guide shows now what this feed's own listing has on, Find the channel…, or Not sure. A channel named takes the feed as one of its own.

Set aside, a feed isn't played, replayed or used for listings on that channel (the guide's choice, Rewind, Live Events' replays); "Its listings don't match what's on" keeps it playing and only leaves its listings out. Only that channel: the same feed plays wherever else it belongs. It's kept across restarts and list reloads (found again by its provider name and group if the provider renumbers it). Each one is in the playback log ("set aside", "used again") and the channel report (set_aside, set_aside_reason, set_aside_is, set_aside_since).

| Check | How | Pass |
|---|---|---|
| The menu | Sources on KNTV, hold OK on a feed | A small card beside the panel: "FEED 3 · DREAM", Play this feed, Always start with this feed, This feed is wrong… |
| Play / pin | Play this feed; then Always start with this feed on another | The same as OK on its row; "Will start with this feed", and the row says "Always first" |
| Reasons | This feed is wrong… | "KNTV · FEED 3 · DREAM", What's wrong with it?, the seven reasons with their second lines |
| Which one | It's a different channel, on the copy showing 49ers Now | ON NOW, IT LOOKS LIKE: NBC Sports Bay Area, "On now: 49ers Now, as on this feed" (at most three); OR: Find the channel…, Not sure |
| Find | Find the channel…, type "bay" | The keyboard on the left, channels with "bay" in their name on the right, My Lineup first; OK picks one |
| Playing it | Set aside the feed playing | Back in Sources: "Feed 3 set aside for KNTV" / "Playing Feed 4 · OK again to undo"; the picture moves to Feed 4 at once; the feeds read Feed 1, Feed 2, Feed 4 |
| Undo | OK again right away | Feed 3 plays again and is back under WATCH WITH; "Feed 3 is back on KNTV" |
| The only feed | Set aside a channel's only feed | "It's the only feed, so it plays on · OK again to undo"; it keeps playing |
| A backup | Set aside a feed not playing | The picture re-opens on the same feed (a second); the feed is under SET ASIDE |
| SET ASIDE | Sources after setting one aside | At the end: "Feed 3", "It's NBC Sports Bay Area · Dream · today" in amber, "OK to use it again"; OK puts it back |
| Moved | Watch NBC Sports Bay Area after naming it | Its Sources list the feed; KNTV's don't |
| Listings only | Its listings don't match what's on, on a feed | It still plays; the guide and Rewind don't use its listings (Listings look wrong? no longer offers them) |
| Rewind | Rewind on KNTV after setting aside the copy with Bay Area's archive | The replay never plays on that copy |
| Restart | Quit and open the app again; reload the providers | Still set aside, still under SET ASIDE and in Settings |
| Settings | Settings › Help › Feeds you set aside | "KNTV · Feed 3", the reason · provider · date, "Use again"; OK uses it again. None: "None set aside. Hold OK on a feed in Sources to set one aside." |
| Log | Playback log after setting one aside | `set aside · KNTV · Feed 3 · Dream · it's a different channel: NBC Sports Bay Area · its name "…" in "…" · on now: "49ers Now" · guide: "Jazz at Clippers"`; "used again" on undo. No link, host or login anywhere |
| Report | Channel report | The feed's row: set_aside "yes", set_aside_reason, set_aside_is (the channel named), set_aside_since (the date) |
| Counts | Every new screen | No number of channels anywhere |

## Build 182: 1.0.25, Sources in a replay: the copies that reach this moment

Before, Sources (Red key) during a replay showed the live feeds with the check mark on the live feed (on KNTV: the cf.hrshsauras feed, which keeps no replay at all, while the replay played from a Dream or Trex copy), and picking a feed left the replay for live. Now, while a replay is on (playing, opening or paused), Sources is the replay's:

- **Corner and title.** "REPLAY · 8:42 PM" (the moment showing, moving on as it plays) and the channel's name.
- **REPLAY FROM 8:42 PM WITH.** The channel's copies whose archive reaches that moment, in the order the replay picks them (clock trust, archive length, how its replays went, the copy playing, ...). Each is "Feed N" with its picture, its replay depth and the provider: "Playing now · 1080p 30 · replay 2 days · Dream" on the copy the replay plays (the check mark is there, never on the live one). A note where three replays or more tell: "replays held up well", or in amber "stalled in a replay lately" (start failures and stalls an hour clearly above the others, the same margin the replay's choice uses).
- **CAN'T REPLAY 8:42 PM.** The others, dimmed: "Live only · cf.hrshsauras" (no archive), "Its replay doesn't reach 8:42 PM · Dream" (archive too short), "You set its replay aside · Dream". OK does nothing on them; hold OK gives the live menu (Play this feed goes live on it).
- **Feed numbers** as the live panel has them (Feed 3 set aside: Feed 1, 2, 4); feeds set aside (Build 181) aren't in either section; SET ASIDE, ALWAYS START WITH and GUIDE are as before.
- **Picking a copy** carries the replay on there from the same minute; Sources closes, the player says "Replay on Feed 4" / "From 8:42 PM, where you were", and the REPLAY tag shows again. The copy already playing: nothing. If it shows no picture within the replay's start time (10 s, or 25 s started inside), or fails at once, the replay goes back to the copy before at the same moment (not another copy, not live): "Feed 4's replay didn't open" / "Back on Feed 2 from 8:42 PM".
- **Live** stays on the controls' Live button ("For live, press Live in the controls."). Outside a replay, Sources is as in 1.0.24.

**This feed's replay is wrong…** Hold OK on a row under REPLAY FROM … WITH: "FEED 2 · DREAM", Replay from this feed (as OK), This feed's replay is wrong…. Then "KNTV · FEED 2 · DREAM", What's wrong with its replay?: It's a different channel ("The replay shows another channel's programmes"), It's at the wrong time ("Off from the time picked, e.g. an hour early"), It doesn't play well ("Stalls, freezes or won't open"), Its programme names are wrong ("The Rewind bar names other shows"), Something else. Replay only, for that channel only: the feed still plays live there.

- Different channel, doesn't play well, something else: not replayed on that channel (the replay's choice, Live Events' game replays through it, its own archive table on the Rewind bar).
- Wrong time: the same, and it counts as a failed clock check of its account: the background check measures that account's clock again (2 min later, then the usual backoff).
- Programme names: it still replays; only its own archive table is left off the Rewind bar for that channel.
- The copy replaying: the replay carries on at once on the next copy that reaches the moment ("Feed 2's replay set aside for KNTV" / "Replay on Feed 4 from 8:42 PM · OK again to undo"); none: it plays on ("It's the only replay, so it plays on · OK again to undo"). Programme names: "Its programme names won't be used · OK again to undo".
- OK again (its row under SET ASIDE, "Feed 2 · replay", "OK to use its replay again") puts it back, and the replay moved off it plays on it again from the moment showing.
- In the live panel the feed's row says "replay set aside" in amber; Settings › Help › Feeds you set aside lists "KNTV · Feed 2 · replay".

The playback log (never a link, a host or a login):

- `replay: switched by you to Trex from 20:42; ...` (the clock and plan as every stretch's line)
- `replay: the copy you picked didn't open (no picture after 10 s); back to Dream from 20:42; ...`
- `summary: Dream → Trex, ...` (the copies in order, as in 1.0.23)
- `set aside · KNTV · Feed 2 · Dream · replay: it's at the wrong time · its name "US: NBC BAY AREA (KNTV)" in "|US| ✪ NFL TEAMS" · at 20:42 · archive clock UTC (server says UTC; not measured) · guide then: "Jazz at Clippers" · its own list then: "49ers Now"` (parts not known left out)
- `replay: its replay set aside; on to Trex from 20:42; ...` when the copy replaying was set aside; `replay: used again; back to Dream from 20:42; ...` on undo; `used again · ... · replay: it's at the wrong time, set aside today`
- The channel report: `set_aside_scope` (whole, listings, replay, replay-listings) after set_aside_since.

| Check | How | Pass |
|---|---|---|
| The KNTV case | KNTV live on the cf.hrshsauras feed, Rewind an hour, then Red | "REPLAY · 8:42 PM"; REPLAY FROM 8:42 PM WITH lists the Dream / Trex copies with the check on the one replaying ("Playing now · … · Dream"); cf.hrshsauras is under CAN'T REPLAY as "Live only · cf.hrshsauras", dimmed |
| Order | The same with two copies of the same archive length | The copy replaying first unless another's replays clearly went better; the order matches the replay's own choice (Rewind again picks the first) |
| Too short | Rewind past a day on a channel Trex keeps one day and Dream two | Trex's copy: "Its replay doesn't reach …" under CAN'T REPLAY |
| Notes | A copy with three or more replays | "replays held up well", or "stalled in a replay lately" in amber when it stalled or failed to start clearly more; nothing with fewer |
| Pick | OK on another copy | Sources closes; "Replay on Feed 4" / "From 8:42 PM, where you were"; the REPLAY tag shows; the picture is the same moment; log `switched by you to …` |
| Same copy | OK on the copy playing | Nothing happens |
| Doesn't open | Pick a copy whose replay won't start (or unplug the network right after picking) | After about 10 s: "Feed 4's replay didn't open" / "Back on Feed 2 from 8:42 PM"; the replay plays on the copy before, same minute; log `the copy you picked didn't open (…); back to …` |
| Paused | Pause a replay, Red, pick another copy | The replay plays on the picked copy from the paused minute |
| Live | Live in the controls during a replay | Live on the live feed as before; Sources is the live one again |
| Numbers | A channel with Feed 3 set aside (Build 181), in a replay | Feed numbers as in the live panel; Feed 3 in neither section, still under SET ASIDE |
| Hold menu | Hold OK on a replay row; on a CAN'T REPLAY row | "FEED 2 · DREAM": Replay from this feed, This feed's replay is wrong…; the CAN'T REPLAY row: the 1.0.24 menu (Play this feed goes live) |
| Replay set aside | This feed's replay is wrong… › It doesn't play well, on the copy replaying | "Feed 2's replay set aside for KNTV" / "Replay on Feed 4 from 8:42 PM · OK again to undo"; the replay goes on at the same minute on Feed 4; Feed 2 under CAN'T REPLAY "You set its replay aside" |
| Undo | OK again right away | "Feed 2's replay is back on KNTV"; the replay goes back to Feed 2 at the moment showing |
| Live keeps it | Live after setting its replay aside | Feed 2 still plays live; its row says "replay set aside" in amber; SET ASIDE "Feed 2 · replay", "OK to use its replay again" |
| Only replay | The same on a channel with one replay copy | "It's the only replay, so it plays on · OK again to undo"; the replay goes on |
| Wrong time | It's at the wrong time | As above, and a background clock check of that account runs again within a few minutes (log `clock check (background): …`) |
| Names | Its programme names are wrong | "Its programme names won't be used · OK again to undo"; it keeps replaying; the Rewind bar shows the guide's names only |
| Restart | Quit and reopen | Still set aside, as replay; Settings › Help › Feeds you set aside: "KNTV · Feed 2 · replay" |
| Log | Export the playback log | The lines above; no link, host or login anywhere |
| Report | Channel report | set_aside_scope "replay" (or "replay-listings") on the feed's row; older ones "whole" or "listings" |
| Other platforms | Android / Fire TV, Apple TV, Mac | The same panel, notices and log lines |

## Build 183: 1.0.26, Go live and Jump to in a replay

In a replay (behind live), the controls' first two replay buttons say what they do:

- **Go live** (was "Live"), focused: "Go live · 12 min behind", how far the replay is from live, to the nearest minute ("1 h 5 min behind" past an hour); it grows while paused. A finished game's replay: "Watch NBA TV live" (was "NBA TV live"), no minutes. Unfocused it is the red dot, as before.
- **Jump to** (was "Rewind"), with its own icon: a vertical line with arrows both ways. OK opens the same timeline, whose title is "Jump to · KNTV" with that icon.
- On live TV nothing changes: Pause, then Rewind with the circular arrow; the timeline opened from live says "Rewind".

| Check | How | Pass |
|---|---|---|
| Go live | In a replay, OK | The controls open on Go live: "Go live · 12 min behind" (the real distance); OK goes live as before |
| Behind grows | Pause a replay a couple of minutes, OK | The minutes have grown |
| Over an hour | Rewind past an hour, OK | "Go live · 1 h 5 min behind" (or "1 h behind" on the hour) |
| Final | A finished game's replay from Live Events, OK | "Watch NBA TV live" (the game's channel), no minutes |
| Jump to | In a replay, Right to the next replay button | "Jump to" with the both-ways icon; OK opens the timeline titled "Jump to · KNTV" with the same icon; Left / Right move earlier / later as before |
| Live TV | On live TV, OK, Right | Pause, then "Rewind" with the circular arrow; the timeline says "Rewind" |
| Other platforms | Android / Fire TV, Apple TV, Mac | The same labels and icon |
