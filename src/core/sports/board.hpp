// Live Events' board as a plain view model (the pure logic of
// LiveEventsBoard.swift and LiveEventGuide.swift: LiveEventsModel.recompute,
// describe, the tile's status texts, the hero, Where to Watch, the details
// card, the sports column and the in-player event guide). The UI draws what
// is here and computes nothing: every string and flag a tile needs is set.
//
// Rows: Your teams (with My Sports), Live now (whip-arounds first), one row
// per sport (My Sports order, else NFL, College Football, NBA, WNBA, College
// Basketball, MLB, NHL, MLS, Soccer, Fighting, Motorsport, Golf, Tennis, then
// others A-Z), More events, Time not listed, Event channels. Groups inside a
// row: Whip-around, Live, Final, Earlier, Starting Soon (within 1 h), Later
// Today, Tomorrow, weekday names, Shows, Postponed, Not in your channels.
//
// Strings never name a data source.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/types.hpp"
#include "core/model/channel.hpp"
#include "core/sports/network_map.hpp"
#include "core/sports/my_sports.hpp"
#include "core/sports/scores.hpp"
#include "core/sports/types.hpp"
#include "core/util/time.hpp"

namespace marigold::sports::board {

// MARK: Names

// "Rams" for "Los Angeles Rams", "Red Sox", "Man City" (a short name), a
// doubles pair as is.
std::string shortName(const SportsTeam& team, bool soccer);
// "Rams at Seahawks", "Arsenal vs Chelsea", or a card's own title.
std::string title(const SportsGame& game);
// Icon key for a sport (the tvOS symbol names): "football.fill",
// "basketball.fill", "soccerball", "figure.boxing", "sportscourt.fill"...
std::string symbol(std::string_view league);
// Whether black text reads better than white on a team color ("FFCB05").
bool isLight(const std::optional<std::string>& hex);
// Services that are an app, not a channel ("Streaming only").
bool isStreaming(std::string_view network);

// MARK: Whip-arounds

// A whip-around channel (NFL RedZone, MLB Strike Zone, soccer conference
// feeds): SpecialFeeds.Whip.
struct Whip {
  std::string group;   // row group: "NFL", "MLB", "Soccer"
  std::string shortName;
  std::string title;
  std::string subtitle;
  int minimumLive = 1;           // live games in the group before the tile shows
  bool sundayAfternoon = false;  // NFL RedZone: Sundays 1-8 PM Eastern only
  bool operator==(const Whip& o) const {
    return group == o.group && shortName == o.shortName && title == o.title && subtitle == o.subtitle &&
           minimumLive == o.minimumLive && sundayAfternoon == o.sundayAfternoon;
  }
};
// The whip-around a channel is, by its name; nullopt for anything else.
std::optional<Whip> whipNamed(std::string_view name);

struct WhipChannel {
  Whip whip;
  std::string channelID;
  std::string displayName;
  bool inLineup = false;
};
// Your channels that are whip-arounds (My Lineup first).
std::vector<WhipChannel> whipChannels(const std::vector<Channel>& entries, const std::set<std::string>& lineup);

// MARK: Input

// How a source's channel shows on a tile: its name, short name and logos.
struct ChannelLabel {
  std::string id;
  std::string name;
  std::string display;
  std::vector<std::string> logoUrls;
};
using ChannelFor = std::function<std::optional<ChannelLabel>(const EventSource&)>;

// A race session's top three for a podium line (race_data::RaceData::podium).
struct PodiumDriver {
  std::string code;                  // "VER"
  std::optional<std::string> color;  // team color hex
};

struct Settings {
  // "Show games not in my channels".
  bool showsUnwatchable = true;
  // Scores and the game situation; off still uses the live status.
  bool showsScores = true;
  // "8:00 PM" or "20:00".
  bool twelveHour = true;
};

struct Input {
  // The store's events (or the plain provider events before the first build).
  std::vector<SportsEvent> events;
  std::vector<catalog::EventFamily> families;
  MySportsPrefs prefs;
  scores::Statuses statuses;
  std::map<std::string, Seconds> finalSeen;
  std::vector<WhipChannel> whips;
  Seconds now = 0;
  time::Zone zone = time::Zone::device();
  Settings settings;
  ChannelFor channelFor;
  // Hooks for the cameras panel, race data and photos; unset = none.
  // A race camera feed (special_feeds::isCameraName), and the race it
  // belongs to (special_feeds::cameraRace; nullopt for cameras of no
  // particular race).
  std::function<bool(std::string_view)> isCameraName;
  std::function<std::optional<std::string>(std::string_view)> cameraRace;
  // A finished session's top three (RaceData.top3: race_data::RaceData::podiumHook).
  std::function<std::vector<PodiumDriver>(const std::string& kind, Seconds start)> podium;
  // A tennis player's saved photo URL by name (PlayerPhotos.photo:
  // player_photos::PlayerPhotos::photoHook).
  std::function<std::optional<std::string>(std::string_view name)> playerPhoto;
};

// MARK: View model

enum class Pill {
  none,
  live,          // "LIVE", filled
  liveOutlined,  // "LIVE", outlined (not in your channels)
  delayed,       // "DELAYED"
  onNow,         // "ON NOW" (a studio show)
  likelyOver,    // "Likely over"
  postponed,     // "POSTPONED"
  ended,         // the final clock ("Final", "Final/OT") or "Ended"
  timeNotListed, // "Time not listed"
  notStarted,    // "Not started" (past its start, not underway)
};

struct SideLine {
  std::string teamID;
  std::string name;       // as shown ("Rams")
  std::string fullName;
  std::string abbreviation;
  std::optional<int> rank;
  std::optional<std::string> country;  // a chip instead of the badge
  std::optional<std::string> color;
  std::optional<std::string> altColor;
  bool lightColor = false;  // black text on the color badge
  std::optional<std::string> logoUrl;
  bool home = false;        // "home" after the name
  std::optional<std::string> score;
  std::optional<bool> leads;  // false: dimmed score
  bool possession = false;
  std::vector<std::string> sets;  // tennis games per set
  // Tennis: a player photo in a circle instead of the badge (a country
  // chip still shows). The small size first, the full one if that fails;
  // `initials` until one loads; a doubles pair gets a two-person icon.
  bool photo = false;
  std::optional<std::string> photoUrl;
  std::optional<std::string> photoSmallUrl;
  std::string initials;
  bool doubles = false;
};

struct PartLine {
  std::string label;  // "Prelims", "Main", "FP1", "Race"
  Seconds start = 0;
  double fill = 0;    // 0..1
  bool current = false;
  bool timed = true;
  std::string text;   // "Main 10:00" (label alone when not timed)
};

struct Tile {
  std::string id;   // the event id
  std::string key;  // row id + "|" + event id: focus key
  SportsEvent::State state = SportsEvent::State::upcoming;
  bool watchable = false;  // false: dimmed
  bool studio = false;
  bool favorite = false;

