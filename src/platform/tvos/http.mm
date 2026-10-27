// tvOS: one transfer over NSURLSession, for the shared worker pool. The
// session's delegate queue only collects what arrives (the response, the
// body's pieces, the end); the worker thread that asked hands the pieces to
// their consumer, decides the caps and polls for cancellation, as on Android.
// A streamed body that arrives faster than it is used pauses the download.
// Groups and handles: http_groups.cpp.
#import <Foundation/Foundation.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "platform/http_transport.hpp"
#include "platform/tvos/bridge.hpp"

namespace marigold::http::transport {
namespace {

constexpr size_t kPauseAbove = 4u << 20;   // queued body bytes: pause the download
constexpr size_t kResumeBelow = 1u << 20;  // and go on below this
constexpr const char* kUserAgent = "Marigold/0.1 (tvOS)";

struct Call {
  std::mutex mu;
  std::condition_variable cv;
  bool follow = true;
  bool insecure = false;  // a personal server (Emby on the LAN) with its own certificate
  bool head = false, done = false, paused = false;
  int status = 0, retryAfter = 0;
  std::string mime, headers, error;
  std::deque<std::string> pieces;
  size_t queued = 0;
};

std::mutex gCallsMu;
std::unordered_map<NSUInteger, std::shared_ptr<Call>> gCalls;

std::shared_ptr<Call> callFor(NSURLSessionTask* task) {
  std::lock_guard<std::mutex> lock(gCallsMu);
  auto it = gCalls.find(task.taskIdentifier);
  return it == gCalls.end() ? nullptr : it->second;
}

std::string errorCode(NSError* e) {
  if (!e) return "";
  if (![e.domain isEqualToString:NSURLErrorDomain]) return "transport";
  switch (e.code) {
    case NSURLErrorTimedOut: return "timeout";
    case NSURLErrorCancelled: return "cancelled";
    case NSURLErrorCannotFindHost:
    case NSURLErrorDNSLookupFailed: return "transport (dns)";
    case NSURLErrorCannotConnectToHost:
    case NSURLErrorNetworkConnectionLost:
    case NSURLErrorNotConnectedToInternet: return "transport (connect)";
    case NSURLErrorSecureConnectionFailed:
    case NSURLErrorServerCertificateHasBadDate:
    case NSURLErrorServerCertificateUntrusted:
    case NSURLErrorServerCertificateHasUnknownRoot:
    case NSURLErrorServerCertificateNotYetValid:
    case NSURLErrorClientCertificateRejected:
    case NSURLErrorClientCertificateRequired: return "transport (tls)";
    case NSURLErrorAppTransportSecurityRequiresSecureConnection: return "transport (ats)";  // Info.plist
    case NSURLErrorBadURL:
    case NSURLErrorUnsupportedURL: return "transport (url)";
    case NSURLErrorHTTPTooManyRedirects: return "redirect";
    default: return "transport";
  }
}

// "text/html; charset=utf-8" -> "text/html", as Legacy's rede does.
std::string mimeOf(NSString* type) {
  std::string t = type ? std::string(type.lowercaseString.UTF8String ?: "") : std::string();
  size_t semi = t.find(';');
  if (semi != std::string::npos) t.resize(semi);
  std::string out;
  for (char c : t)
    if (c != ' ' && c != '\t') out += c;
  return out;
}

}  // namespace
}  // namespace marigold::http::transport

@interface MGHttpDelegate : NSObject <NSURLSessionDataDelegate>
@end

@implementation MGHttpDelegate

- (void)URLSession:(NSURLSession*)session
                          task:(NSURLSessionTask*)task
    willPerformHTTPRedirection:(NSHTTPURLResponse*)response
                    newRequest:(NSURLRequest*)request
             completionHandler:(void (^)(NSURLRequest*))completionHandler {
  auto call = marigold::http::transport::callFor(task);
  // Not following: the 3xx itself is the answer.
  completionHandler(call && call->follow ? request : nil);
}

