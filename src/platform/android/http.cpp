// Android: one transfer over the system's HttpURLConnection (Bridge.http in
// Java). Java hands the body over in pieces as it arrives and asks between
// reads whether to stop; the caps and cancellation are decided here, as
// webOS's rede decides them in C. Groups and handles: http_groups.cpp.
#include <algorithm>

#include "platform/android/jni.hpp"
#include "platform/http_transport.hpp"

namespace marigold::http::transport {
namespace {

// What Java's callbacks reach, by address (a jlong).
struct Call {
  const std::function<bool(std::string_view)>* receive = nullptr;
  const std::function<bool()>* stop = nullptr;
  Result* out = nullptr;
  size_t cap = 0;
  size_t received = 0;
  bool overflow = false;
  bool stopped = false;  // the consumer said stop (rede reports curl's write error)
};

Call* callOf(jlong p) { return reinterpret_cast<Call*>(static_cast<intptr_t>(p)); }

}  // namespace

void prepare() {}

Result fetch(const Request& q, Handle*, size_t wireCap, const std::function<bool(std::string_view)>& receive,
             const std::function<bool()>& stop) {
  Result out;
  Call call;
  call.receive = receive ? &receive : nullptr;
  call.stop = &stop;
  call.out = &out;
  call.cap = wireCap ? wireCap : receive ? kStreamedMax : kBufferedDefault;

  JNIEnv* e = android::env();
  if (!e) {
    out.error = "unavailable";
    return out;
  }
  static jmethodID http = android::method("http", "(J[B[B[B[BIZZ)[B");
  std::string headers;
  for (const std::string& h : q.headers) headers += h + "\n";
  android::Local<jbyteArray> method(e, android::bytes(e, q.method));
  android::Local<jbyteArray> url(e, android::bytes(e, q.url));
  android::Local<jbyteArray> head(e, android::bytes(e, headers));
  android::Local<jbyteArray> body(e, q.body.empty() ? nullptr : android::bytes(e, q.body));
  android::Local<jbyteArray> error(
      e, static_cast<jbyteArray>(e->CallStaticObjectMethod(
             android::bridge(), http, static_cast<jlong>(reinterpret_cast<intptr_t>(&call)), method.get(), url.get(),
             head.get(), body.get(), static_cast<jint>(std::min(q.timeoutMs, 300000u)), static_cast<jboolean>(q.follow),
             static_cast<jboolean>(q.personalServer))));
  if (android::caught(e, "http")) out.error = "transport";
  else out.error = android::text(e, error.get());
  if (call.overflow) out.error = "body-cap";
  else if (call.stopped && out.error.empty()) out.error = "transport (stopped)";
  return out;
}

}  // namespace marigold::http::transport

extern "C" {

// The response line and headers, before the body.
JNIEXPORT void JNICALL Java_org_marigold_app_Bridge_nativeHttpHead(JNIEnv* env, jclass, jlong p, jint status,
                                                                   jbyteArray mime, jbyteArray headers, jint retryAfter,
                                                                   jint ms) {
  using namespace marigold::http::transport;
  Result* out = callOf(p)->out;
  out->status = status;
  out->mime = marigold::android::text(env, mime);
  out->headers = marigold::android::text(env, headers);
  out->retryAfterS = retryAfter;
  out->ms = static_cast<unsigned>(ms);
}

// A piece of the body. False stops the transfer.
JNIEXPORT jboolean JNICALL Java_org_marigold_app_Bridge_nativeHttpChunk(JNIEnv* env, jclass, jlong p, jbyteArray buf,
                                                                        jint n) {
  using namespace marigold::http::transport;
  Call* c = callOf(p);
  if ((*c->stop)()) return JNI_FALSE;
  if (c->received + static_cast<size_t>(n) > c->cap) {
    c->overflow = true;
    return JNI_FALSE;
  }
  c->received += static_cast<size_t>(n);
  // As rede: a streamed body goes to its consumer only on a 2xx response.
  int status = c->out->status;
  if (c->receive && (status < 200 || status >= 300)) return JNI_TRUE;
  jbyte* data = env->GetByteArrayElements(buf, nullptr);
  bool more = true;
  std::string_view chunk(reinterpret_cast<const char*>(data), static_cast<size_t>(n));
  if (c->receive) more = (*c->receive)(chunk);
  else c->out->body.append(chunk);
  env->ReleaseByteArrayElements(buf, data, JNI_ABORT);
  if (!more) c->stopped = true;
  return more ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_org_marigold_app_Bridge_nativeHttpStopped(JNIEnv*, jclass, jlong p) {
  return (*marigold::http::transport::callOf(p)->stop)() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_marigold_app_Bridge_nativeHttpMs(JNIEnv*, jclass, jlong p, jint ms) {
  marigold::http::transport::callOf(p)->out->ms = static_cast<unsigned>(ms);
}

}  // extern "C"
