// Settings' levels (Build 144): every page opens in Settings' middle column
// (the categories stay on the left, dimmed), one level deeper each time.
// What Back does at each level, the line above the title, rows that ask
// on themselves before something that can't be undone, and finding the
// row you came from again after the rows are rebuilt. No drawing here.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace marigold::settings_nav {

// "SETTINGS › PROVIDERS › BACKUP TV": "Settings" and the levels' names, in
// capitals (ASCII letters only; the rest stays as it is).
std::string crumb(const std::vector<std::string>& names);

// What Back does, from the inside out: a row asking first stops asking;
// a page goes back one level (to the row it was opened from); the
// category's rows go back to the categories; the categories close
// Settings. `opened`: Settings was opened at the page of that depth from
// outside (Quick settings' Edit My Lineup): Back there closes Settings,
// back to where it was opened from.
enum class Back { Disarm, UpOneLevel, ToCategories, Close };
Back back(bool armed, size_t depth, bool inRows, size_t opened = 0);

// A row that asks before it acts: OK turns it red ("Remove Backup TV?"),
// OK again acts; Back, moving to another row or the rows changing under it
// cancels. The rows are rebuilt after every change (and once a second
// where they show live state), so it is kept by row and title.
class Arm {
 public:
  bool armed() const { return row_ >= 0; }
  int row() const { return row_; }
  bool on(int row) const { return row_ >= 0 && row == row_; }
  void arm(int row, std::string title) {
    row_ = row;
    title_ = std::move(title);
  }
  void clear() {
    row_ = -1;
    title_.clear();
  }
  // After a rebuild or a move: kept only on the same row with the same
  // title, and only while it has the focus.
  void keepIf(int focus, const std::string& titleAtRow) {
    if (row_ >= 0 && (focus != row_ || titleAtRow != title_)) clear();
  }

 private:
  int row_ = -1;
  std::string title_;
};

// The row to focus after the rows were rebuilt on coming back (from a
// full-screen task such as Add a provider): the row with the same title
// where it still is (a provider added above it moves it down), else the
// same place.
int refocus(const std::vector<std::string>& titles, int index, const std::string& title);

}  // namespace marigold::settings_nav