  // Status row.
  Pill pill = Pill::none;
  std::string pillText;
  std::string statusText;    // "ends ~9:45", "started 7:30", "8:00 PM", "due 7:30", "was 8:00 PM", "ended 9:52"
  std::string statusDetail;  // "in 25 m", "in 2 h" next to an upcoming time
  std::optional<std::vector<bool>> bases;
  std::optional<std::string> tag;  // "World Series · Game 1", "RED ZONE", "STUDIO"
  bool tagAlert = false;
  std::optional<std::string> leagueChip;  // a mixed row with no tag

  // Middle.
  enum class Layout { sides, card, plain };
  Layout layout = Layout::plain;
  std::vector<SideLine> sides;  // top, bottom
  std::string title;
  std::optional<std::string> subtitle;
  std::string icon;
  std::vector<PartLine> parts;
  std::optional<std::string> partLabel;  // "Main Card · fight 3 of 5"
  std::optional<std::string> boutLine;   // "Now: Pereira vs. Hill"
  bool boutNow = false;
  std::vector<PodiumDriver> podium;
  std::optional<std::string> podiumLabel;

  // Footer.
  std::optional<std::string> channelName;
  std::optional<std::string> channelDisplay;
  std::vector<std::string> channelLogos;
  bool inLineup = false;
  std::optional<std::string> footerPill;  // event channel family, "Event feed", or the network
  bool footerPillOutlined = false;
  std::string footer;                      // "In your lineup", "+ 3 backup feeds"...
  std::optional<std::string> situation;    // shown instead of the footer
  bool redZone = false;
  std::optional<std::string> lastPlay;
  int feeds = 0;

