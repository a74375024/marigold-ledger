package org.marigold.app;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;

import java.io.File;

/**
 * The native text renderer's font engine (src/platform/android/text.cpp):
 * the same Inter Display files as the TV, drawn by the system. A line is an
 * alpha-only bitmap as tall as the font's ascent plus descent, with the
 * baseline at the ascent, like SDL_ttf's on webOS. Glyphs Inter lacks
 * (emoji, other scripts) fall back to the system's fonts.
 */
final class Fonts {
  private Fonts() {}

  private static final String[] FILES = {
      "InterDisplay-Regular.ttf", "InterDisplay-Medium.ttf", "InterDisplay-Bold.ttf"};
  private static final Paint[] paints = new Paint[3];

  static synchronized boolean init(String dir) {
    for (int i = 0; i < 3; i++) {
      Typeface face = null;
      File f = new File(dir, FILES[i]);
      if (f.canRead()) {
        try {
          face = Typeface.createFromFile(f);
        } catch (RuntimeException e) {
          face = null;
        }
      }
      if (face == null) face = Typeface.create(Typeface.SANS_SERIF, i == 2 ? Typeface.BOLD : Typeface.NORMAL);
      Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
      p.setTypeface(face);
      p.setColor(0xFFFFFFFF);
      p.setHinting(Paint.HINTING_ON);
      paints[i] = p;
    }
    return true;
  }

  private static Paint paint(int weight, int px) {
    Paint p = paints[Math.max(0, Math.min(2, weight))];
    p.setTextSize(px);
    return p;
  }

  static synchronized float measure(String s, int weight, int px) {
    if (paints[0] == null || s.isEmpty()) return 0;
    return paint(weight, px).measureText(s);
  }

  static synchronized float height(int weight, int px) {
    if (paints[0] == null) return 0;
    Paint.FontMetrics m = paint(weight, px).getFontMetrics();
    return (float) Math.ceil(m.descent - m.ascent);
  }

  static synchronized Bitmap render(String s, int weight, int px) {
    if (paints[0] == null || s.isEmpty()) return null;
    Paint p = paint(weight, px);
    Paint.FontMetrics m = p.getFontMetrics();
    int w = (int) Math.ceil(p.measureText(s));
    int h = (int) Math.ceil(m.descent - m.ascent);
    if (w <= 0 || h <= 0) return null;
    Bitmap b = Bitmap.createBitmap(w, h, Bitmap.Config.ALPHA_8);
    new Canvas(b).drawText(s, 0, -m.ascent, p);
    return b;
  }
}
