#include "core/input/player_rail.hpp"

#include <algorithm>

namespace marigold::rail {

std::vector<Item> playerLists(bool allChannels, bool liveEvents) {
  std::vector<Item> out = {Item::Lineup, Item::Favorites, Item::Recent};
  if (liveEvents) out.push_back(Item::Events);
  if (allChannels) out.push_back(Item::All);
  return out;
}

PlayerNav::PlayerNav(Item list) : nav_(list) { nav_.setItems(playerLists(false, true)); }

PlayerStep PlayerNav::of(const Step& s) {
  using Do = PlayerStep::Do;
  switch (s.what) {
    case Step::Do::Scroll: return {Do::Scroll, s.index, s.item};
    case Step::Do::Restore: return {Do::Restore, s.index, s.item};
    case Step::Do::Top: return {Do::Top, s.index, s.item};
    case Step::Do::Open: return {Do::Open, s.index, s.item};
    // The main guide's menu Back (My Lineup, the way out) isn't used here.
    case Step::Do::None:
    case Step::Do::Home:
    case Step::Do::Exit: break;
  }
  return {Do::None, s.index, s.item};
}

PlayerStep PlayerNav::key(Key k, const Grid& g, uint32_t glide) {
  using Do = PlayerStep::Do;
  Mode m = nav_.mode();
  if (m == Mode::Content) {
    if (k == Key::Back) {
      if (g.digits) return {Do::Digits};
      if (g.later) return {Do::Now};
      return {Do::Close};
    }
    if (k == Key::Left && g.firstShow && !g.later) {
      // A swipe that walked the grid here stops at its edge.
      if (glide && glide == glide_) return {Do::None};
      if (glide) glide_ = glide;
      return edge(g.origin, g.count);
    }
    if (glide) glide_ = glide;
    return {Do::Grid};
  }
  // The column and the lists: one step per swipe.
  if (glide) {
    if (glide == glide_) return {Do::None};
    glide_ = glide;
  }
  if (m == Mode::Menu) {
    if (k == Key::Back) return closeLists();
    if (k == Key::Left) return {Do::None};
    return of(nav_.key(k));
  }
  // The column: Left opens the lists over the grid put back as it was.
  if (k == Key::Left) return openLists();
  return of(nav_.key(k));
}

PlayerStep PlayerNav::edge(int origin, int count) {
  nav_.leftEdge(origin, count);
  fromColumn_ = false;
  return {nav_.mode() == Mode::Column ? PlayerStep::Do::Column : PlayerStep::Do::None, nav_.origin(), list()};
}

PlayerStep PlayerNav::openColumn(int origin, int count) {
  nav_.openColumn(origin, count);
  return {nav_.mode() == Mode::Column ? PlayerStep::Do::Column : PlayerStep::Do::None, nav_.origin(), list()};
}

PlayerStep PlayerNav::openLists() {
  Mode m = nav_.mode();
  if (m == Mode::Menu) return {PlayerStep::Do::None};
  fromColumn_ = m == Mode::Column;
  if (fromColumn_) return of(nav_.key(Key::Left));  // Restore, and the lists
  nav_.openMenu();
  return {PlayerStep::Do::None};
}

PlayerStep PlayerNav::clickColumn(int i) { return of(nav_.clickColumn(i)); }

PlayerStep PlayerNav::pickList(int i) {
  const std::vector<Item>& items = nav_.items();
  if (nav_.mode() != Mode::Menu || items.empty()) return {PlayerStep::Do::None};
  return of(nav_.pick(items[static_cast<size_t>(std::clamp(i, 0, static_cast<int>(items.size()) - 1))]));
}

PlayerStep PlayerNav::closeLists() {
  if (nav_.mode() != Mode::Menu) return {PlayerStep::Do::None};
  // Back to the column it came from, on the neighborhood it opened on (the
  // grid is already back there); a list without one: the grid.
  if (fromColumn_) return openColumn(nav_.origin(), nav_.count());
  nav_.close();
  return {PlayerStep::Do::None};
}

}  // namespace marigold::rail
