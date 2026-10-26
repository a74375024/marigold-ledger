// Providers: the list of accounts and the add form (Xtream, Emby or an M3U
// playlist). Text goes in with the TV keyboard. Passwords show as dots and,
// like playlist links, are kept in the secrets file only.
#include <algorithm>

#include "app/app.hpp"
#include "app/screens/guide_screen.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

class AccountsScreen : public Screen {
 public:
  AccountsScreen(App& app, bool firstRun) : Screen(app), firstRun_(firstRun) {
    if (firstRun_ || app.store().accounts().empty()) startForm();
  }

  void shown() override { list_ = app_.store().accounts(); }

  bool key(const KeyEvent& e) override {
    if (busy_) return true;
    return form_ ? formKey(e) : listKey(e);
  }

  void text(const std::string& value, bool closed, bool confirmed) override {
    if (!form_ || editing_ < 0) return;
    std::string* f = field(editing_);
    if (f) *f = value;
    if (closed) {
      editing_ = -1;
      if (confirmed) focus_ = std::min(focus_ + 1, fieldCount() + 1);
    }
  }

  void hover(int id) override {
    if (id >= 10 && id < 40) focus_ = id - 10;
  }

  void draw(ui::Ctx& c) override {
    if (form_) drawForm(c);
    else drawList(c);
  }

  bool tick(uint32_t) override { return busy_; }

 private:
  // ----- list
  bool listKey(const KeyEvent& e) {
    int n = static_cast<int>(list_.size()) + 1;  // + "Add a Provider"
    switch (e.key) {
      case Key::Up:
        focus_ = std::max(0, focus_ - 1);
        return true;
      case Key::Down:
        focus_ = std::min(n - 1, focus_ + 1);
        return true;
      case Key::Ok:
        if (focus_ == static_cast<int>(list_.size())) startForm();
        else if (focus_ < static_cast<int>(list_.size())) accountMenu(list_[static_cast<size_t>(focus_)]);
        return true;
      default:
        return e.key != Key::Back;
    }
  }

  void accountMenu(const AccountSummary& a) {
    App& app = app_;
    std::string id = a.id, name = a.name;
    app_.nav().push(std::make_unique<PanelScreen>(app_, PanelScreen::Side::Trailing, 640.0f,
                                                  [&app, id, name](ui::ListPanel& p) {
      p.title = name;
      ui::PanelItem reload{ui::PanelItem::Kind::Action, "Reload now"};
      reload.select = [&app] {
        app.store().refreshAll();
        app.nav().pop();
      };
      p.items.push_back(reload);
      ui::PanelItem remove{ui::PanelItem::Kind::Action, "Remove this provider"};
      remove.detail = "Its channels leave the guide; My Lineup keeps the rest";
      remove.select = [&app, id] {
        app.store().removeAccount(id);
        app.nav().pop();
        app.toast("Provider removed");
      };
      p.items.push_back(remove);
    }));
  }

  void drawList(ui::Ctx& c) {
    float x = 120, y = 80;
    ui::label(c, "Providers", x, y, Weight::Bold, 52, theme::white());
    y += 84;
    ui::paragraph(c, "Where your channels come from. Each one keeps its own login on this TV.", x, y, 1100,
                  Weight::Regular, 24, theme::white(0.65f), 2);
    y += 80;
    for (size_t i = 0; i <= list_.size(); ++i) {
      Rect r{x, y, 1100, 96};
      c.hit(r, 10 + static_cast<int>(i));
      bool f = static_cast<int>(i) == focus_;
      c.g.fill(r, f ? theme::white() : theme::white(0.07f), theme::kRowRadius);
      if (i < list_.size()) {
        const AccountSummary& a = list_[i];
        ui::label(c, a.name, r.x + 32, r.y + 16, Weight::Bold, 27, f ? theme::black() : theme::white(), 700);
        ui::label(c, a.kind + " \xc2\xb7 " + a.state, r.x + 32, r.y + 56, Weight::Medium, 20,
                  f ? theme::black(0.6f) : theme::white(0.6f), 900);
      } else {
        ui::labelIn(c, "+  Add a Provider", {r.x + 32, r.y, r.w - 64, r.h}, Weight::Bold, 27,
                    f ? theme::black() : theme::white());
      }
      y += 108;
    }
  }

