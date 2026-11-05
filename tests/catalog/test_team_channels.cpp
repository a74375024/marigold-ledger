// Build 143: team channels filed as sports wherever the provider puts them,
// call signs that are words, and MSG's networks kept apart.
#include "catalog/helpers.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/reference.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;
using testdata::channel;

TEST_CASE("pro team channels are sports channels") {
  CHECK(proTeamChannel("BOSTON BRUINS"));
  CHECK(proTeamChannel("WNBA NEW YORK LIBERTY"));
  CHECK(proTeamChannel("St. Louis Blues"));
  CHECK(proTeamChannel("Philadelphia 76ers"));
  CHECK_FALSE(proTeamChannel("Boston Bruins Classics"));
  CHECK_FALSE(proTeamChannel("Liberty"));
  CHECK_FALSE(proTeamChannel("King of the Hill"));

  // A league's team group, a team name filed under entertainment, the WNBA by name.
  CHECK(traitsFor(channel("1", "US: BOSTON BRUINS", std::string("US| NHL TEAM PPV"))).category ==
        LineupCategory::sports);
  CHECK(traitsFor(channel("2", "AT&T: ATLANTA BRAVES ᴿᴬᵂ", std::string("US| AT&T ᴿᴬᵂ"))).category ==
        LineupCategory::sports);
  CHECK(traitsFor(channel("3", "US: WNBA NEW YORK LIBERTY", std::string("US| WNBA TEAM PPV"))).category ==
        LineupCategory::sports);
  CHECK(traitsFor(channel("4", "US: WNBA Classics", std::string("US| ENTERTAINMENT"))).category ==
        LineupCategory::sports);
  // NBA and MLB team channels as before.
  CHECK(traitsFor(channel("5", "US: NBA MIAMI HEAT", std::string("US| NBA TEAM PPV"))).category ==
        LineupCategory::sports);
  CHECK(traitsFor(channel("6", "US: MLB LOS ANGELES ANGELS ᴿᴬᵂ", std::string("US| MLB TEAM PPV"))).category ==
        LineupCategory::sports);
  // Not a team: the show stays where it was.
  CHECK(traitsFor(channel("7", "AT&T: AMERICA'S TEST KITCHEN ᴿᴬᵂ", std::string("US| AT&T ᴿᴬᵂ"))).category ==
        LineupCategory::entertainment);
}

TEST_CASE("call signs that are words need a network word or a channel number") {
  CHECK(callsignCandidates("KING OF THE HILL").empty());
  CHECK(callsignCandidates("THE KING OF QUEENS 4K").empty());
  CHECK(callsignCandidates("HONG KONG FIGHT CLUB").empty());
  REQUIRE(callsignCandidates("KING 5").size() == 1);
  CHECK(callsignCandidates("KING 5")[0].sign == "KING");
  CHECK(callsignCandidates("SEATTLE, WA KONG IND 16")[0].sign == "KONG");
  CHECK(callsignCandidates("NBC 5 (KING) SEATTLE")[0].sign == "KING");
  CHECK(callsignCandidates("SEATTLE, WA KING NBC 5 A3 HD")[0].sign == "KING");
  // Other signs as before.
  CHECK(callsignCandidates("KTVU Plus")[0].sign == "KTVU");

  // A real KING station and a show named King aren't one row.
  std::vector<Channel> list = {channel("1", "US: NBC 5 (KING) SEATTLE (A)", std::string("US| NBC")),
                               channel("2", "US: KING OF THE HILL 4K", std::string("US| CINEMA TV SHOWS")),
                               channel("3", "TV: SEATTLE, WA KONG IND 16 A3 HD", std::string("US| TV")),
                               channel("4", "AT&T: HONG KONG FIGHT CLUB", std::string("US| AT&T"))};
  CallsignContext context(list, {{"KING", "NBC", {"seattle"}, "5"}, {"KONG", "IND", {"seattle"}, "16"}});
  ChannelTraits station = traitsFor(list[0], context);
  ChannelTraits show = traitsFor(list[1], context);
  CHECK(station.callsign == std::string("KING"));
  CHECK_FALSE(show.callsign);
  CHECK(station.clusterKey != show.clusterKey);
  ChannelTraits kong = traitsFor(list[2], context);
  ChannelTraits club = traitsFor(list[3], context);
  CHECK(kong.callsign == std::string("KONG"));
  CHECK_FALSE(club.callsign);
  CHECK(kong.clusterKey != club.clusterKey);
  CHECK(club.category != LineupCategory::local);
}

TEST_CASE("MSG Sportsnet isn't MSG Network") {
  CallsignContext context;
  context.reference = &reference::builtInIndex();
  ChannelTraits msg = traitsFor(channel("1", "TV: MADISON SQUARE GARDEN ᴿᴬᵂ", std::string("US| TV")), context);
  ChannelTraits sportsnet = traitsFor(channel("2", "TV: MSG SPORTSNET ᴿᴬᵂ", std::string("US| TV")), context);
  ChannelTraits plus = traitsFor(channel("3", "US: MSG PLUS HD", std::string("US| ENTERTAINMENT")), context);
  CHECK(msg.clusterKey != sportsnet.clusterKey);
  CHECK(msg.clusterKey != plus.clusterKey);
  CHECK(sportsnet.category == LineupCategory::sports);
}

TEST_CASE("NBCSN is a core network") {
  ChannelTraits t = traitsFor(channel("1", "US: NBC SPORTS NETWORK", std::string("US| SPORT")));
  CHECK(t.core == std::string("NBCSN"));
  CHECK(t.category == LineupCategory::sports);
  CHECK(traitsFor(channel("2", "US: NBC SN HD", std::string("US| NEWS"))).core == std::string("NBCSN"));
  CHECK_FALSE(traitsFor(channel("3", "US: NBC SPORTS NOW", std::string("US| SPORT"))).core);
}