  // The live progress edge (0..1), when shown.
  std::optional<double> progress;

  // Live scoreboard details the hero and guide reuse.
  std::optional<std::string> clock;
  bool notStarted = false;
  std::optional<Seconds> endedAt;

  // Select: watch `playID` (a card's part on now), or open the details card.
  bool selectWatches = false;
  std::string playID;
  bool canWhereToWatch = false;
  bool canRemind = false;  // upcoming (the store finds the target)
};

struct Group {
  std::string label;  // empty: no header
  int order = 0;
  std::vector<Tile> tiles;
};

struct FamilyTile {
  std::string id;
  std::string key;
  std::string name;
  std::string countText;  // "12 feeds"
  std::string leagueChip;
  std::string hint;       // "Up/Down to surf"
};

struct Row {
  std::string id;  // "teams", "live", "sport|NFL", "more", "untimed", "families"
  std::string title;
  int live = 0;
  int count = 0;
  bool mixed = false;
  bool grouped = false;
  std::string summary;  // "·  12  ·  3 live"
  std::vector<Group> groups;
  std::vector<FamilyTile> families;
  std::vector<std::string> eventIDs;  // in order
};

enum class HeroChip { none, live, delayed, later, ended, postponed, mayHaveEnded };

struct Hero {
  bool empty = true;  // no game: "Live Events" and the intro line
  std::string id;
  std::string leagueLine;  // "NFL  ·  Week 8"
  std::string clockText;   // now
  std::vector<SideLine> badges;
  std::string title;       // "Indiana 24  –  17 Michigan" with scores
  HeroChip chip = HeroChip::none;
  std::string chipText;
  std::string statusText;  // "Q3 4:12   ·   3rd & 7", "Tomorrow 8:00 PM   ·   Starts in 2 h 5 m   ·   Venue"
  std::optional<double> progress;
  std::string whereText;
  std::optional<std::string> channelDisplay;
  std::vector<std::string> channelLogos;
};

struct Board {
  std::vector<Row> rows;
  // No rows: "No upcoming events found in your channels right now."
  std::string emptyMessage;
  std::optional<std::string> heroID;
  Hero hero;
  // Every tile shown, by event id (copies with their live phase set).
  std::map<std::string, SportsEvent> events;
  std::map<std::string, Tile> tiles;
  // A card's tile id -> its parts (early prelims, prelims, main; race
  // weekend sessions), in order.
  std::map<std::string, std::vector<SportsEvent>> cards;
  std::set<std::string> studio;
  // Tennis players on screen (player_photos::PlayerPhotos::request) and race
  // weekends you can watch (race_data::refreshForBoard).
  std::vector<std::string> tennisPlayers;
  std::vector<std::string> watchableRaceWeekends;
  Seconds now = 0;
};

Board build(const Input& input);

// The tile with this focus key is still on the board (the caller keeps a
// focused tile until you move or 5 s pass).
bool containsKey(const Board& board, const std::string& key);

// A tile is the game on screen: the same event, or a card whose part is.
bool matches(const Board& board, const std::string& tileID, const std::string& eventID);
// A card tile: the part on now, else the first one.
std::optional<SportsEvent> cardCurrent(const Board& board, const std::string& tileID,
                                       const scores::Statuses& statuses, Seconds now);

// MARK: Sports column

struct ColumnItem {
  std::string rowID;
  std::string title;
  std::optional<std::string> liveText;  // "3 live"
  int count = 0;
};
std::vector<ColumnItem> sportsColumn(const Board& board);

// MARK: Event guide (in the player)

// Rows the guide offers (not event channels or untimed feeds).
std::vector<const Row*> guideRows(const Board& board);
// The row the guide opens on: the game's sport, else any row with it.
std::optional<std::string> guideRowID(const Board& board, const std::optional<std::string>& eventID);
// The games of a row you can switch to.
std::vector<std::string> guideEvents(const Board& board, const std::string& rowID);
// Up / Down while watching: the live games of the game's sport, in row
// order (the game on screen is kept even if it just ended).
std::vector<std::string> flipEvents(const Board& board, const std::string& eventID, Seconds now);

struct GuideGame {
  std::string id;
  bool sidesLayout = false;
  std::vector<SideLine> sides;
  std::string title;
  std::optional<std::string> subtitle;
  Pill pill = Pill::none;  // live / delayed only
  std::string pillText;
  std::string statusText;
  std::string whereText;
  bool watching = false;
  bool dimmed = false;
  std::string playID;
};
struct GuideItem {
  std::optional<std::string> header;
  std::optional<GuideGame> game;
};
struct GuideColumnItem {
  std::string rowID;
  std::string title;
  std::optional<std::string> liveText;
  int count = 0;  // games you can watch
  bool origin = false;
};
struct EventGuide {
  std::string rowID;
  std::string heading;  // "Live Events  ·  NFL  ·  8:05 PM"
  std::vector<GuideItem> items;
  std::optional<std::string> focusID;  // the game on screen, else the first
  std::string emptyMessage;            // when there are no items
  std::vector<GuideColumnItem> sports;
  std::string hint;         // the game list
  std::string sportsHint;   // the sports column open
  std::string sourcesHint;  // a game's other ways to watch open
};
// The guide for a row (nullopt: the row of the game on screen).
EventGuide eventGuide(const Board& board, const std::optional<std::string>& rowID,
                      const std::optional<std::string>& watchingEventID, const Settings& settings,
                      const time::Zone& zone);

struct SourceItem {
  std::string channelID;
  std::string name;
  std::string detail;
  bool spanish = false;  // "ES" badge
};
// The guide's other ways to watch a game (a card: its part on now).
std::vector<SourceItem> guideSources(const Board& board, const std::string& tileID, const scores::Statuses& statuses,
                                     const ChannelFor& channelFor);

// MARK: Where to Watch and the details card

struct WhereSection {
  std::string title;
  std::vector<SourceItem> items;
  std::string playEventID;  // the event (or card part) these play
};
struct WherePanel {
  std::string title;
  std::vector<WhereSection> sections;
  std::optional<std::string> moreStations;  // "3 more stations in your channels are showing it"
  std::string hint;
};
// `feedSummary`: extra detail for a provider feed (the store's), optional.
WherePanel wherePanel(const Board& board, const SportsEvent& event, const ChannelFor& channelFor,
                      const std::function<std::string(const std::string&)>& feedSummary = nullptr);

struct DetailCard {
  std::string leagueLine;
  std::vector<SideLine> badges;
  std::string title;
  std::string summary;  // "Tonight 8:00 PM  ·  Venue  ·  On X  ·  3 feeds from your provider"
  std::optional<std::string> watchLabel;  // "Watch X Now"
  std::optional<std::string> watchSourceID;
  bool whereToWatch = false;
  bool canRemind = false;
  std::optional<std::string> notInChannels;  // with the "if one picks it up" line
};
DetailCard detailCard(const SportsEvent& event, Seconds now, const time::Zone& zone, const Settings& settings,
                      const ChannelFor& channelFor);

// MARK: Formatting (exposed for tests)

std::string clockText(Seconds t, const time::Zone& zone, bool twelveHour);       // "8:00 PM" / "20:00"
std::string shortClockText(Seconds t, const time::Zone& zone, bool twelveHour);  // "8:00" / "20:00"
std::string untilText(Seconds start, Seconds now);                               // "in 25 m", "in 2 h", ""
// How far into the game the clock says it is (0..1), or nullopt.
std::optional<double> clockProgress(const LiveStatus& status, std::string_view league);

}  // namespace marigold::sports::board