  // ----- form
  void startForm() {
    form_ = true;
    focus_ = 0;
    editing_ = -1;
    error_.clear();
    data_ = AccountForm{};
  }

  int fieldCount() const { return data_.kind == AccountForm::Kind::M3U ? 3 : 4; }
  // Rows: 0 kind, 1.. fields, then Sign In.
  std::string* field(int row) {
    bool m3u = data_.kind == AccountForm::Kind::M3U;
    switch (row) {
      case 1: return &data_.name;
      case 2: return &data_.server;
      case 3: return m3u ? &data_.guideUrl : &data_.user;
      case 4: return m3u ? nullptr : &data_.password;
      default: return nullptr;
    }
  }
  std::string fieldTitle(int row) const {
    bool m3u = data_.kind == AccountForm::Kind::M3U;
    switch (row) {
      case 1: return "Name";
      case 2: return m3u ? "Playlist link" : "Server address";
      case 3: return m3u ? "Guide link (optional)" : "User name";
      case 4: return "Password";
      default: return "";
    }
  }

  bool formKey(const KeyEvent& e) {
    int last = fieldCount() + 1;  // Sign In row
    switch (e.key) {
      case Key::Up:
        focus_ = std::max(0, focus_ - 1);
        return true;
      case Key::Down:
        focus_ = std::min(last, focus_ + 1);
        return true;
      case Key::Left:
      case Key::Right:
        if (focus_ == 0) {
          int k = static_cast<int>(data_.kind) + (e.key == Key::Right ? 1 : 2);
          data_.kind = static_cast<AccountForm::Kind>(k % 3);
        }
        return true;
      case Key::Ok:
        if (focus_ == 0) {
          data_.kind = static_cast<AccountForm::Kind>((static_cast<int>(data_.kind) + 1) % 3);
        } else if (focus_ == last) {
          submit();
        } else if (std::string* f = field(focus_)) {
          editing_ = focus_;
          if (!app_.openKeyboard(*f)) app_.toast("This TV has no on-screen keyboard");
        }
        return true;
      case Key::Back:
        if (app_.keyboardOpen()) {
          app_.closeKeyboard();
          return true;
        }
        if (!list_.empty() || !app_.store().accounts().empty()) {
          form_ = false;
          list_ = app_.store().accounts();
          focus_ = 0;
          return true;
        }
        return !firstRun_ ? false : true;
      default:
        return true;
    }
  }

  void submit() {
    if (str_empty(data_.server)) {
      error_ = data_.kind == AccountForm::Kind::M3U ? "Enter the playlist link." : "Enter the server address.";
      return;
    }
    if (data_.kind != AccountForm::Kind::M3U && (str_empty(data_.user) || data_.password.empty())) {
      error_ = "Enter the user name and password.";
      return;
    }
    busy_ = true;
    error_.clear();
    App& app = app_;
    bool first = firstRun_;
    AccountsScreen* self = this;
    std::weak_ptr<bool> alive = alive_;
    app_.store().addAccount(data_, [&app, self, first, alive](std::string problem) {
      if (alive.expired()) return;  // the screen was closed meanwhile
      self->busy_ = false;
      if (!problem.empty()) {
        self->error_ = problem;
        return;
      }
      self->data_.password.clear();
      if (first) {
        app.nav().replaceAll(std::make_unique<GuideScreen>(app));
      } else {
        self->form_ = false;
        self->list_ = app.store().accounts();
        self->focus_ = 0;
        app.toast("Provider added. Loading channels\xe2\x80\xa6");
      }
    });
  }

