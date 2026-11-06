// Settings › Help & info › Send a report to your phone (Build 144: in
// place). The channel report, the playback log or the Live Events report,
// served on the home network for 10 minutes: OK on a report builds and
// shares it (its code, link and time left in the right column), OK again
// stops; OK on another report shares that one instead. The link is shown
// on screen only, never logged. Sharing stops when the page closes.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "app/screens/screens.hpp"
#include "core/input/wake.hpp"
#include "core/report/qr.hpp"
#include "platform/lan_share.hpp"

namespace marigold::app {

// "Channel report", "Playback log", "Live Events report".
const char* reportName(ExportKind kind);

class ReportShare {
 public:
  explicit ReportShare(App& app) : app_(app) {}
  ~ReportShare();
  ReportShare(const ReportShare&) = delete;
  ReportShare& operator=(const ReportShare&) = delete;

  // OK on a report: shares it (instead of the one before), or stops it
  // when it is the one building or shared.
  void toggle(ExportKind kind);
  void start(ExportKind kind);
  void stop();

  // The report building, shared or that couldn't be shared.
  std::optional<ExportKind> kind() const { return kind_; }
  bool building() const { return building_; }
  bool sharing() const { return status_.state == platform::LanShare::State::Ready; }
  const std::string& problem() const { return problem_; }
  // "9:41": how long the link still works.
  std::string timeLeft() const;
  // Its row's second line while it is the one: "Building the report…",
  // "Sharing now · 9:41 left", or that it couldn't.
  std::string rowLine() const;

  // Once a loop: true when something shown changed (the time left, the
  // spinner's step, the link expiring).
  bool tick(uint32_t ms);
  uint32_t wakeAt() const { return wake_.get(); }

  // The right column: the report's name, then its code, link and time
  // left (or the spinner while it builds, or what went wrong).
  void drawInfo(ui::Ctx& c, float x, float y, float w) const;

 private:
  void serve(std::string body);

  App& app_;
  std::optional<ExportKind> kind_;
  platform::LanShare share_;
  platform::LanShare::Status status_;
  std::optional<report::QrMatrix> qr_;
  std::string problem_;
  bool building_ = false;
  uint64_t serial_ = 0;  // a build finishing for a report no longer wanted is dropped
  wake::Due wake_;
  wake::Steps spinner_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace marigold::app