- (void)URLSession:(NSURLSession*)session
                   task:(NSURLSessionTask*)task
    didReceiveChallenge:(NSURLAuthenticationChallenge*)challenge
      completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential*))completionHandler {
  auto call = marigold::http::transport::callFor(task);
  SecTrustRef trust = challenge.protectionSpace.serverTrust;
  if (call && call->insecure && trust &&
      [challenge.protectionSpace.authenticationMethod isEqualToString:NSURLAuthenticationMethodServerTrust]) {
    completionHandler(NSURLSessionAuthChallengeUseCredential, [NSURLCredential credentialForTrust:trust]);
    return;
  }
  completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)task
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
  auto call = marigold::http::transport::callFor(task);
  if (call) {
    std::lock_guard<std::mutex> lock(call->mu);
    if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
      NSHTTPURLResponse* http = (NSHTTPURLResponse*)response;
      call->status = static_cast<int>(http.statusCode);
      std::string block = "HTTP/1.1 " + std::to_string(call->status) + "\r\n";
      for (NSString* name in http.allHeaderFields) {
        NSString* value = http.allHeaderFields[name];
        block += std::string(name.UTF8String ?: "") + ": " + std::string(value.description.UTF8String ?: "") + "\r\n";
        if ([name caseInsensitiveCompare:@"Retry-After"] == NSOrderedSame) call->retryAfter = std::max(0, value.intValue);
      }
      call->headers = std::move(block);
    }
    call->mime = marigold::http::transport::mimeOf(response.MIMEType);
    call->head = true;
    call->cv.notify_all();
  }
  completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session dataTask:(NSURLSessionDataTask*)task didReceiveData:(NSData*)data {
  auto call = marigold::http::transport::callFor(task);
  if (!call) return;
  std::lock_guard<std::mutex> lock(call->mu);
  [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange range, BOOL*) {
    call->pieces.emplace_back(static_cast<const char*>(bytes), range.length);
    call->queued += range.length;
  }];
  if (call->queued > marigold::http::transport::kPauseAbove && !call->paused) {
    call->paused = true;
    [task suspend];
  }
  call->cv.notify_all();
}

- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
  auto call = marigold::http::transport::callFor(task);
  if (!call) return;
  std::lock_guard<std::mutex> lock(call->mu);
  call->error = marigold::http::transport::errorCode(error);
  call->done = true;
  call->cv.notify_all();
}

@end

