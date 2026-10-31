// Search (Build 113): what a typed query finds, decided once for every
// platform. The screen draws the groups this returns and computes nothing.
//
//   query  -> parse(): folded, cleaned ("watch the bulls game" -> "bulls"),
//             time words as a filter ("news tonight"), number words as
//             digits ("two oh six"); a query that is itself a title
//             ("Lakers Tonight") is left as typed.
//   index  -> what can be found: channels with their other names (the core
//             networks, regional sports names, the built-in reference's
//             names and former call signs), every team of the saved team
//             lists (byes too) with a small nickname table, the board's
//             games, leagues and sports, and the guide's show titles.
//   run()  -> the top result, then Channels, Teams, Games, Shows, and
//             genre, sport and league results; "Showing …" for a typo.
//
// Matching: "&" = "and", a leading "the" is optional, tokens match in any
// order with the last one as a prefix, typos per token (1 edit at 5-7
// letters, 2 at 8+, a swap is one edit), two letters only match word
// starts and initials. Score text (the hero title, situation, leaders,
// podium) is never in the index, and finished games come last, never on
// top, and only while Show scores is on.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/engine/events_model.hpp"
#include "core/engine/model.hpp"
#include "core/util/time.hpp"

namespace marigold::sports {
struct SportsTeam;
}

namespace marigold::engine::search {

// ---------------------------------------------------------------- text

// Lower case without accents, "&" as "and", apostrophes dropped, other
// punctuation as spaces, letters split from digits ("sn2" -> "sn 2",
// "49ers" -> "49 ers"), single spaces.
std::string fold(std::string_view s);
std::vector<std::string> words(std::string_view folded);
// First letters of the words ("northbridge bulls" -> "nb").
std::string initials(const std::vector<std::string>& words);
// Edits between two words (a swap of neighbours is one), up to `cap` + 1.
int editDistance(std::string_view a, std::string_view b, int cap);
// The edits a word of `letters` letters may have: 0 below 5, 1 for 5-7, 2
// for 8 or more.
int typoAllowance(size_t letters);

// ---------------------------------------------------------------- query

enum class When : uint8_t { Any, Now, Tonight, Today, Tomorrow };

struct Query {
  std::string raw;
  std::string text;  // folded and cleaned; "" for nothing
  std::vector<std::string> tokens;
  When when = When::Any;
  bool cleaned = false;    // words or a time word were taken off
  bool digits = false;     // only digits ("206"; "channel four" -> "4")
  bool exactOnly = false;  // "Search for … instead": no typo matches
  std::string regional;    // a regional sports network's key ("Bally Sports Sun")
  // Letters and digits, without spaces (the 1- and 2-character rules).
  size_t length() const;
};

// `isTitle(folded)`: a show, channel or team is called exactly that, so the
// cleaner leaves the query as typed.
Query parse(std::string_view raw, const std::function<bool(const std::string&)>& isTitle = {});

// How a name matches, best last.
enum class Hit : uint8_t { None, Typo, Inside, Initials, Tokens, Word, Start, Exact };
// One name (folded). One character: exact or a start. Two: exact, starts
// of words and initials, never inside a word. Three or more: tokens in any
// order, inside a word, and typos (unless the query is exact only).
Hit match(const Query& q, std::string_view folded);

// ---------------------------------------------------------------- index

struct ChannelEntry {
  std::string id;
  std::string name;  // folded
  std::string number;
  // Locals: the network ("nbc") and the call signs, the current one first
  // and the ones it had before.
  std::string network;
  std::string channel;  // the station's channel ("4" for "NBC 4")
  std::vector<std::string> stations;
  std::vector<std::string> aliases;  // folded other names
  std::string regional;              // the regional sports network's key, or ""
  std::string section;               // folded neighborhood
  bool inLineup = false, favorite = false;
  bool browsable = true;  // another country's sports channel: only when named exactly
  int stationRank = 0;    // lower first (network_map::stationRank)
};

struct TeamEntry {
  std::string key;       // "LEAGUE|id"
  std::string name;      // full name, as shown
  std::string league;    // short ("PBL")
  std::string leagueName;
  std::vector<std::string> names;  // folded: full name, variants, nicknames
  std::string abbreviation;        // folded
  bool followed = false;
  int followOrder = 0;         // the order followed
  bool leaguePicked = true;    // "Add PBL in My Sports to see their games" when not
  std::string liveGame;        // a game on now (an index into games by id)
  std::string nextGame;        // the next one in the coming week
};

struct GameEntry {
  enum class State : uint8_t { Live, Upcoming, Ended, Unknown };
  std::string id;
  std::string title;  // the matchup, as shown ("Bulls at Harbor Kings"); never a score
  std::vector<std::string> names;  // folded: matchup, full team names, parts
  std::vector<std::string> teams;  // team keys
  std::string league, leagueName, sport;  // folded league words for matching
  State state = State::Unknown;
  time::Seconds start = 0;
  bool watchable = false;
  bool streamingOnly = false;
  bool favorite = false;
};

// A board tile as a game to find: its matchup, side names, sport and
// parts, and its state. Never the hero title, the situation, leaders or a
// podium (score text). The caller adds the teams and the league.
GameEntry gameEntry(const EventTile& t);

struct LeagueEntry {
  std::string id;  // "PBL", or "S:Golf"; for a sport, "sport:basketball"
  std::string title;
  std::vector<std::string> names;  // folded: name, short name, keywords
  bool followed = false;
  bool sport = false;
};

struct Index {
  std::vector<ChannelEntry> channels;
  std::vector<TeamEntry> teams;
  std::vector<GameEntry> games;
  std::vector<LeagueEntry> leagues;
  // The guide's shows: folded title -> airings still to end, by start.
  struct Airing {
    std::string entryId;
    const Listing* listing = nullptr;
  };
  std::unordered_map<std::string, std::vector<Airing>> shows;
  std::shared_ptr<const Listings> listings;  // keeps the airings' listings alive
  std::unordered_set<std::string> titles;    // folded channel, team and show names
  std::unordered_map<std::string, size_t> channelById, gameById, teamByKey;

