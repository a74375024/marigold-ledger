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
import androidx.media3.common.TrackGroup;
import androidx.media3.common.TrackSelectionOverride;
import androidx.media3.common.TrackSelectionParameters;
import androidx.media3.common.Tracks;
import androidx.media3.common.text.Cue;
import androidx.media3.common.text.CueGroup;
import androidx.media3.common.VideoSize;
import androidx.media3.datasource.DefaultHttpDataSource;
import androidx.media3.exoplayer.DefaultLoadControl;
import androidx.media3.exoplayer.DefaultRenderersFactory;
import androidx.media3.exoplayer.ExoPlayer;
import androidx.media3.exoplayer.source.DefaultMediaSourceFactory;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
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

  // The device's stock agent ("Dalvik/2.1.0 (Linux; U; Android 9; AFTMM
  // Build/PS7633)"), what any app on it sends, without our name.
  private static final String USER_AGENT = stockAgent();

  private static String stockAgent() {
    String agent = System.getProperty("http.agent");
    return agent == null || agent.isEmpty() ? "Dalvik/2.1.0 (Linux; U; Android)" : agent;
  }
  private static final Handler ui = new Handler(Looper.getMainLooper());

  private static ExoPlayer player;
  private static DefaultHttpDataSource.Factory http;
  private static DefaultMediaSourceFactory sources;
  private static FrameLayout root;
  private static SurfaceView view;

  // The rectangle asked for, in 1920x1080 layout units.
  private static int wantX, wantY, wantW = 1920, wantH = 1080;
  private static boolean muted;

  // What is playing (the link stays in memory only).
  private static String url = "";
  private static boolean asHls;
  private static boolean triedOther;

  // The snapshot native reads (guarded by lock).
  private static final Object lock = new Object();
  private static boolean active, ready, playing, failed, ended, firstFrame, audioUnsupported, buffering;
  private static long bufferingSince;
  // The picture check: the video tracks listed (-1 not yet), one that can't
  // be decoded, and frames that stopped being drawn while the clock runs.
  private static int videoTracks = -1;
  private static boolean videoUnsupported, framesStalled;
  // UI thread: the video decoder's rendered-frame count when last read, and
  // since when it hasn't moved while the position did.
  private static int renderedLast = -1;
  private static long stillSince;
  private static double stillFrom;
  private static String facts = "";
  private static double position, bufferEnd;
  private static int width, height;
  private static float pixelRatio = 1f;
  private static String error = "";
  // Subtitles: the text tracks ("id\tlanguage\tchannel" lines), the lines
  // decoded for the one shown, and a counter that moves when they change.
  private static String textTracks = "", textLines = "";
  private static long textVersion;
  // What the app asked for (UI thread): a track shown or none. ExoPlayer draws
  // nothing itself; the app draws the lines.
  private static boolean textOn;
  private static String textWanted = "";
  private static final List<TrackGroup> textGroups = new ArrayList<>();

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
      // Live TV starts after 1 s of video, not ExoPlayer's 2.5 s (2 s after a
      // stall), and keeps at most ~20 MB ahead (its default could hold about
      // 50 MB of live TS, a lot on a 1-1.5 GB Fire TV).
      DefaultLoadControl load = new DefaultLoadControl.Builder()
          .setBufferDurationsMs(15000, 30000, 1000, 2000)
          .setTargetBufferBytes(20 * 1024 * 1024)
          .setPrioritizeTimeOverSizeThresholds(false)
          .build();
      player = new ExoPlayer.Builder(context, renderers)
          .setMediaSourceFactory(sources)
          .setLoadControl(load)
          .build();
      // Audio focus: another app's sound (or the voice assistant) ducks or
      // pauses this one, and it comes back after.
      AudioAttributes audio = new AudioAttributes.Builder()
          .setUsage(C.USAGE_MEDIA)
          .setContentType(C.AUDIO_CONTENT_TYPE_MOVIE)
          .build();
      player.setAudioAttributes(audio, true);
      player.addListener(listener);
      player.setVolume(muted ? 0f : 1f);
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
        videoTracks = -1;
        videoUnsupported = framesStalled = false;
        facts = "";
        buffering = true;
        bufferingSince = SystemClock.elapsedRealtime();
        position = bufferEnd = 0;
        width = height = 0;
        error = "";
        textTracks = "";
        textLines = "";
        textVersion++;
      }
      Bridge.keepScreenOn(true);
      renderedLast = -1;
      stillSince = 0;
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
        videoUnsupported = framesStalled = false;
        videoTracks = -1;
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

  /** Search's corner preview plays without sound; off brings it back on the same stream. */
  static void muted(boolean on) {
    ui.post(() -> {
      muted = on;
      if (player != null) player.setVolume(on ? 0f : 1f);
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

  /**
   * Any thread. Fills flags, buffering ms, position, buffer end, width, height and the video tracks
   * listed (-1 not yet); returns the error.
   */
  static String state(double[] out) {
    synchronized (lock) {
      int flags = (active ? 1 : 0) | (ready ? 2 : 0) | (playing ? 4 : 0) | (failed ? 8 : 0) | (ended ? 16 : 0)
          | (firstFrame ? 32 : 0) | (audioUnsupported ? 64 : 0) | (framesStalled ? 128 : 0)
          | (videoUnsupported ? 256 : 0);
      out[0] = flags;
      out[1] = buffering ? SystemClock.elapsedRealtime() - bufferingSince : 0;
      out[2] = position;
      out[3] = bufferEnd;
      out[4] = width;
      out[5] = height;
      if (out.length > 6) out[6] = videoTracks;
      return error;
    }
  }

  /**
   * Any thread. What plays, for the playback log: "width\theight\tframe rate\tvideo type\tvideo
   * codecs\taudio type"; "" before the first frame.
   */
  static String facts() {
    synchronized (lock) {
      return facts;
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

  static String textTracks() {
    synchronized (lock) {
      return textTracks;
    }
  }

  static String textLines(long[] version) {
    synchronized (lock) {
      if (version != null && version.length > 0) version[0] = textVersion;
      return textLines;
    }
  }

  static void selectText(String id, boolean on) {
    ui.post(() -> {
      textOn = on;
      textWanted = id == null ? "" : id;
      applyText();
    });
  }

  // UI thread: the track asked for (or the first), or text off.
  private static void applyText() {
    if (player == null) return;
    TrackSelectionParameters.Builder p = player.getTrackSelectionParameters().buildUpon()
        .clearOverridesOfType(C.TRACK_TYPE_TEXT);
    TrackGroup group = null;
    int index = 0;
    if (textOn && !textGroups.isEmpty()) {
      group = textGroups.get(0);
      if (textWanted.startsWith("t")) {
        try {
          int dot = textWanted.indexOf('.');
          int k = Integer.parseInt(textWanted.substring(1, dot));
          int i = Integer.parseInt(textWanted.substring(dot + 1));
          if (k < textGroups.size() && i < textGroups.get(k).length) {
            group = textGroups.get(k);
            index = i;
          }
        } catch (RuntimeException ignored) {
          // not one of ours: the first track
        }
      }
    }
    if (group != null) {
      p.setTrackTypeDisabled(C.TRACK_TYPE_TEXT, false).setOverrideForType(new TrackSelectionOverride(group, index));
    } else {
      p.setTrackTypeDisabled(C.TRACK_TYPE_TEXT, true);
      synchronized (lock) {
        if (!textLines.isEmpty()) {
          textLines = "";
          textVersion++;
        }
      }
    }
    player.setTrackSelectionParameters(p.build());
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
    boolean stalled = framesStalled();
    String what = factsNow();
    synchronized (lock) {
      position = player.getCurrentPosition() / 1000.0;
      bufferEnd = player.getBufferedPosition() / 1000.0;
      playing = player.isPlaying();
      framesStalled = stalled;
      if (facts.isEmpty() && firstFrame) facts = what;
    }
    ui.postDelayed(ticker, 250);
  }

  // The picture check: the video decoder's rendered-frame count stopped for
  // 2 s while the position moved on (a frozen or undecodable picture under
  // running sound). Only once a first frame was drawn.
  private static boolean framesStalled() {
    androidx.media3.exoplayer.DecoderCounters c = player.getVideoDecoderCounters();
    boolean drawn;
    synchronized (lock) {
      drawn = firstFrame;
    }
    if (c == null || !drawn || !player.isPlaying()) {
      stillSince = 0;
      renderedLast = -1;
      return false;
    }
    c.ensureUpdated();
    int rendered = c.renderedOutputBufferCount;
    long now = SystemClock.elapsedRealtime();
    double at = player.getCurrentPosition() / 1000.0;
    if (rendered != renderedLast) {
      renderedLast = rendered;
      stillSince = 0;
      return false;
    }
    if (stillSince == 0) {
      stillSince = now;
      stillFrom = at;
      return false;
    }
    return now - stillSince >= 2000 && at - stillFrom >= 1.5;
  }

  // What plays, once the formats are known (see facts()).
  private static String factsNow() {
    androidx.media3.common.Format v = player.getVideoFormat();
    if (v == null) return "";
    androidx.media3.common.Format a = player.getAudioFormat();
    return v.width + "\t" + v.height + "\t" + (v.frameRate > 0 ? v.frameRate : 0) + "\t"
        + (v.sampleMimeType == null ? "" : v.sampleMimeType) + "\t" + (v.codecs == null ? "" : v.codecs) + "\t"
        + (a == null || a.sampleMimeType == null ? "" : a.sampleMimeType);
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
      int video = 0;
      boolean videoPlayable = false;
      StringBuilder text = new StringBuilder();
      textGroups.clear();
      for (Tracks.Group g : tracks.getGroups()) {
        if (g.getType() == C.TRACK_TYPE_TEXT && g.isSupported()) {
          TrackGroup group = g.getMediaTrackGroup();
          int k = textGroups.size();
          textGroups.add(group);
          for (int i = 0; i < group.length; i++) {
            if (!g.isTrackSupported(i)) continue;
            String language = group.getFormat(i).language;
            int channel = group.getFormat(i).accessibilityChannel;
            text.append('t').append(k).append('.').append(i).append('\t')
                .append(language == null ? "" : language).append('\t')
                .append(channel == androidx.media3.common.Format.NO_VALUE ? 0 : channel).append('\n');
          }
        }
        // The picture check: a stream with no video track, or only ones this
        // device can't decode.
        if (g.getType() == C.TRACK_TYPE_VIDEO) {
          video++;
          if (g.isSupported()) videoPlayable = true;
        }
        if (g.getType() != C.TRACK_TYPE_AUDIO) continue;
        audio = true;
        if (g.isSupported()) playable = true;
      }
      synchronized (lock) {
        audioUnsupported = audio && !playable;
        // Tracks come empty before the stream is read: not known yet.
        if (!tracks.getGroups().isEmpty()) {
          videoTracks = video;
          videoUnsupported = video > 0 && !videoPlayable;
        }
        textTracks = text.toString();
      }
      applyText();
    }

    @Override
    public void onCues(CueGroup cues) {
      StringBuilder b = new StringBuilder();
      for (Cue c : cues.cues) {
        if (c.text == null) continue;
        if (b.length() > 0) b.append('\n');
        b.append(c.text.toString());
      }
      synchronized (lock) {
        String now = b.toString();
        if (!now.equals(textLines)) {
          textLines = now;
          textVersion++;
        }
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
