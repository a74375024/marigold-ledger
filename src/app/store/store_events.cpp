// Live Events in the store: the schedule, scores and the board snapshot.
#include <algorithm>

#include "app/app.hpp"
#include "app/store/store.hpp"

namespace marigold::app {

void Store::eventsOpen(bool open) { eventsOpen_ = std::max(0, eventsOpen_ + (open ? 1 : -1)); }

bool Store::isEvent(const std::string& id) const { return eventTile(id) != nullptr; }

const EventTile* Store::eventTile(const std::string& id) const {
  for (const EventRow& r : events_->rows)
    for (const EventGroup& g : r.groups)
      for (const EventTile& t : g.tiles)
        if (t.id == id) return &t;
  return nullptr;
}

WhereToWatch Store::whereToWatch(const std::string& eventId) const {
  WhereToWatch w;
  if (const EventTile* t = eventTile(eventId)) w.title = t->heroTitle.empty() ? t->title : t->heroTitle;
  return w;
}

std::vector<std::string> Store::eventFlipList(const std::string& eventId) const { return {eventId}; }

std::vector<Feed> Store::eventFeeds(const std::string& eventId, const std::string& via) const {
  (void)via;
  return feeds(eventId);
}

bool Store::hasReminder(const std::string&) const { return false; }
bool Store::toggleReminder(const std::string&) { return false; }

}  // namespace marigold::app
