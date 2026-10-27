// tvOS: an Apple TV keeps only about 500 KB for good (the app's defaults);
// files go in Caches, which the system may empty when space runs low. So the
// data folder is Caches/data, as on Android, and what the user made
// (engine::fileKind: Keep) is also kept in the defaults, from where a file
// the system deleted comes back. Passwords and tokens (Secret) go in the
// Keychain. Everything else is rebuilt from the sources, as on a first start.
#include "platform/storage.hpp"

#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

#include "core/engine/host.hpp"
#include "core/util/log.hpp"
#include "platform/files_posix.hpp"

namespace marigold::platform {
namespace {

// Well under the defaults' limit (a warning at 512 KB, the app ends at 1 MB).
constexpr NSUInteger kKeepBudget = 384u * 1024u;
NSString* const kService = @"org.marigold.app";

NSString* keyFor(const std::string& name) { return [@"file." stringByAppendingString:@(name.c_str())]; }

NSData* dataOf(const std::string& s) { return [NSData dataWithBytes:s.data() length:s.size()]; }

std::string stringOf(NSData* d) {
  return d ? std::string(static_cast<const char*>(d.bytes), d.length) : std::string();
}

// What the defaults hold for good, other than `except`.
NSUInteger keptBytes(NSString* except) {
  NSUInteger total = 0;
  NSDictionary* all = NSUserDefaults.standardUserDefaults.dictionaryRepresentation;
  for (NSString* key in all) {
    if (![key hasPrefix:@"file."] || [key isEqualToString:except]) continue;
    id v = all[key];
    if ([v isKindOfClass:[NSData class]]) total += static_cast<NSData*>(v).length;
  }
  return total;
}

NSMutableDictionary* keychainQuery(const std::string& name) {
  return [@{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : kService,
    (__bridge id)kSecAttrAccount : @(name.c_str()),
  } mutableCopy];
}

std::optional<std::string> keychainRead(const std::string& name) {
  NSMutableDictionary* q = keychainQuery(name);
  q[(__bridge id)kSecReturnData] = @YES;
  q[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
  CFTypeRef out = nullptr;
  if (SecItemCopyMatching((__bridge CFDictionaryRef)q, &out) != errSecSuccess || !out) return std::nullopt;
  NSData* d = (__bridge_transfer NSData*)out;
  return stringOf(d);
}

bool keychainWrite(const std::string& name, const std::string& content) {
  NSMutableDictionary* q = keychainQuery(name);
  NSDictionary* change = @{(__bridge id)kSecValueData : dataOf(content)};
  OSStatus st = SecItemUpdate((__bridge CFDictionaryRef)q, (__bridge CFDictionaryRef)change);
  if (st == errSecItemNotFound) {
    q[(__bridge id)kSecValueData] = dataOf(content);
    q[(__bridge id)kSecAttrAccessible] = (__bridge id)kSecAttrAccessibleAfterFirstUnlock;
    st = SecItemAdd((__bridge CFDictionaryRef)q, nullptr);
  }
  if (st != errSecSuccess) {
    static bool warned = false;
    if (!warned) MLOG_W("storage", "Keychain unavailable (%d): secrets kept as a private file", static_cast<int>(st));
    warned = true;
    return false;
  }
  return true;
}

void keychainRemove(const std::string& name) { SecItemDelete((__bridge CFDictionaryRef)keychainQuery(name)); }

}  // namespace

void Storage::init(const std::string& appDir) {
  std::string dir = appDir + "/data";
  if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return;
  dir_ = dir;
}

std::string Storage::path(const std::string& name) const {
  return dir_.empty() ? std::string() : dir_ + "/" + name;
}

std::optional<std::string> Storage::read(const std::string& name) const {
  engine::FileKind kind = engine::fileKind(name);
  if (kind == engine::FileKind::Secret) {
    if (auto v = keychainRead(name)) return v;
  }
  if (!dir_.empty()) {
    if (auto v = files::read(path(name))) return v;
  }
  if (kind != engine::FileKind::Keep) return std::nullopt;
  NSData* kept = [NSUserDefaults.standardUserDefaults dataForKey:keyFor(name)];
  if (!kept) return std::nullopt;
  std::string v = stringOf(kept);
  // The system emptied Caches: the file comes back.
  if (!dir_.empty()) files::writeAtomic(path(name), v, false);
  MLOG_I("storage", "%s restored", name.c_str());
  return v;
}

bool Storage::write(const std::string& name, const std::string& content, bool secret) {
  engine::FileKind kind = engine::fileKind(name);
  if (kind == engine::FileKind::Secret || secret) {
    if (keychainWrite(name, content)) {
      if (!dir_.empty()) unlink(path(name).c_str());
      return true;
    }
    return !dir_.empty() && files::writeAtomic(path(name), content, true);
  }
  bool ok = !dir_.empty() && files::writeAtomic(path(name), content, false);
  if (kind == engine::FileKind::Keep) {
    NSString* key = keyFor(name);
    if (keptBytes(key) + content.size() <= kKeepBudget) {
      [NSUserDefaults.standardUserDefaults setObject:dataOf(content) forKey:key];
      ok = true;
    } else {
      MLOG_W("storage", "%s (%zu bytes) is too big to keep for good; kept until the system clears space",
             name.c_str(), content.size());
    }
  }
  return ok;
}

bool Storage::remove(const std::string& name) {
  keychainRemove(name);
  [NSUserDefaults.standardUserDefaults removeObjectForKey:keyFor(name)];
  return !dir_.empty() && unlink(path(name).c_str()) == 0;
}

}  // namespace marigold::platform
