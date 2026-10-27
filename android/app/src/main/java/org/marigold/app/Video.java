package org.marigold.app;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.SurfaceView;
import android.view.View;
import android.widget.FrameLayout;

import androidx.media3.common.AudioAttributes;
import androidx.media3.common.C;
import androidx.media3.common.MediaItem;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.PlaybackException;
import androidx.media3.common.Player;
import androidx.media3.common.Tracks;
import androidx.media3.common.VideoSize;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * The native video player (src/platform/android/video.cpp): one ExoPlayer on
 * the SurfaceView under the UI, on the UI thread. The UI clears a hole where
 * the picture goes (as on webOS); the picture is fitted inside the rectangle
 * it is given, keeping its shape. State is kept as a snapshot native reads.
 */
final class Video {
  private Video() {}

  private static final String USER_AGENT = "Marigold/0.1 (Android)";
  private static final Handler ui = new Handler(Looper.getMainLooper());

  private static ExoPlayer player;
  private static DefaultHttpDataSource.Factory http;
  private static DefaultMediaSourceFactory sources;
  private static FrameLayout root;
  private static SurfaceView view;

  // The rectangle asked for, in 1920x1080 layout units.
  private static int wantX, wantY, wantW = 1920, wantH = 1080;

  // What is playing (the link stays in memory only).
  private static String url = "";
  private static boolean asHls;
  private static boolean triedOther;

  // The snapshot native reads (guarded by lock).
  private static final Object lock = new Object();
  private static boolean active, ready, playing, failed, ended, firstFrame, audioUnsupported, buffering;
  private static long bufferingSince;
  private static double position, bufferEnd;
  private static int width, height;
  private static float pixelRatio = 1f;
  private static String error = "";

