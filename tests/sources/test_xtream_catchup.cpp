// Build 163, the catch-up test: the Xtream archive link (timeshift) and the
// panel's clock it is asked in (server_info), kept on the account.
#include <string>

#include "core/sources/account.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/time.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::sources;

namespace {

Account account(std::string user = "u", std::string pass = "p") {
  Account a;
  a.kind = AccountKind::Xtream;
  a.id = "acc1";
  a.displayName = "Mock IPTV";
  a.serverUrl = "http://provider.example.invalid:8080/";
  a.userName = std::move(user);
  a.password = std::move(pass);
  return a;
}

xtream::Client client(const Account& a = account()) {
  Result<xtream::Client> c = xtream::Client::make(a);
  REQUIRE(c.ok());
  return c.value();
}

time::Seconds utc(int y, int mo, int d, int h, int mi, int s = 0) {
  return time::secondsFromCivil(time::Civil{y, mo, d, h, mi, s});
}

std::string info(const std::string& server) {
  return R"({"user_info":{"auth":1,"status":"Active"},"server_info":)" + server + "}";
}

}  // namespace

TEST_CASE("catch-up: the timeshift link, path form, in the server's own clock") {
  xtream::Client c = client();
  // 00:52:37 UTC on the 16th; the seconds are dropped (the panel takes minutes).
  time::Seconds start = utc(2026, 11, 16, 0, 52, 37);
  // A server five hours behind UTC: the evening before, its time.
  CHECK(*c.timeshiftURL("100005", start, 15, -5 * 3600) ==
        "http://provider.example.invalid:8080/timeshift/u/p/15/2026-11-15:19-52/100005.ts");
  // Five and a half hours ahead.
  CHECK(*c.timeshiftURL("100005", start, 15, 5 * 3600 + 1800) ==
        "http://provider.example.invalid:8080/timeshift/u/p/15/2026-11-16:06-22/100005.ts");
  // UTC itself; single digits padded.
  CHECK(*c.timeshiftURL("7", utc(2027, 1, 2, 3, 4), 9, 0) ==
        "http://provider.example.invalid:8080/timeshift/u/p/9/2027-01-02:03-04/7.ts");
  // Across a year end and a month end.
  CHECK(*c.timeshiftURL("7", utc(2027, 1, 1, 2, 0), 9, -3 * 3600) ==
        "http://provider.example.invalid:8080/timeshift/u/p/9/2026-12-31:23-00/7.ts");
}

TEST_CASE("catch-up: the timeshift link encodes the login like the live link") {
  Account a = account("u@x", "p+/ &");
  xtream::Client c = client(a);
  std::string url = *c.timeshiftURL("7", utc(2026, 11, 16, 0, 0), 15, 0);
  CHECK(url == "http://provider.example.invalid:8080/timeshift/u%40x/p%2B%2F%20%26/15/2026-11-16:00-00/7.ts");
  // The same encoding as the live link's.
  std::string live = *c.streamURL("7", {}, true);
  CHECK(live.find("/u%40x/p%2B%2F%20%26/") != std::string::npos);
}

TEST_CASE("catch-up: the duration is capped, and unsafe ids are refused") {
  xtream::Client c = client();
  time::Seconds start = utc(2026, 11, 16, 0, 0);
  CHECK(c.timeshiftURL("7", start, 500, 0)->find("/timeshift/u/p/180/") != std::string::npos);
  CHECK(c.timeshiftURL("7", start, 0, 0)->find("/timeshift/u/p/1/") != std::string::npos);
  CHECK(c.timeshiftURL("7", start, -4, 0)->find("/timeshift/u/p/1/") != std::string::npos);
  CHECK_FALSE(c.timeshiftURL("../1", start, 15, 0));
  CHECK_FALSE(c.timeshiftURL("1?x", start, 15, 0));
  CHECK_FALSE(c.timeshiftURL("1/2", start, 15, 0));
  CHECK_FALSE(c.timeshiftURL("", start, 15, 0));

  // How much is asked: from the moment until now, 5 minutes more, at most 3 hours.
  time::Seconds now = utc(2026, 11, 16, 20, 0);
  CHECK(xtream::timeshiftMinutes(now - 600, now) == 15);
  CHECK(xtream::timeshiftMinutes(now - 60 * 60, now) == 65);
  CHECK(xtream::timeshiftMinutes(now - 86400, now) == xtream::kTimeshiftMaxMinutes);
  CHECK(xtream::kTimeshiftMaxMinutes == 180);
  CHECK(xtream::timeshiftMinutes(now - 30, now) == 5);
  CHECK(xtream::timeshiftMinutes(now + 600, now) == 5);
}

