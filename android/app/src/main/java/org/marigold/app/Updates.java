package org.marigold.app;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;

import androidx.core.content.FileProvider;

import java.io.File;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Build 148: installing an update of the app (src/core/engine/updates.hpp
 * finds it, downloads it to the cache and checks its SHA-256 first). The APK
 * goes to the system's installer through a FileProvider; where the system
 * asks (Android 8+), the app's "Install unknown apps" page opens first and
 * the installer follows when the viewer comes back with it allowed. An APK
 * that isn't this app, isn't newer or is signed with another key is refused
 * here, as the installer would refuse it. Never logs a path or a link.
 */
final class Updates {
  private Updates() {}

  private static final String TAG = "marigold";
  private static final String FILE = "marigold-android.apk";
  private static final String MIME = "application/vnd.android.package-archive";

  // UI thread: the APK waiting for the "Install unknown apps" page to be
  // allowed, and whether that page is open.
  private static File pending;
  private static boolean askedToAllow;

  /** Where the download goes: the cache's update folder (update_paths.xml). */
  static String file() {
    Context c = Bridge.context();
    if (c == null) return "";
    File dir = new File(c.getCacheDir(), "update");
    if (!dir.mkdirs() && !dir.isDirectory()) return "";
    return new File(dir, FILE).getAbsolutePath();
  }

  /**
   * A worker thread: checks the APK and starts the installer (or the page
   * that allows it) on the UI thread. Returns "" once started, or why not.
   */
  static String install(String path) {
    Context c = Bridge.context();
    if (c == null) return "no app";
    File apk = new File(path);
    if (!apk.isFile()) return "the download is missing";
    String refused = refusal(c, apk.getAbsolutePath());
    if (refused != null) return refused;
    String[] started = {"the installer didn't start"};
    CountDownLatch done = new CountDownLatch(1);
    Bridge.onUi(() -> {
      try {
        Activity a = Bridge.activity();
        started[0] = a == null ? "the app isn't on screen" : start(a, apk);
      } finally {
        done.countDown();
      }
    });
    try {
      if (!done.await(10, TimeUnit.SECONDS)) return "the installer didn't start in time";
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
      return "interrupted";
    }
    return started[0];
  }

  /** The activity is back on screen: the installer, if the page allowed it. */
  static void resumed(Activity a) {
    if (!askedToAllow) return;
    askedToAllow = false;
    File apk = pending;
    pending = null;
    if (apk == null || !mayInstall(a)) return;  // not allowed: Settings' row stays for later
    launch(a, apk);
  }

  // ---- UI thread ----

  private static String start(Activity a, File apk) {
    if (!mayInstall(a)) {
      Intent allow = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + a.getPackageName()));
      try {
        pending = apk;
        askedToAllow = true;
        a.startActivity(allow);
        return "";
      } catch (ActivityNotFoundException e) {
        // No such page on this device: the installer says what it needs.
        pending = null;
        askedToAllow = false;
      }
    }
    return launch(a, apk);
  }

  private static boolean mayInstall(Context c) {
    return Build.VERSION.SDK_INT < Build.VERSION_CODES.O || c.getPackageManager().canRequestPackageInstalls();
  }

  @SuppressWarnings("deprecation")
  private static String launch(Activity a, File apk) {
    Uri uri;
    try {
      uri = FileProvider.getUriForFile(a, a.getPackageName() + ".updates", apk);
    } catch (IllegalArgumentException e) {
      return "no provider for the download";
    }
    for (String action : new String[] {Intent.ACTION_INSTALL_PACKAGE, Intent.ACTION_VIEW}) {
      Intent i = new Intent(action);
      i.setDataAndType(uri, MIME);
      i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
      try {
        a.startActivity(i);
        return "";
      } catch (ActivityNotFoundException e) {
        // the next way
      }
    }
    Log.w(TAG, "update: no installer on this device");
    return "no installer on this device";
  }

  // ---- The APK itself (a worker) ----

  /** Why the system's installer would refuse it, or null. */
  @SuppressWarnings("deprecation")
  private static String refusal(Context c, String path) {
    PackageManager pm = c.getPackageManager();
    PackageInfo archive = pm.getPackageArchiveInfo(path, PackageManager.GET_SIGNATURES);
    if (archive == null) return "not an app";
    if (!c.getPackageName().equals(archive.packageName)) return "another app";
    PackageInfo self;
    try {
      self = pm.getPackageInfo(c.getPackageName(), PackageManager.GET_SIGNATURES);
    } catch (PackageManager.NameNotFoundException e) {
      return "this app isn't installed";
    }
    if (versionCode(archive) <= versionCode(self)) return "not a newer build";
    // Only installs over an APK signed with the same key (CI's release key,
    // or its cached debug key). Unknown on either side: the installer decides.
    Signature[] theirs = archive.signatures, ours = self.signatures;
    if (theirs != null && ours != null && theirs.length > 0 && ours.length > 0 && !sameSigners(theirs, ours))
      return "signed with another key";
    return null;
  }

  private static boolean sameSigners(Signature[] a, Signature[] b) {
    String[] x = new String[a.length], y = new String[b.length];
    for (int i = 0; i < a.length; i++) x[i] = a[i].toCharsString();
    for (int i = 0; i < b.length; i++) y[i] = b[i].toCharsString();
    Arrays.sort(x);
    Arrays.sort(y);
    return Arrays.equals(x, y);
  }

  @SuppressWarnings("deprecation")
  private static long versionCode(PackageInfo p) {
    return Build.VERSION.SDK_INT >= Build.VERSION_CODES.P ? p.getLongVersionCode() : p.versionCode;
  }
}
