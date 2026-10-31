#include "core/input/left_rail.hpp"

#include <algorithm>
#include <utility>

namespace marigold::rail {

std::vector<Item> items(bool allChannels) {
  std::vector<Item> out = {Item::Search, Item::Lineup, Item::Favorites, Item::Recent, Item::Events};
  if (allChannels) out.push_back(Item::All);
  out.push_back(Item::MySports);
  out.push_back(Item::Options);
  return out;
}

const char* title(Item i) {
  switch (i) {
    case Item::Search: return "Search";
    case Item::Lineup: return "My Lineup";
    case Item::Favorites: return "Favorites";
    case Item::Recent: return "Recent";
    case Item::Events: return "Live Events";
    case Item::All: return "All Channels";
    case Item::MySports: return "My Sports";
    case Item::Options: return "Settings";
  }
  return "";
}

bool isSection(Item i) {
  return i == Item::Lineup || i == Item::Favorites || i == Item::Recent || i == Item::Events || i == Item::All;
}

bool hasColumn(Item i) { return isSection(i) && i != Item::Recent; }

bool atBottom(Item i) { return i == Item::MySports || i == Item::Options; }

Nav::Nav(Item section) : items_(rail::items(false)), section_(section) {}

void Nav::setItems(std::vector<Item> items) {
  items_ = std::move(items);
  menuFocus_ = std::clamp(menuFocus_, 0, std::max(0, static_cast<int>(items_.size()) - 1));
}

int Nav::indexOf(Item i) const {
  auto it = std::find(items_.begin(), items_.end(), i);
  return it == items_.end() ? 0 : static_cast<int>(it - items_.begin());
}

void Nav::leftEdge(int origin, int count) {
  if (hasColumn(section_) && count > 0) openColumn(origin, count);
  else openMenu();
}

void Nav::openColumn(int origin, int count, bool fromMenu) {
  if (count <= 0) {
    mode_ = Mode::Content;
    return;
  }
  count_ = count;
  origin_ = index_ = std::clamp(origin, 0, count - 1);
  fromMenu_ = fromMenu;
  mode_ = Mode::Column;
}

void Nav::openMenu() {
  mode_ = Mode::Menu;
  menuFocus_ = indexOf(section_);
}

Step Nav::confirm(int i) {
  mode_ = Mode::Content;
  // The return rule: the neighborhood the column opened on (even after
  // moving away and back) puts the focus back where it was.
  if (i == origin_) return {Step::Do::Restore, i, section_};
  return {Step::Do::Top, i, section_};
}

Step Nav::key(Key k) {
  if (mode_ == Mode::Column) {
    switch (k) {
      case Key::Up:
      case Key::Down: {
        int next = std::clamp(index_ + (k == Key::Up ? -1 : 1), 0, count_ - 1);
        if (next == index_) return {};
        index_ = next;
        return {Step::Do::Scroll, index_, section_};
      }
      case Key::Ok:
      case Key::Right: return confirm(index_);
      case Key::Back:
        // Cancels: exactly where you were (and the menu again when the
        // column came from it).
        mode_ = fromMenu_ ? Mode::Menu : Mode::Content;
        if (mode_ == Mode::Menu) menuFocus_ = indexOf(section_);
        return {Step::Do::Restore, origin_, section_};
      case Key::Left:
        // Full left: the menu, over the content put back as it was.
        openMenu();
        return {Step::Do::Restore, origin_, section_};
      default: return {};
    }
  }
  if (mode_ == Mode::Menu) {
    int n = static_cast<int>(items_.size());
    switch (k) {
      case Key::Up:
      case Key::Down:
        menuFocus_ = std::clamp(menuFocus_ + (k == Key::Up ? -1 : 1), 0, n - 1);
        return {};
      case Key::Ok:
      case Key::Right:
        if (n == 0) return {};
        return pick(items_[static_cast<size_t>(menuFocus_)]);
      case Key::Back:
        if (section_ != Item::Lineup) {
          section_ = Item::Lineup;
          mode_ = Mode::Content;
          return {Step::Do::Home, 0, Item::Lineup};
        }
        return {Step::Do::Exit, 0, Item::Lineup};
      default: return {};
    }
  }
  return {};
}

Step Nav::clickColumn(int i) {
  if (mode_ != Mode::Column || count_ <= 0) return {};
  index_ = std::clamp(i, 0, count_ - 1);
  return confirm(index_);
}

Step Nav::pick(Item i) {
  mode_ = Mode::Content;
  return {Step::Do::Open, 0, i};
}

void Nav::hoverMenu(int i) {
  if (mode_ == Mode::Menu) menuFocus_ = std::clamp(i, 0, std::max(0, static_cast<int>(items_.size()) - 1));
}

}  // namespace marigold::rail
