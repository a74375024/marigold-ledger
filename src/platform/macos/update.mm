// The Mac (Build 148): the checked .dmg is opened out of sight, its
// Marigold.app copied next to the running one, and the image closed; a
// small helper then waits for this app to quit, puts the new app in the old
// one's place (the same path), and opens it. The app quits the usual way
// (Updates::quitWanted). The app is signed ad hoc and the download is the
// app's own (not quarantined), so the new one opens like the old. A folder
// the app may not change (another user's, or the app run from where it was
// downloaded) fails with a line saying so.
#import <Foundation/Foundation.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

#include "platform/update.hpp"

extern char** environ;

namespace marigold::platform::update {
namespace {

constexpr const char* kAsset = "marigold-mac.dmg";
constexpr const char* kFolderLine = "Couldn't update. Marigold's folder can't be changed.";
constexpr const char* kMoveLine = "Couldn't update. Move Marigold to Applications first.";

// The helper: waits for the app (pid $1) to end (5 minutes at most), swaps
// the new app ($3) in at the old one's path ($2), removes the download ($4)
// and itself, and opens the app (the old one if the swap failed).
constexpr const char* kHelper = R"SH(#!/bin/sh
pid="$1"; app="$2"; new="$3"; dmg="$4"
n=0
while kill -0 "$pid" 2>/dev/null; do
  n=$((n + 1))
  if [ "$n" -gt 600 ]; then rm -rf "$new"; rm -f "$0"; exit 1; fi
  sleep 0.5
done
old="$app.old-$$"
if mv "$app" "$old"; then
  if mv "$new" "$app"; then rm -rf "$old"; else mv "$old" "$app"; fi
fi
rm -rf "$new"
rm -f "$dmg"
/usr/bin/open "$app"
rm -f "$0"
)SH";

std::string utf8(NSString* s) { return s ? std::string(s.UTF8String ?: "") : std::string(); }

// Runs a program and waits for it; true when it exits with 0. Its output goes
// nowhere (hdiutil's names paths).
bool run(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  posix_spawn_file_actions_t files;
  posix_spawn_file_actions_init(&files);
  posix_spawn_file_actions_addopen(&files, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&files, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&files, 2, "/dev/null", O_WRONLY, 0);
  pid_t pid = 0;
  int rc = posix_spawn(&pid, argv[0], &files, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&files);
  if (rc != 0) return false;
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Starts the helper in a session of its own, so it outlives the app.
bool startHelper(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  posix_spawn_file_actions_t files;
  posix_spawn_file_actions_init(&files);
  posix_spawn_file_actions_addopen(&files, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&files, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&files, 2, "/dev/null", O_WRONLY, 0);
  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
#ifdef POSIX_SPAWN_SETSID
  posix_spawnattr_setflags(&attr, static_cast<short>(POSIX_SPAWN_SETSID));
#endif
  pid_t pid = 0;
  int rc = posix_spawn(&pid, argv[0], &files, &attr, argv.data(), environ);
  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&files);
  return rc == 0;
}

engine::UpdateInstall failed(std::string why, std::string message = std::string()) {
  engine::UpdateInstall out;
  out.why = std::move(why);
  out.message = std::move(message);
  return out;
}

}  // namespace

std::string asset() { return kAsset; }

std::string file() {
  @autoreleasepool {
    NSString* caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
    if (!caches) return {};
    NSString* dir = [[caches stringByAppendingPathComponent:@"org.marigold.app"] stringByAppendingPathComponent:@"update"];
    if (![NSFileManager.defaultManager createDirectoryAtPath:dir
                                 withIntermediateDirectories:YES
                                                  attributes:@{NSFilePosixPermissions : @0700}
                                                       error:nil])
      return {};
    return utf8([dir stringByAppendingPathComponent:@(kAsset)]);
  }
}

engine::UpdateInstall install(const std::string& path) {
  @autoreleasepool {
    NSFileManager* fm = NSFileManager.defaultManager;
    NSBundle* own = NSBundle.mainBundle;
    NSString* bundle = own.bundlePath;
    if (![bundle.pathExtension isEqualToString:@"app"]) return failed("not run from an app bundle");
    // Run from where it was downloaded (macOS runs it from a read-only copy).
    if ([bundle rangeOfString:@"/AppTranslocation/"].location != NSNotFound)
      return failed("run from a translocated copy", kMoveLine);
    NSString* parent = bundle.stringByDeletingLastPathComponent;
    if (access(parent.fileSystemRepresentation, W_OK) != 0 || access(bundle.fileSystemRepresentation, W_OK) != 0)
      return failed("the app's folder isn't writable", kFolderLine);

    // The image, opened out of sight and read-only, at a folder of our own.
    std::string mountTemplate = utf8([NSTemporaryDirectory() stringByAppendingPathComponent:@"marigold-update.XXXXXX"]);
    std::vector<char> mountBuf(mountTemplate.begin(), mountTemplate.end());
    mountBuf.push_back('\0');
    if (!mkdtemp(mountBuf.data())) return failed("no folder to open the image at");
    std::string mount = mountBuf.data();
    if (!run({"/usr/bin/hdiutil", "attach", "-nobrowse", "-readonly", "-noautoopen", "-noverify", "-mountpoint", mount,
              path})) {
      rmdir(mount.c_str());
      return failed("the image didn't open");
    }
    auto detach = [&mount] {
      if (!run({"/usr/bin/hdiutil", "detach", mount, "-quiet"})) run({"/usr/bin/hdiutil", "detach", mount, "-force"});
      rmdir(mount.c_str());
    };

    // The new app: Marigold.app in the image, and this app (same identifier).
    NSString* source = [@(mount.c_str()) stringByAppendingPathComponent:@"Marigold.app"];
    NSBundle* incoming = [NSBundle bundleWithPath:source];
    if (!incoming || ![incoming.bundleIdentifier isEqualToString:own.bundleIdentifier]) {
      detach();
      return failed("the image holds no Marigold.app");
    }
    // Copied next to the running app (the same disk, so the swap is a move).
    NSString* staged = [parent stringByAppendingPathComponent:@".Marigold-update.app"];
    [fm removeItemAtPath:staged error:nil];
    bool copied = run({"/usr/bin/ditto", utf8(source), utf8(staged)});
    detach();
    if (!copied) {
      [fm removeItemAtPath:staged error:nil];
      return failed("the new app couldn't be copied");
    }

    // The helper takes over once the app has quit.
    NSString* script = [NSTemporaryDirectory() stringByAppendingPathComponent:
                                                   [NSString stringWithFormat:@"marigold-update-%d.sh", getpid()]];
    NSData* text = [NSData dataWithBytes:kHelper length:std::char_traits<char>::length(kHelper)];
    if (![text writeToFile:script atomically:YES]) {
      [fm removeItemAtPath:staged error:nil];
      return failed("the helper couldn't be written");
    }
    if (!startHelper({"/bin/sh", utf8(script), std::to_string(getpid()), utf8(bundle), utf8(staged), path})) {
      [fm removeItemAtPath:staged error:nil];
      [fm removeItemAtPath:script error:nil];
      return failed("the helper didn't start");
    }
    engine::UpdateInstall out;
    out.ok = true;
    out.quit = true;
    return out;
  }
}

}  // namespace marigold::platform::update