  // Fills the lookups and titles from the lists above.
  void finish();
  bool isTitle(const std::string& folded) const { return titles.count(folded) > 0; }
};

// A team's names for the index: its full name, the sports matcher's
// variants (other names, the nickname, a school's short name) and the
// nickname table's, folded.
std::vector<std::string> teamNames(const sports::SportsTeam& team, bool college);

// Team nicknames people say that the team lists don't carry (folded
// nickname -> folded full names). Ambiguous ones list every team.
const std::unordered_map<std::string, std::vector<std::string>>& nicknames();
// The nicknames of a team, by its folded full name.
std::vector<std::string> nicknamesOf(const std::string& foldedName);

// A show the guide lists as a stand-in, not a programme (a 24/7 loop, "To
// be announced", a slot as long as half a day or more).
bool placeholderShow(const Listing& l, const std::string& channelName);
// "REPLAY" or "HIGHLIGHTS" for a sports re-airing, else "".
std::string showTag(const std::string& foldedTitle);

// ---------------------------------------------------------------- results

enum class Kind : uint8_t { Channel, Team, Game, Show, Genre };

struct Item {
  Kind kind = Kind::Channel;
  std::string id;  // entry id, team key, game id, show "entry|start", genre key
  // Rank of the merged list (lower first): 1 exact channel, 2 a followed
  // team's live game, 3 a live game whose team matches exactly, 4 an exact
  // show title on now, 5 a genre or sport word, 6 word starts, 7 initials
  // and aliases, 8 typos, 9 the rest.
  int tier = 9;
  int score = 0;  // within the group
  Hit hit = Hit::None;
  std::string name;  // what "Showing …" and a recent search say
  // Shows: the airing found, and the next one of the same title there.
  std::string entryId;
  const Listing* listing = nullptr;
  time::Seconds also = 0;
  bool live = false;
  bool playable = false;  // OK watches it (live, in your channels)
};

struct Options {
  time::Seconds now = 0;
  time::Zone zone = time::Zone::utc();
  bool showScores = true;
  bool shows = true;  // shows are searched (after the 450 ms pause)
  size_t maxChannels = 6, maxTeams = 3, maxGames = 6, maxShows = 6, maxGenres = 2;
};

struct Results {
  Query query;
  std::optional<Item> top;
  std::vector<Item> channels, teams, games, shows, genres;
  // A corrected match: "Showing Northbridge Bulls" over the band.
  std::string showing;
  // Nothing anywhere (never for one character).
  bool nothing = false;
  bool empty() const { return !top && channels.empty() && teams.empty() && games.empty() && shows.empty() && genres.empty(); }
};

Results run(const Index& index, const Query& q, const Options& o);

// ---------------------------------------------------------------- numbers

// Number keys from an empty field (LG): the channel with that number, the
// ones starting with it, and whether to wait for OK because the digits
// also start a team or channel name ("49": the 49ers).
struct Digits {
  std::string channel;            // entry id, or ""
  std::vector<std::string> also;  // numbers that start with the digits
  bool waits = false;             // no tune by itself
  std::string team;               // the team the digits start ("Harbor 49s")
};
Digits digits(const Index& index, const std::string& typed);

// ---------------------------------------------------------------- recent

// A recent search: the name of what was picked ("Sports Net 2", never the
// typed "sn2") and its id. Eight are kept, four shown.
struct Recent {
  std::string name, id;
  bool operator==(const Recent& o) const { return name == o.name && id == o.id; }
};
constexpr size_t kRecentKept = 8;
constexpr size_t kRecentShown = 4;
// Newest first; the same id or name moves to the front. Nothing without a
// name or an id is kept.
bool remember(std::vector<Recent>& list, const Recent& r);

}  // namespace marigold::engine::search