  /** UI thread, once the views exist. */
  static void attach(Context context, FrameLayout parent, SurfaceView surface) {
    root = parent;
    view = surface;
    if (player == null) {
      http = new DefaultHttpDataSource.Factory()
          .setUserAgent(USER_AGENT)
          .setAllowCrossProtocolRedirects(true)
          .setConnectTimeoutMs(15000)
          .setReadTimeoutMs(20000);
      sources = new DefaultMediaSourceFactory(context).setDataSourceFactory(http);
      DefaultRenderersFactory renderers = new DefaultRenderersFactory(context).setEnableDecoderFallback(true);
      player = new ExoPlayer.Builder(context, renderers).setMediaSourceFactory(sources).build();
      // Audio focus: another app's sound (or the voice assistant) ducks or
      // pauses this one, and it comes back after.
      AudioAttributes audio = new AudioAttributes.Builder()
          .setUsage(C.USAGE_MEDIA)
          .setContentType(C.AUDIO_CONTENT_TYPE_MOVIE)
          .build();
      player.setAudioAttributes(audio, true);
      player.addListener(listener);
    }
    player.setVideoSurfaceView(view);
    root.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> layout());
  }

  static void play(String link, String headers) {
    ui.post(() -> {
      if (player == null) return;
      url = link;
      triedOther = false;
      asHls = looksHls(link);
      applyHeaders(headers);
      synchronized (lock) {
        active = true;
        ready = playing = failed = ended = firstFrame = audioUnsupported = false;
        buffering = true;
        bufferingSince = SystemClock.elapsedRealtime();
        position = bufferEnd = 0;
        width = height = 0;
        error = "";
      }
      Bridge.keepScreenOn(true);
      start();
      tick();
    });
  }

  static void stop() {
    ui.post(() -> {
      if (player != null) {
        player.stop();
        player.clearMediaItems();
      }
      url = "";
      Bridge.keepScreenOn(false);
      synchronized (lock) {
        active = ready = playing = failed = ended = firstFrame = buffering = false;
        width = height = 0;
      }
    });
  }

  static void release() {
    ui.post(() -> {
      if (player != null) player.release();
      player = null;
    });
  }

  static void window(int x, int y, int w, int h, boolean force) {
    ui.post(() -> {
      if (!force && x == wantX && y == wantY && w == wantW && h == wantH) return;
      wantX = x;
      wantY = y;
      wantW = w;
      wantH = h;
      layout();
    });
  }

  /** Any thread. Fills flags, buffering ms, position, buffer end, width, height; returns the error. */
  static String state(double[] out) {
    synchronized (lock) {
      int flags = (active ? 1 : 0) | (ready ? 2 : 0) | (playing ? 4 : 0) | (failed ? 8 : 0) | (ended ? 16 : 0)
          | (firstFrame ? 32 : 0) | (audioUnsupported ? 64 : 0);
      out[0] = flags;
      out[1] = buffering ? SystemClock.elapsedRealtime() - bufferingSince : 0;
      out[2] = position;
      out[3] = bufferEnd;
      out[4] = width;
      out[5] = height;
      return error;
    }
  }

  // ---- UI thread ----

  private static boolean looksHls(String link) {
    String path = link.toLowerCase(Locale.ROOT);
    int q = path.indexOf('?');
    if (q >= 0) path = path.substring(0, q);
    return path.endsWith(".m3u8") || path.endsWith(".m3u") || path.contains("/hls/");
  }

  private static void applyHeaders(String headers) {
    Map<String, String> props = new HashMap<>();
    String agent = USER_AGENT;
    for (String line : headers.split("\n")) {
      int colon = line.indexOf(':');
      if (colon <= 0) continue;
      String name = line.substring(0, colon).trim();
      String value = line.substring(colon + 1).trim();
      if (name.equalsIgnoreCase("User-Agent")) agent = value;
      else props.put(name, value);
    }
    http.setUserAgent(agent);
    http.setDefaultRequestProperties(props);
  }

  private static void start() {
    MediaItem.Builder item = new MediaItem.Builder().setUri(Uri.parse(url));
    if (asHls) item.setMimeType(MimeTypes.APPLICATION_M3U8);
    player.setMediaItem(item.build());
    player.prepare();
    player.setPlayWhenReady(true);
  }

  // Keeps the position and buffer in the snapshot while something plays.
  private static final Runnable ticker = Video::tick;

  private static void tick() {
    ui.removeCallbacks(ticker);
    if (player == null || url.isEmpty()) return;
    synchronized (lock) {
      position = player.getCurrentPosition() / 1000.0;
      bufferEnd = player.getBufferedPosition() / 1000.0;
      playing = player.isPlaying();
    }
    ui.postDelayed(ticker, 250);
  }

  // The picture fitted inside the rectangle asked for, keeping its shape.
  private static void layout() {
    if (root == null || view == null) return;
    int rw = root.getWidth(), rh = root.getHeight();
    if (rw <= 0 || rh <= 0) return;
    float sx = rw / 1920f, sy = rh / 1080f;
    float x = wantX * sx, y = wantY * sy, w = wantW * sx, h = wantH * sy;
    int vw, vh;
    float ratio;
    synchronized (lock) {
      vw = width;
      vh = height;
      ratio = pixelRatio;
    }
    if (vw > 0 && vh > 0 && w > 0 && h > 0) {
      float aspect = vw * ratio / vh;
      if (w / h > aspect) {
        float fw = h * aspect;
        x += (w - fw) / 2;
        w = fw;
      } else {
        float fh = w / aspect;
        y += (h - fh) / 2;
        h = fh;
      }
    }
    FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(Math.max(1, Math.round(w)), Math.max(1, Math.round(h)));
    lp.gravity = Gravity.TOP | Gravity.START;
    lp.leftMargin = Math.round(x);
    lp.topMargin = Math.round(y);
    view.setLayoutParams(lp);
    view.setVisibility(View.VISIBLE);
  }

  private static final Player.Listener listener = new Player.Listener() {
    @Override
    public void onPlaybackStateChanged(int state) {
      synchronized (lock) {
        if (state == Player.STATE_BUFFERING) {
          if (!buffering) bufferingSince = SystemClock.elapsedRealtime();
          buffering = true;
        } else {
          buffering = false;
        }
        if (state == Player.STATE_READY) ready = true;
        if (state == Player.STATE_ENDED) ended = true;
      }
    }

    @Override
    public void onIsPlayingChanged(boolean isPlaying) {
      synchronized (lock) {
        playing = isPlaying;
      }
    }

    @Override
    public void onRenderedFirstFrame() {
      synchronized (lock) {
        firstFrame = true;
      }
    }

    @Override
    public void onVideoSizeChanged(VideoSize size) {
      synchronized (lock) {
        width = size.width;
        height = size.height;
        pixelRatio = size.pixelWidthHeightRatio > 0 ? size.pixelWidthHeightRatio : 1f;
      }
      layout();
    }

    @Override
    public void onTracksChanged(Tracks tracks) {
      boolean audio = false, playable = false;
      for (Tracks.Group g : tracks.getGroups()) {
        if (g.getType() != C.TRACK_TYPE_AUDIO) continue;
        audio = true;
        if (g.isSupported()) playable = true;
      }
      synchronized (lock) {
        audioUnsupported = audio && !playable;
      }
    }

    @Override
    public void onPlayerError(PlaybackException e) {
      if (url.isEmpty()) return;
      int code = e.errorCode;
      // A live stream fell behind its window: back to the live edge.
      if (code == PlaybackException.ERROR_CODE_BEHIND_LIVE_WINDOW) {
        player.seekToDefaultPosition();
        player.prepare();
        return;
      }
      // Not the kind of stream its link suggested (an HLS playlist without
      // .m3u8, or a TS link ending .m3u8): try the other kind once.
      boolean wrongKind = code == PlaybackException.ERROR_CODE_PARSING_CONTAINER_UNSUPPORTED
          || code == PlaybackException.ERROR_CODE_PARSING_CONTAINER_MALFORMED
          || code == PlaybackException.ERROR_CODE_PARSING_MANIFEST_MALFORMED
          || code == PlaybackException.ERROR_CODE_PARSING_MANIFEST_UNSUPPORTED;
      if (wrongKind && !triedOther) {
        triedOther = true;
        asHls = !asHls;
        start();
        return;
      }
      synchronized (lock) {
        failed = true;
        buffering = false;
        error = e.getErrorCodeName();
      }
    }
  };
}
