// Build 144: a provider membership's end date in words (Settings ›
// Providers, the provider's page, the guide's heads-up), counted on the
// TV's own calendar.
#include <doctest.h>

#include <string>

#include "core/engine/membership.hpp"

namespace ms = marigold::engine::membership;
namespace time_ = marigold::time;
using ms::Membership;
using ms::Stage;

namespace {

time_::Zone la() { return *time_::Zone::named("America/Los_Angeles"); }

time_::Seconds at(const time_::Zone& z, int y, int mo, int d, int h = 12, int mi = 0) {
  return z.fromLocal(time_::Civil{y, mo, d, h, mi, 0});
}

Membership ends(time_::Seconds t, int streams = 0, bool trial = false) {
  Membership m;
  m.ends = t;
  if (streams) m.streams = streams;
  m.trial = trial;
  return m;
}

}  // namespace

TEST_CASE("membership: far off, the date with its year") {
  time_::Zone z = la();
  time_::Seconds now = at(z, 2026, 11, 5);  // a Thursday
  Membership m = ends(at(z, 2027, 3, 3, 23, 59), 2);
  CHECK(ms::stage(m, now, z) == Stage::Far);
  CHECK_FALSE(ms::warns(m, now, z));
  CHECK(ms::daysLeft(*m.ends, now, z) == 118);
  CHECK(ms::rowText(m, now, z) == "Ends Mar 3, 2027");
  CHECK(ms::accountLine("Xtream", "Signed in", m, now, z) == "Xtream \xc2\xb7 Signed in \xc2\xb7 Ends Mar 3, 2027");
  ms::Text note = ms::pageNote(m, now, z);
  CHECK(note.title == "Membership");
  CHECK(note.detail == "Ends Wednesday, March 3, 2027 \xc2\xb7 in 118 days");
  ms::Text info = ms::info(m, false, now, z);
  CHECK(info.title == "Membership ends March 3, 2027");
  CHECK(info.detail == "118 days left. Up to 2 streams at once.");
  CHECK(ms::streamsNote(m) == "2 at a time on this account. Another TV watching it counts too.");
  CHECK(ms::headsUp("Home TV", m, now, z).empty());
}

TEST_CASE("membership: the last week is amber, with the day") {
  time_::Zone z = la();
  time_::Seconds now = at(z, 2026, 11, 5);
  Membership m = ends(at(z, 2026, 11, 10, 23, 0));
  CHECK(ms::stage(m, now, z) == Stage::Soon);
  CHECK(ms::warns(m, now, z));
  CHECK(ms::rowText(m, now, z) == "Ends in 5 days \xc2\xb7 Nov 10");
  CHECK(ms::accountLine("Xtream", "Signed in", m, now, z) ==
        "Xtream \xc2\xb7 Signed in \xc2\xb7 Ends in 5 days \xc2\xb7 Nov 10");
  ms::Text note = ms::pageNote(m, now, z);
  CHECK(note.title == "Membership ends in 5 days");
  CHECK(note.detail == "Tuesday, November 10, 2026. Renew it with your provider to keep its channels.");
  CHECK(ms::info(m, false, now, z).title == "Membership ends in 5 days");
  CHECK(ms::headsUp("Backup TV", m, now, z) == "Backup TV ends in 5 days");

  // Tomorrow and today.
  Membership tomorrow = ends(at(z, 2026, 11, 6, 1, 0));
  CHECK(ms::rowText(tomorrow, now, z) == "Ends tomorrow \xc2\xb7 Nov 6");
  CHECK(ms::pageNote(tomorrow, now, z).title == "Membership ends tomorrow");
  CHECK(ms::headsUp("Backup TV", tomorrow, now, z) == "Backup TV ends tomorrow");
  Membership today = ends(at(z, 2026, 11, 5, 23, 30));
  CHECK(ms::rowText(today, now, z) == "Ends today");
  CHECK(ms::headsUp("Backup TV", today, now, z) == "Backup TV ends today");
  CHECK(ms::info(today, false, now, z).title == "Membership ends today");

  // Seven days out is not yet the last week; six is.
  CHECK(ms::stage(ends(at(z, 2026, 11, 12, 9, 0)), now, z) == Stage::Far);
  CHECK(ms::stage(ends(at(z, 2026, 11, 11, 23, 59)), now, z) == Stage::Soon);
  // Across the new year, no year on the short date.
  time_::Seconds dec = at(z, 2026, 12, 29);
  CHECK(ms::rowText(ends(at(z, 2027, 1, 2)), dec, z) == "Ends in 4 days \xc2\xb7 Jan 2");
}

