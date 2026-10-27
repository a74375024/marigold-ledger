package org.marigold.app;

import android.app.Activity;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The one activity. Two surfaces: the video's (ExoPlayer draws on it) and,
 * over it, the UI's: translucent, fixed at 1920x1080 (the display scales it),
 * drawn by the native app with GLES2. Where the UI clears its alpha the
 * picture shows through, as with the TV's video plane on webOS. Keys and
 * lifecycle go to native; everything else about the app is in native code.
 */
public final class MarigoldActivity extends Activity implements SurfaceHolder.Callback {

  @Override
  protected void onCreate(Bundle state) {
    super.onCreate(state);
    hideSystemBars();

    FrameLayout root = new FrameLayout(this);
    root.setBackgroundColor(Color.BLACK);
    SurfaceView video = new SurfaceView(this);
    root.addView(video, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
        FrameLayout.LayoutParams.MATCH_PARENT));
    SurfaceView ui = new SurfaceView(this);
    ui.setZOrderMediaOverlay(true);
    ui.getHolder().setFormat(PixelFormat.TRANSLUCENT);
    ui.getHolder().setFixedSize(1920, 1080);
    ui.getHolder().addCallback(this);
    root.addView(ui, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
        FrameLayout.LayoutParams.MATCH_PARENT));
    setContentView(root);

    Bridge.attach(this);
    Video.attach(this, root, video);
    String assets = unpackAssets();
    Bridge.nativeStart(Bridge.utf8(assets), Bridge.utf8(getFilesDir().getAbsolutePath()));
  }

  @Override
  protected void onStart() {
    super.onStart();
    Bridge.nativeLifecycle(2);
  }

  @Override
  protected void onStop() {
    super.onStop();
    Bridge.nativeLifecycle(3);  // in the background: the picture stops
  }

  @Override
  protected void onDestroy() {
    Keyboard.close();
    Bridge.detach(this);
    if (isFinishing()) Bridge.nativeLifecycle(0);
    super.onDestroy();
  }

  @Override
  public void onWindowFocusChanged(boolean hasFocus) {
    super.onWindowFocusChanged(hasFocus);
    if (hasFocus) hideSystemBars();
  }

  @Override
  public boolean dispatchKeyEvent(KeyEvent e) {
    int action = e.getAction();
    if (action == KeyEvent.ACTION_DOWN || action == KeyEvent.ACTION_UP) {
      if (Bridge.nativeKey(e.getKeyCode(), action == KeyEvent.ACTION_DOWN, e.getRepeatCount())) return true;
    }
    return super.dispatchKeyEvent(e);  // volume, Home and the like
  }

  // ---- The UI surface ----

  @Override
  public void surfaceCreated(SurfaceHolder holder) {}

  @Override
  public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    Bridge.nativeSurface(holder.getSurface());
  }

  @Override
  public void surfaceDestroyed(SurfaceHolder holder) {
    Bridge.nativeSurface(null);  // returns once native stopped drawing on it
  }

  // ---- Helpers ----

  // While a picture plays the screen saver stays away (the TV holds it the
  // same way); on the guide it may start as usual.
  void keepScreenOn(boolean on) {
    if (on) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
  }

  @SuppressWarnings("deprecation")
  private void hideSystemBars() {
    getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
  }

  // The fonts and sport icons as plain files (native reads them by path),
  // unpacked once per app version.
  private String unpackAssets() {
    File dir = new File(getFilesDir(), "assets-" + versionCode());
    File done = new File(dir, ".done");
    if (done.exists()) return dir.getAbsolutePath();
    File[] old = getFilesDir().listFiles((d, name) -> name.startsWith("assets-"));
    if (old != null) for (File f : old) deleteTree(f);
    AssetManager am = getAssets();
    try {
      for (String folder : new String[] {"fonts", "icons"}) {
        File out = new File(dir, folder);
        if (!out.mkdirs() && !out.isDirectory()) throw new IOException("mkdir");
        String[] names = am.list(folder);
        if (names == null) continue;
        for (String name : names) {
          try (InputStream in = am.open(folder + "/" + name);
               OutputStream os = new FileOutputStream(new File(out, name))) {
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
          }
        }
      }
      if (!done.createNewFile() && !done.exists()) throw new IOException("marker");
    } catch (IOException e) {
      // Native falls back to the system's fonts and draws glyphs for icons.
    }
    return dir.getAbsolutePath();
  }

  @SuppressWarnings("deprecation")
  private long versionCode() {
    try {
      PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
      return Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
    } catch (PackageManager.NameNotFoundException e) {
      return 0;
    }
  }

  private static void deleteTree(File f) {
    File[] children = f.listFiles();
    if (children != null) for (File c : children) deleteTree(c);
    //noinspection ResultOfMethodCallIgnored
    f.delete();
  }
}