  static bool str_empty(const std::string& s) {
    return std::all_of(s.begin(), s.end(), [](char ch) { return ch == ' ' || ch == '\t'; });
  }

  void drawForm(ui::Ctx& c) {
    float x = 120, y = 80;
    ui::label(c, firstRun_ ? "Welcome to Marigold" : "Add a Provider", x, y, Weight::Bold, 52, theme::white());
    y += 84;
    ui::paragraph(c,
                  firstRun_ ? "Add the provider your channels come from. You can add more later in Options."
                            : "Channels from every provider you add are merged into one guide.",
                  x, y, 1200, Weight::Regular, 24, theme::white(0.65f), 2);
    y += 86;
    static const char* kKinds[] = {"Xtream", "Emby / Jellyfin", "M3U playlist"};
    int last = fieldCount() + 1;
    for (int row = 0; row <= last; ++row) {
      bool f = row == focus_;
      if (row == last) {
        y += 20;
        float w = ui::chipWidth(c, busy_ ? "Signing in\xe2\x80\xa6" : "Sign In", 26) + 60;
        Rect b{x, y, w, 70};
        c.hit(b, 10 + row);
        c.g.fill(b, f ? theme::white() : theme::white(0.14f), 35);
        ui::labelIn(c, busy_ ? "Signing in\xe2\x80\xa6" : "Sign In", b, Weight::Bold, 26, f ? theme::black() : theme::white(),
                    Align::Center);
        if (busy_) ui::spinner(c, b.x + b.w + 40, b.y + 35, 14);
        y += 90;
        break;
      }
      Rect r{x, y, 1200, 84};
      c.hit(r, 10 + row);
      c.g.fill(r, f ? theme::white() : theme::white(0.07f), theme::kRowRadius);
      Color fg = f ? theme::black() : theme::white();
      Color sub = f ? theme::black(0.55f) : theme::white(0.55f);
      if (row == 0) {
        ui::label(c, "Kind", r.x + 30, r.y + 12, Weight::Medium, 18, sub);
        std::string k = std::string("\xe2\x97\x80  ") + kKinds[static_cast<int>(data_.kind)] + "  \xe2\x96\xb6";
        ui::label(c, k, r.x + 30, r.y + 40, Weight::Bold, 26, fg);
      } else {
        ui::label(c, fieldTitle(row), r.x + 30, r.y + 12, Weight::Medium, 18, sub);
        std::string* v = field(row);
        std::string shown = v ? *v : std::string();
        if (row == 4 && !shown.empty()) shown = std::string(std::min<size_t>(shown.size(), 24), '*');
        if (row == 2 && !shown.empty() && editing_ != row) shown = maskLink(shown);
        if (shown.empty()) shown = editing_ == row ? "" : "Press OK to type";
        ui::label(c, shown, r.x + 30, r.y + 40, Weight::Bold, 26, v && !v->empty() ? fg : sub, r.w - 60);
      }
      y += 96;
    }
    if (!error_.empty()) ui::label(c, error_, x, y, Weight::Medium, 24, theme::hex(theme::kWatching), 1200);
  }

  // Shows only the scheme and host of a link, so a playlist login in the
  // path or query never appears on screen.
  static std::string maskLink(const std::string& s) {
    size_t scheme = s.find("://");
    size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    size_t end = s.find_first_of("/?#", start);
    size_t at = s.rfind('@', end == std::string::npos ? s.size() : end);
    if (at != std::string::npos && at >= start) start = at + 1;
    std::string host = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return end == std::string::npos ? host : host + "/\xe2\x80\xa6";
  }

  using Color = ui::Color;

  bool firstRun_;
  bool form_ = false;
  bool busy_ = false;
  int focus_ = 0;
  int editing_ = -1;
  std::string error_;
  AccountForm data_;
  std::vector<AccountSummary> list_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace

std::unique_ptr<Screen> makeAccounts(App& app, bool firstRun) {
  return std::make_unique<AccountsScreen>(app, firstRun);
}

}  // namespace marigold::app
