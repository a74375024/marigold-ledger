#include "app/screen.hpp"

namespace marigold::app {

void Navigator::push(std::unique_ptr<Screen> s) {
  if (s) pendingPush_.push_back(std::move(s));
}

void Navigator::pop() { ++pendingPops_; }

void Navigator::popTo(const Screen* s) { popTarget_ = s; }

void Navigator::replaceAll(std::unique_ptr<Screen> s) { pendingReplace_ = std::move(s); }

bool Navigator::settle() {
  graveyard_.clear();
  bool changed = false;
  if (pendingReplace_) {
    for (auto& s : stack_) graveyard_.push_back(std::move(s));
    stack_.clear();
    stack_.push_back(std::move(pendingReplace_));
    pendingPops_ = 0;
    popTarget_ = nullptr;
    changed = true;
  }
  if (popTarget_) {
    while (!stack_.empty() && stack_.back().get() != popTarget_) {
      graveyard_.push_back(std::move(stack_.back()));
      stack_.pop_back();
      changed = true;
    }
    popTarget_ = nullptr;
  }
  while (pendingPops_ > 0 && stack_.size() > 1) {
    graveyard_.push_back(std::move(stack_.back()));
    stack_.pop_back();
    --pendingPops_;
    changed = true;
  }
  pendingPops_ = 0;
  for (auto& s : pendingPush_) {
    stack_.push_back(std::move(s));
    changed = true;
  }
  bool pushed = !pendingPush_.empty();
  pendingPush_.clear();
  if (changed && !stack_.empty()) {
    if (pushed || !graveyard_.empty()) stack_.back()->shown();
  }
  return changed;
}

const Screen* Navigator::topOpaque() const {
  for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
    if ((*it)->opaque()) return it->get();
  return stack_.empty() ? nullptr : stack_.front().get();
}

void Navigator::draw(ui::Ctx& c) {
  if (stack_.empty()) return;
  size_t first = stack_.size() - 1;
  while (first > 0 && !stack_[first]->opaque()) --first;
  for (size_t i = first; i < stack_.size(); ++i) {
    // Only the top screen registers pointer targets.
    std::vector<ui::HitRegion>* hits = c.hits;
    bool covered = c.covered;
    if (i + 1 != stack_.size()) {
      c.hits = nullptr;
      c.covered = true;
    }
    stack_[i]->draw(c);
    c.hits = hits;
    c.covered = covered;
  }
}

}  // namespace marigold::app
