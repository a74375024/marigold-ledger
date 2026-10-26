#include "core/sports/reminders.hpp"

#include <algorithm>
#include <cstdlib>

#include "core/util/json.hpp"

namespace marigold::sports {

std::string LiveReminder::id() const { return channelID + "|" + std::to_string(start); }

Reminders::Reminders(std::vector<LiveReminder> saved, time::Seconds now) {
  for (LiveReminder& r : saved) {
    if (r.end > now) items_.push_back(std::move(r));
  }
}

bool Reminders::isSet(std::string_view channelID, time::Seconds start) const {
  return std::any_of(items_.begin(), items_.end(), [&](const LiveReminder& r) {
    return r.channelID == channelID && std::llabs(r.start - start) < kSameStart;
  });
}

void Reminders::toggle(const LiveReminder& reminder) {
  auto it = std::find_if(items_.begin(), items_.end(), [&](const LiveReminder& r) {
    return r.channelID == reminder.channelID && std::llabs(r.start - reminder.start) < kSameStart;
  });
  if (it != items_.end()) {
    items_.erase(it);
    return;
  }
  items_.push_back(reminder);
  std::stable_sort(items_.begin(), items_.end(),
                   [](const LiveReminder& a, const LiveReminder& b) { return a.start < b.start; });
}

Reminders::Check Reminders::check(time::Seconds now) {
  Check result;
  size_t before = items_.size();
  items_.erase(std::remove_if(items_.begin(), items_.end(),
                              [&](const LiveReminder& r) { return r.end <= now; }),
               items_.end());
  auto due = std::find_if(items_.begin(), items_.end(),
                          [&](const LiveReminder& r) { return r.start <= now + kLeadSeconds; });
  if (due_ || due == items_.end()) {
    result.changed = items_.size() != before;
    return result;
  }
  due_ = *due;
  items_.erase(due);
  result.changed = true;
  result.due = due_;
  return result;
}

std::string Reminders::toJson() const {
  json::Value list = json::Value::array();
  for (const LiveReminder& r : items_) {
    json::Value v = json::Value::object();
    v.set("channelID", r.channelID);
    v.set("channelName", r.channelName);
    v.set("title", r.title);
    v.set("start", static_cast<int64_t>(r.start));
    v.set("end", static_cast<int64_t>(r.end));
    list.push(v);
  }
  return json::write(list);
}

std::optional<std::vector<LiveReminder>> Reminders::fromJson(std::string_view text) {
  auto doc = json::parse(text);
  if (!doc || !doc->isArray()) return std::nullopt;
  std::vector<LiveReminder> out;
  for (const json::Value& v : doc->items()) {
    // Like Swift's decoder: one bad item fails the whole list.
    if (!v["channelID"].isString() || !v["channelName"].isString() || !v["title"].isString() ||
        !v["start"].isNumber() || !v["end"].isNumber()) {
      return std::nullopt;
    }
    out.push_back({v["channelID"].asString(), v["channelName"].asString(), v["title"].asString(),
                   v["start"].asInt(), v["end"].asInt()});
  }
  return out;
}

}  // namespace marigold::sports
