package org.marigold.app;

import android.graphics.Bitmap;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;

import java.nio.charset.StandardCharsets;

/**
 * Everything the native app (src/platform/android) calls in Java, and the
 * native calls Java makes into it. Strings cross as UTF-8 byte arrays (JNI's
 * own string conversion breaks on emoji). Calls from native threads that touch
 * views or the player hop to the UI thread here.
 */
final class Bridge {
  private Bridge() {}

  static {
    System.loadLibrary("marigold");
  }

  private static final Handler ui = new Handler(Looper.getMainLooper());
  private static volatile MarigoldActivity activity;

  static void attach(MarigoldActivity a) {
    activity = a;
  }

  static void detach(MarigoldActivity a) {
    if (activity == a) activity = null;
  }

  static void onUi(Runnable r) {
    if (Looper.myLooper() == Looper.getMainLooper()) r.run();
    else ui.post(r);
  }

  // ---- Into native ----

  static native void nativeStart(byte[] assetDir, byte[] filesDir);
  static native void nativeSurface(Surface surface);
  static native boolean nativeKey(int keyCode, boolean down, int repeat);
  static native void nativeLifecycle(int what);  // 0 quit, 1 hidden, 2 shown, 3 background
  static native void nativeText(byte[] value, boolean done, boolean confirmed);

  static native void nativeHttpHead(long call, int status, byte[] mime, byte[] headers, int retryAfter, int ms);
  static native boolean nativeHttpChunk(long call, byte[] buf, int n);
  static native boolean nativeHttpStopped(long call);
  static native void nativeHttpMs(long call, int ms);

  // ---- Strings ----

  static String str(byte[] utf8) {
    return utf8 == null ? "" : new String(utf8, StandardCharsets.UTF_8);
  }

  static byte[] utf8(String s) {
    return (s == null ? "" : s).getBytes(StandardCharsets.UTF_8);
  }

  static void recycle(Bitmap b) {
    if (b != null) b.recycle();
  }

  // ---- Text and images (the app thread and workers) ----

  static boolean textInit(byte[] fontDir) {
    return Fonts.init(str(fontDir));
  }

  static float textMeasure(byte[] s, int weight, int px) {
    return Fonts.measure(str(s), weight, px);
  }

  static float textHeight(int weight, int px) {
    return Fonts.height(weight, px);
  }

  static Bitmap textRender(byte[] s, int weight, int px) {
    return Fonts.render(str(s), weight, px);
  }

  static Bitmap imageDecode(byte[] data, int maxPixels) {
    return Images.decode(data, maxPixels);
  }

  // ---- HTTP (workers) ----

  static byte[] http(long call, byte[] method, byte[] url, byte[] headers, byte[] body, int timeoutMs,
                     boolean follow, boolean insecure) {
    return utf8(Net.fetch(call, str(method), str(url), str(headers), body, timeoutMs, follow, insecure));
  }

  // ---- Video (the app thread) ----

  static void videoPlay(byte[] url, byte[] headers) {
    Video.play(str(url), str(headers));
  }

  static void videoStop() {
    Video.stop();
  }

  static void videoRelease() {
    Video.release();
  }

  static void videoWindow(int x, int y, int w, int h, boolean force) {
    Video.window(x, y, w, h, force);
  }

  static byte[] videoState(double[] out) {
    return utf8(Video.state(out));
  }

  // ---- The keyboard and the app (the app thread) ----

  static void keyboardOpen(byte[] value) {
    String v = str(value);
    onUi(() -> {
      MarigoldActivity a = activity;
      if (a != null) Keyboard.open(a, v);
      else nativeText(utf8(v), true, false);
    });
  }

  static void keyboardClose() {
    onUi(Keyboard::close);
  }

  static void leave() {
    onUi(() -> {
      MarigoldActivity a = activity;
      if (a != null) a.moveTaskToBack(true);
    });
  }

  static void finish() {
    onUi(() -> {
      MarigoldActivity a = activity;
      if (a != null) a.finishAndRemoveTask();
      // The native app has ended; a new launch starts a fresh process.
      ui.postDelayed(() -> System.exit(0), 300);
    });
  }
}