TEST_CASE("membership: once it has ended") {
  time_::Zone z = la();
  time_::Seconds now = at(z, 2026, 11, 12);
  Membership m = ends(at(z, 2026, 11, 10, 23, 0), 2);
  CHECK(ms::stage(m, now, z) == Stage::Ended);
  CHECK(ms::warns(m, now, z));
  CHECK(ms::rowText(m, now, z) == "Ended Nov 10 \xc2\xb7 renew it with your provider");
  // Not a bare "Couldn't sign in": the end takes its place.
  CHECK(ms::accountLine("Xtream", "Couldn't sign in", m, now, z) ==
        "Xtream \xc2\xb7 Ended Nov 10 \xc2\xb7 renew it with your provider");
  ms::Text note = ms::pageNote(m, now, z);
  CHECK(note.title == "Membership ended on November 10");
  CHECK(note.detail == "Renew it with your provider; its channels come back by themselves.");
  ms::Text info = ms::info(m, true, now, z);
  CHECK(info.title == "Its membership ended on November 10");
  CHECK(info.detail.find("no longer lets this TV sign in") != std::string::npos);
  CHECK(info.detail.find("Channels from your other providers keep playing.") != std::string::npos);
  CHECK(ms::headsUp("Backup TV", m, now, z) == "Backup TV ended Nov 10");
  // The very moment it ends.
  CHECK(ms::stage(m, *m.ends, z) == Stage::Ended);
  CHECK(ms::stage(m, *m.ends - 1, z) == Stage::Soon);
  // Last year's: with its year.
  time_::Seconds jan = at(z, 2027, 1, 2);
  CHECK(ms::rowText(ends(at(z, 2026, 12, 30)), jan, z) == "Ended Dec 30, 2026 \xc2\xb7 renew it with your provider");
  CHECK(ms::headsUp("Backup TV", ends(at(z, 2026, 12, 30)), jan, z) == "Backup TV ended Dec 30, 2026");
  // The panel says it has ended, with no date.
  Membership said;
  said.endedStatus = true;
  CHECK(ms::stage(said, now, z) == Stage::Ended);
  CHECK(ms::rowText(said, now, z) == "Ended \xc2\xb7 renew it with your provider");
  CHECK(ms::headsUp("Backup TV", said, now, z) == "Backup TV has ended");
  CHECK(ms::pageNote(said, now, z).title == "Membership has ended");
}

TEST_CASE("membership: a trial says so") {
  time_::Zone z = la();
  time_::Seconds now = at(z, 2026, 11, 5);
  Membership far = ends(at(z, 2027, 3, 3), 0, true);
  CHECK(ms::rowText(far, now, z) == "Trial \xc2\xb7 ends Mar 3, 2027");
  CHECK(ms::pageNote(far, now, z).title == "Trial");
  CHECK(ms::info(far, false, now, z).title == "Trial ends March 3, 2027");
  Membership soon = ends(at(z, 2026, 11, 10), 0, true);
  CHECK(ms::rowText(soon, now, z) == "Trial \xc2\xb7 ends in 5 days \xc2\xb7 Nov 10");
  CHECK(ms::pageNote(soon, now, z).title == "Trial ends in 5 days");
  CHECK(ms::rowText(ends(at(z, 2026, 11, 1), 0, true), now, z) ==
        "Trial \xc2\xb7 ended Nov 1 \xc2\xb7 renew it with your provider");
}

TEST_CASE("membership: no end date (or unlimited) shows nothing") {
  time_::Zone z = la();
  time_::Seconds now = at(z, 2026, 11, 5);
  Membership none;
  CHECK(ms::stage(none, now, z) == Stage::None);
  CHECK_FALSE(ms::warns(none, now, z));
  CHECK(ms::rowText(none, now, z).empty());
  CHECK(ms::accountLine("Emby / Jellyfin", "Signed in", none, now, z) == "Emby / Jellyfin \xc2\xb7 Signed in");
  CHECK(ms::accountLine("Xtream", "", none, now, z) == "Xtream \xc2\xb7 Signing in\xe2\x80\xa6");
  CHECK(ms::pageNote(none, now, z).title.empty());
  CHECK(ms::headsUp("Living room server", none, now, z).empty());
  CHECK(ms::info(none, false, now, z).title.empty());
  CHECK(ms::info(none, false, now, z).detail.empty());
  // Streams at once still said when known; 0 is not said.
  none.streams = 1;
  CHECK(ms::info(none, false, now, z).detail == "One stream at a time.");
  none.streams = 0;
  CHECK(ms::streamsNote(none).empty());
}

TEST_CASE("membership: days count on the TV's own calendar") {
  time_::Zone z = la();
  time_::Zone utc = time_::Zone::utc();
  // 03:00 UTC on Nov 10 is the evening of Nov 9 in Los Angeles.
  time_::Seconds end = time_::secondsFromCivil({2026, 11, 10, 3, 0, 0});
  time_::Seconds now = time_::secondsFromCivil({2026, 11, 5, 20, 0, 0});
  CHECK(ms::daysLeft(end, now, z) == 4);
  CHECK(ms::daysLeft(end, now, utc) == 5);
  CHECK(ms::rowText(ends(end), now, z) == "Ends in 4 days \xc2\xb7 Nov 9");
  CHECK(ms::rowText(ends(end), now, utc) == "Ends in 5 days \xc2\xb7 Nov 10");
  // The weekday follows the zone too.
  CHECK(ms::pageNote(ends(end), now, z).detail.rfind("Monday, November 9, 2026", 0) == 0);
}

TEST_CASE("membership: the guide's heads-up is once a day, by the TV's calendar") {
  time_::Zone z = la();
  time_::Seconds morning = at(z, 2026, 11, 5, 8, 0);
  int64_t today = ms::localDay(morning, z);
  CHECK(ms::headsUpDue(0, morning, z));  // never shown
  CHECK_FALSE(ms::headsUpDue(today, morning, z));
  CHECK_FALSE(ms::headsUpDue(today, at(z, 2026, 11, 5, 23, 59), z));  // late the same evening
  CHECK(ms::headsUpDue(today, at(z, 2026, 11, 6, 0, 1), z));          // just after midnight
  CHECK(ms::headsUpDue(today - 1, morning, z));
  // 8 PM in Los Angeles is already the next day in UTC: still today here.
  time_::Seconds evening = at(z, 2026, 11, 5, 20, 0);
  CHECK_FALSE(ms::headsUpDue(today, evening, z));
  CHECK(ms::headsUpDue(today, evening, time_::Zone::utc()));
}
