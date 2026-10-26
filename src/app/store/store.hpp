// The data store: accounts, channel lists, the catalog, My Lineup and guide
// listings. Network and parsing run on the pool; results become immutable
// snapshots swapped in on the UI thread (see model.hpp).
//
// Request budget (docs/plan.md §6): channel lists re-read every 30 minutes,
// the provider guide every 12 hours, outside guides every 20 hours with
// ETag, and short EPG only for rows on screen that have no full guide.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <string>
#include <vector>

#include "app/store/model.hpp"

namespace marigold {
struct Channel;
struct Program;
namespace catalog {
struct CatalogResult;
struct LineupPrefs;
namespace cache {
struct Saved;
}
}  // namespace catalog
namespace sources {
struct Account;
}
}  // namespace marigold

namespace marigold::app {

class App;
struct Fetched;
struct OpenStream;

// A country / language of the channel list, for the lineup setup.
struct MarketSummary {
  std::string code;   // "US", "US-ES", "MX"...
  std::string name;   // "United States (English)"
  int count = 0;      // channels
  bool on = false;
};

// A lineup category with how many channels it would suggest.
struct CategorySummary {
  std::string key;  // saved value ("local", "news"...)
  std::string title, detail;
  int suggested = 0, total = 0;
  bool on = false;
};

struct AccountSummary {
  std::string id;
  std::string name;   // the name the person gave it
  std::string kind;   // "Xtream", "Emby", "M3U playlist"
  std::string state;  // "1,240 channels", "Couldn't sign in"...
};

// Fields of the add-account form. Passwords and playlist links go to the
// secrets file only.
struct AccountForm {
  enum class Kind { Xtream, Emby, M3U } kind = Kind::Xtream;
  std::string name;
  std::string server;    // Xtream/Emby base address, or the M3U playlist link
  std::string user;
  std::string password;
  std::string guideUrl;  // optional XMLTV link (M3U)
};

struct Settings {
  bool previewChannels = true;  // the guide corner plays the focused channel
  bool allChannelsTab = false;
  bool preferTS = true;         // Xtream: MPEG-TS rather than HLS
  bool fillMissingListings = true;  // outside guides for channels without one
};

class Store {
 public:
  explicit Store(App& app);
  ~Store();
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // UI thread, once per loop. True when a snapshot changed.
  bool tick(uint32_t ms);

  std::shared_ptr<const Channels> channels() const { return channels_; }
  std::shared_ptr<const Listings> listings() const { return listings_; }
  const StoreStatus& status() const { return status_; }

  // Accounts.
  std::vector<AccountSummary> accounts() const;
  // Checks the login with one request, then saves and loads. `done` runs on
  // the UI thread with an empty string or a message to show.
  void addAccount(const AccountForm& form, std::function<void(std::string)> done);
  void removeAccount(const std::string& id);
  void refreshAll();

  // Guide rows on screen: fetches short EPG for those with no listings,
  // a few rows at a time. Cheap to call every frame.
  void wantListings(const std::vector<std::string>& entryIds);

  // Personal lists.
  void toggleFavorite(const std::string& entryId);
  void toggleLineup(const std::string& entryId);
  void noteWatched(const std::string& entryId);

  // Feeds for an entry, best first ("Always start with" honoured).
  std::vector<Feed> feeds(const std::string& entryId) const;
  // Emby: asks the server to open a feed (needsOpen); `done` gets it with
  // its URL, or nothing. Closes the stream opened before.
  void open(const Feed& feed, std::function<void(std::optional<Feed>)> done);
  void closeStream();
  // Feed outcomes move reliable sources up.
  void recordFeed(const std::string& channelId, bool worked);
  void pinFeed(const std::string& entryId, const std::string& channelId);  // "" = automatic
  std::string pinnedFeed(const std::string& entryId) const;

  // My Lineup setup.
  std::vector<MarketSummary> markets() const;
  std::vector<CategorySummary> categories(const std::set<std::string>& countries, bool requireGuide) const;
  void buildLineup(const std::set<std::string>& countries, const std::set<std::string>& categoryKeys,
                   bool requireGuide);
  bool lineupBuilding() const;
  static std::string marketName(const std::string& code);

  const Settings& settings() const { return settings_; }
  void setSettings(const Settings& s);

  struct Impl;

 private:
  void saveAccounts();
  void saveLists();
  void saveM3uStreams();
  void setPrefs(const catalog::LineupPrefs& p);
  std::string currentSignature() const;
  void reset();
  void load(bool force);
  void installSaved(catalog::cache::Saved saved, bool partial);
  void downloadChannels(uint64_t gen);
  void channelsDownloaded(uint64_t gen, std::shared_ptr<std::vector<Fetched>> parts);
  void installCatalog(std::shared_ptr<const std::vector<Channel>> list,
                      std::shared_ptr<const catalog::CatalogResult> cat, bool partial);
  void saveCatalog();
  void rebuildEntries();
  time::Seconds windowStart() const;
  bool entryHasGuide(const std::string& id) const;
  void startGuideFetches();
  std::vector<std::string> entriesOf(const std::vector<std::string>& rawIds) const;
  bool providerCovers(const std::string& id, time::Seconds start) const;
  void landed(const std::vector<std::string>& ids,
              const std::unordered_map<std::string, std::vector<Program>>* results, time::Seconds start);
  void fetchXtreamGuide(const std::vector<std::string>& ids, const sources::Account& account);
  void fetchEmbyGuide(const std::vector<std::string>& ids, const sources::Account& account);
  std::vector<Program> providerListing(const std::string& id) const;
  std::vector<Program> outsideListing(const std::string& id) const;
  std::vector<Program> bestListing(const std::string& id) const;
  void applyEntryGuides(const std::vector<std::string>& entryIDs);
  void rebuildOutsidePrograms();
  void startGuides(bool requestRows);
  void readSavedListings();
  void installProviderSaved();
  void scheduleProvider(bool force);
  void scheduleOutside(bool force);
  void scheduleLineupRefresh();
  void runProviderGuide();
  void runOutsideGuide();
  void publishChannels();
  void publishListings();

  App& app_;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<const Channels> channels_ = std::make_shared<Channels>();
  std::shared_ptr<const Listings> listings_ = std::make_shared<Listings>();
  StoreStatus status_;
  Settings settings_;
  std::shared_ptr<OpenStream> openStream_;
};

}  // namespace marigold::app