TEST_CASE("catch-up: server_info's clock, as numbers or as text") {
  xtream::Client c = client();
  time::Seconds ts = utc(2025, 11, 16, 1, 0);
  // Numbers.
  auto a = c.parseAccountInfo(200, info(R"({"timezone":"America/New_York","timestamp_now":)" + std::to_string(ts) +
                                        R"(,"time_now":"2025-11-15 20:00:00"})"));
  REQUIRE(a.ok());
  CHECK(a.value().timezone == "America/New_York");
  CHECK(a.value().timestampNow == ts);
  CHECK(a.value().timeNow == utc(2025, 11, 15, 20, 0));
  CHECK(a.value().utcOffset == -5 * 3600);
  // Text, and a server whose zone name disagrees with its clock: the clock wins.
  auto b = c.parseAccountInfo(200, info(R"({"timezone":"Europe/London","timestamp_now":")" + std::to_string(ts) +
                                        R"(","time_now":"2025-11-16 02:00:00"})"));
  REQUIRE(b.ok());
  CHECK(b.value().timestampNow == ts);
  CHECK(b.value().utcOffset == 3600);
  // A 'T' and no seconds.
  auto t = c.parseAccountInfo(200, info(R"({"timestamp_now":)" + std::to_string(ts) + R"(,"time_now":"2025-11-16T01:00"})"));
  REQUIRE(t.ok());
  CHECK(t.value().utcOffset == 0);
}

TEST_CASE("catch-up: the server's offset is rounded to 15 minutes") {
  time::Seconds ts = utc(2025, 11, 16, 0, 0);
  // Readings a few seconds apart.
  CHECK(xtream::serverOffset(ts - 5 * 3600 + 2, ts, "", ts) == -5 * 3600);
  CHECK(xtream::serverOffset(ts - 5 * 3600 - 3, ts, "", ts) == -5 * 3600);
  CHECK(xtream::serverOffset(ts + 5 * 3600 + 1800 + 1, ts, "", ts) == 5 * 3600 + 1800);  // India
  CHECK(xtream::serverOffset(ts + 5 * 3600 + 2700 - 4, ts, "", ts) == 5 * 3600 + 2700);  // Nepal
  CHECK(xtream::serverOffset(ts + 7 * 60, ts, "", ts) == 0);                               // under half of 15 min
  CHECK(xtream::serverOffset(ts + 8 * 60, ts, "", ts) == 15 * 60);
  // Beyond 14 h is no clock: the zone name, else nothing.
  CHECK(xtream::serverOffset(ts + 20 * 3600, ts, "UTC", ts) == 0);
  CHECK_FALSE(xtream::serverOffset(ts + 20 * 3600, ts, "", ts));
  // For the log.
  CHECK(xtream::offsetText(-5 * 3600) == "UTC\xe2\x88\x92" "5");
  CHECK(xtream::offsetText(5 * 3600 + 1800) == "UTC+5:30");
  CHECK(xtream::offsetText(0) == "UTC");
  CHECK(xtream::offsetText(-(3 * 3600 + 1800)) == "UTC\xe2\x88\x92" "3:30");
}

TEST_CASE("catch-up: missing clock values fall back to the zone name, then to nothing") {
  xtream::Client c = client();
  time::Seconds summer = utc(2025, 7, 1, 12, 0), winter = utc(2025, 1, 15, 12, 0);
  // No time_now: the zone name, at the server's own time (daylight time in July).
  auto z = c.parseAccountInfo(200, info(R"({"timezone":"America/Chicago","timestamp_now":)" + std::to_string(summer) + "}"));
  REQUIRE(z.ok());
  CHECK_FALSE(z.value().timeNow);
  CHECK(z.value().utcOffset == -5 * 3600);
  auto w = c.parseAccountInfo(200, info(R"({"timezone":"America/Chicago","timestamp_now":)" + std::to_string(winter) + "}"));
  CHECK(w.value().utcOffset == -6 * 3600);
  // A time_now that can't be read: the zone name too.
  auto bad = c.parseAccountInfo(
      200, info(R"({"timezone":"Europe/Berlin","timestamp_now":)" + std::to_string(winter) + R"(,"time_now":"soon"})"));
  CHECK(bad.value().utcOffset == 3600);
  // A zone the app doesn't know and no clock: not known.
  auto unknown = c.parseAccountInfo(200, info(R"({"timezone":"Asia/Kathmandu"})"));
  CHECK_FALSE(unknown.value().utcOffset);
  // No server_info at all, an empty one, or a null.
  CHECK_FALSE(c.parseAccountInfo(200, R"({"user_info":{"auth":1}})").value().utcOffset);
  CHECK_FALSE(c.parseAccountInfo(200, info("{}")).value().utcOffset);
  CHECK_FALSE(c.parseAccountInfo(200, info("null")).value().utcOffset);
  // Only one of the two readings, and no zone.
  CHECK_FALSE(c.parseAccountInfo(200, info(R"({"time_now":"2025-11-16 02:00:00"})")).value().utcOffset);
  CHECK_FALSE(c.parseAccountInfo(200, info(R"({"timestamp_now":0,"time_now":"2025-11-16 02:00:00"})")).value().utcOffset);
}