namespace marigold::http::transport {
namespace {

NSURLSession* session() {
  static NSURLSession* s = [] {
    NSURLSessionConfiguration* c = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    c.HTTPShouldSetCookies = NO;
    c.HTTPCookieAcceptPolicy = NSHTTPCookieAcceptPolicyNever;
    c.URLCache = nil;
    c.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    c.HTTPMaximumConnectionsPerHost = 6;
    c.waitsForConnectivity = NO;
    NSOperationQueue* q = [[NSOperationQueue alloc] init];
    q.maxConcurrentOperationCount = 1;
    q.name = @"marigold.http";
    return [NSURLSession sessionWithConfiguration:c delegate:[[MGHttpDelegate alloc] init] delegateQueue:q];
  }();
  return s;
}

unsigned since(std::chrono::steady_clock::time_point start) {
  return static_cast<unsigned>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

}  // namespace

void prepare() { session(); }

Result fetch(const Request& q, Handle*, size_t wireCap, const std::function<bool(std::string_view)>& receive,
             const std::function<bool()>& stop) {
  Result out;
  auto start = std::chrono::steady_clock::now();
  size_t cap = wireCap ? wireCap : receive ? kStreamedMax : kBufferedDefault;
  NSURL* url = marigold::tvos::urlFrom(q.url);
  if (!url) {
    out.error = "transport (url)";
    return out;
  }
  NSMutableURLRequest* r = [NSMutableURLRequest requestWithURL:url];
  NSString* method = q.method.empty() ? (q.body.empty() ? @"GET" : @"POST") : @(q.method.c_str()).uppercaseString;
  r.HTTPMethod = method;
  r.timeoutInterval = std::max(1.0, std::min(q.timeoutMs, 300000u) / 1000.0);
  bool agent = false;
  for (const std::string& line : q.headers) {
    size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) continue;
    NSString* name = [@(line.substr(0, colon).c_str())
        stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    NSString* value = [@(line.substr(colon + 1).c_str())
        stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    if (!name || !value) continue;
    if ([name caseInsensitiveCompare:@"User-Agent"] == NSOrderedSame) agent = true;
    [r setValue:value forHTTPHeaderField:name];
  }
  if (!agent) [r setValue:@(kUserAgent) forHTTPHeaderField:@"User-Agent"];
  if (!q.body.empty() && ![method isEqualToString:@"GET"] && ![method isEqualToString:@"HEAD"])
    r.HTTPBody = [NSData dataWithBytes:q.body.data() length:q.body.size()];

  auto call = std::make_shared<Call>();
  call->follow = q.follow;
  call->insecure = q.personalServer;
  NSURLSessionDataTask* task = [session() dataTaskWithRequest:r];
  {
    std::lock_guard<std::mutex> lock(gCallsMu);
    gCalls[task.taskIdentifier] = call;
  }
  [task resume];

  size_t received = 0;
  bool head = false, overflow = false, stopped = false, cancelled = false;
  std::unique_lock<std::mutex> lock(call->mu);
  while (true) {
    call->cv.wait_for(lock, std::chrono::milliseconds(100),
                      [&] { return call->done || !call->pieces.empty() || (call->head && !head); });
    if (call->head && !head) {
      head = true;
      out.status = call->status;
      out.mime = call->mime;
      out.headers = call->headers;
      out.retryAfterS = call->retryAfter;
    }
    bool give = true;
    while (!call->pieces.empty() && give) {
      std::string piece = std::move(call->pieces.front());
      call->pieces.pop_front();
      call->queued -= piece.size();
      if (received + piece.size() > cap) {
        overflow = true;
        break;
      }
      received += piece.size();
      if (!receive) {
        out.body += piece;
        continue;
      }
      // As rede: a streamed body goes to its consumer only on a 2xx response.
      if (out.status < 200 || out.status >= 300) continue;
      lock.unlock();
      give = receive(piece);
      lock.lock();
      if (!give) stopped = true;
    }
    if (call->paused && call->queued < kResumeBelow && !overflow && !stopped) {
      call->paused = false;
      [task resume];
    }
    if (call->done && call->pieces.empty()) break;
    if (overflow || stopped) break;
    lock.unlock();
    bool halt = stop();
    lock.lock();
    if (halt) {
      cancelled = true;
      break;
    }
  }
  std::string error = call->error;
  lock.unlock();
  if (!call->done) [task cancel];
  {
    std::lock_guard<std::mutex> calls(gCallsMu);
    gCalls.erase(task.taskIdentifier);
  }
  out.ms = since(start);
  if (overflow) out.error = "body-cap";
  else if (cancelled) out.error = "cancelled";
  else if (stopped) out.error = error.empty() || error == "cancelled" ? "transport (stopped)" : error;
  else out.error = error;
  return out;
}

}  // namespace marigold::http::transport

namespace marigold::tvos {

NSURL* urlFrom(const std::string& link) {
  NSString* s = [NSString stringWithUTF8String:link.c_str()];
  if (!s) return nil;
  NSURL* url = [NSURL URLWithString:s];
  if (!url) {
    // Lists carry links with spaces and the like, which other players accept:
    // encode what a URL can't hold, keeping what is already encoded.
    NSMutableCharacterSet* keep = [NSCharacterSet.URLQueryAllowedCharacterSet mutableCopy];
    [keep formUnionWithCharacterSet:NSCharacterSet.URLPathAllowedCharacterSet];
    [keep addCharactersInString:@"%#"];
    NSString* encoded = [s stringByAddingPercentEncodingWithAllowedCharacters:keep];
    url = encoded ? [NSURL URLWithString:encoded] : nil;
  }
  return url && url.scheme && url.host ? url : nil;
}

}  // namespace marigold::tvos
