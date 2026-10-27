// Request groups and handles for the transports that cancel by asking
// (Android, tvOS): a transfer polls `stop`, which the pool answers from
// live() below. webOS's rede keeps its own.
#include <atomic>
#include <memory>

#include "platform/http_transport.hpp"

namespace marigold::http::transport {

struct GroupState {
  std::atomic<uint64_t> generation{0};
  std::atomic<bool> cancelled{false};
};
struct Group {
  std::shared_ptr<GroupState> state = std::make_shared<GroupState>();
};
struct Handle {
  std::shared_ptr<GroupState> group;
  uint64_t generation = 0;
  std::atomic<bool> cancelled{false};
};

Group* createGroup() { return new Group; }
void cancelGroup(Group* g) { g->state->cancelled = true; }
void advanceGroup(Group* g) { g->state->generation += 1; }
void releaseGroup(Group* g) { delete g; }

Handle* createHandle(Group* g) {
  auto* h = new Handle;
  if (g) {
    h->group = g->state;
    h->generation = g->state->generation.load();
  }
  return h;
}

void cancelHandle(Handle* h) { h->cancelled = true; }

bool live(const Handle* h) {
  if (!h) return true;
  if (h->cancelled) return false;
  return !h->group || (!h->group->cancelled && h->group->generation.load() == h->generation);
}

void releaseHandle(Handle* h) { delete h; }

}  // namespace marigold::http::transport