TEST_CASE("catch-up: the server's offset is kept on the account and saved with it") {
  Account a = account();
  CHECK_FALSE(a.serverOffset);
  xtream::AccountInfo i;
  i.utcOffset = -5 * 3600;
  xtream::applyAccountInfo(a, i);
  CHECK(a.serverOffset == -5 * 3600);
  // An answer that doesn't say keeps what was learned.
  xtream::applyAccountInfo(a, xtream::AccountInfo{});
  CHECK(a.serverOffset == -5 * 3600);
  i.utcOffset = 3600;
  xtream::applyAccountInfo(a, i);
  CHECK(a.serverOffset == 3600);

  // Saved in the main document (not a secret), read back after a restart.
  Account none = account();
  none.id = "acc2";
  EncodedAccounts enc = encodeAccounts({a, none});
  CHECK(enc.main.find("serverOffset") != std::string::npos);
  CHECK(enc.secrets.find("serverOffset") == std::string::npos);
  auto back = decodeAccounts(enc.main, enc.secrets);
  REQUIRE(back.ok());
  REQUIRE(back.value().size() == 2);
  CHECK(back.value()[0].serverOffset == 3600);
  CHECK_FALSE(back.value()[1].serverOffset);
  // A saved value that can't be an offset is dropped.
  std::string odd = enc.main;
  odd.replace(odd.find("3600"), 4, "99999");
  auto dropped = decodeAccounts(odd, enc.secrets);
  REQUIRE(dropped.ok());
  CHECK_FALSE(dropped.value()[0].serverOffset);
}

TEST_CASE("replay: a stream's past guide is one get_simple_data_table request") {
  Request r = client().simpleDataTableRequest("501");
  CHECK(r.url.find("/player_api.php?") != std::string::npos);
  CHECK(r.url.find("action=get_simple_data_table") != std::string::npos);
  CHECK(r.url.find("stream_id=501") != std::string::npos);
  CHECK(r.headers.empty());
}

TEST_CASE("replay: get_simple_data_table's listings, base64 titles and numbers or text") {
  // "Morning Show", "Game Night", "Later" and a plain title.
  std::string body = R"({"epg_listings":[)"
                     R"({"title":"R2FtZSBOaWdodA==","start_timestamp":"1800003600","stop_timestamp":"1800007200","has_archive":"1"},)"
                     R"({"title":"TW9ybmluZyBTaG93","start_timestamp":1800000000,"stop_timestamp":1800003600,"has_archive":1},)"
                     R"({"title":"TGF0ZXI=","start_timestamp":"1800007200","stop_timestamp":"1800010800","has_archive":0},)"
                     R"({"title":"Plain title","start_timestamp":"1800010800","stop_timestamp":"1800014400","has_archive":true},)"
                     R"({"title":"","start_timestamp":"1800014400","stop_timestamp":"1800018000"},)"
                     R"({"title":"TWlzc2luZw==","start_timestamp":"1800018000"},)"
                     R"({"title":"QmFja3dhcmRz","start_timestamp":"1800021600","stop_timestamp":"1800018000"},)"
                     R"({"title":"TW9ybmluZyBTaG93","start_timestamp":1800000000,"stop_timestamp":1800003600,"has_archive":1},)"
                     R"("not an object"]})";
  std::vector<xtream::ArchiveProgramme> list = xtream::parseSimpleDataTable(200, body);
  REQUIRE(list.size() == 4);
  CHECK(list[0].title == "Morning Show");
  CHECK(list[0].start == 1800000000);
  CHECK(list[0].end == 1800003600);
  CHECK(list[0].archived);
  CHECK(list[1].title == "Game Night");
  CHECK(list[1].archived);
  CHECK(list[2].title == "Later");
  CHECK_FALSE(list[2].archived);
  CHECK(list[3].title == "Plain title");
  CHECK(list[3].archived);
  // Malformed answers: nothing.
  CHECK(xtream::parseSimpleDataTable(200, "").empty());
  CHECK(xtream::parseSimpleDataTable(200, "[]").empty());
  CHECK(xtream::parseSimpleDataTable(200, R"({"epg_listings":"none"})").empty());
  CHECK(xtream::parseSimpleDataTable(200, R"({"epg_listings":[)").empty());
  CHECK(xtream::parseSimpleDataTable(404, body).empty());
  CHECK(xtream::parseSimpleDataTable(200, R"({"epg_listings":[]})").empty());
}
