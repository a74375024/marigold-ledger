package org.marigold.app;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;

/**
 * Logo and artwork decoding for the native image code (src/platform/android/
 * image.cpp): PNG, JPEG, GIF (its first frame) and WebP, as unpremultiplied
 * RGBA. Anything over maxPixels is refused before decoding.
 */
final class Images {
  private Images() {}

  static Bitmap decode(byte[] data, int maxPixels) {
    if (data == null || data.length == 0) return null;
    try {
      BitmapFactory.Options bounds = new BitmapFactory.Options();
      bounds.inJustDecodeBounds = true;
      BitmapFactory.decodeByteArray(data, 0, data.length, bounds);
      if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null;
      if ((long) bounds.outWidth * bounds.outHeight > maxPixels) return null;
      BitmapFactory.Options opts = new BitmapFactory.Options();
      opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
      opts.inPremultiplied = false;
      opts.inScaled = false;
      Bitmap b = BitmapFactory.decodeByteArray(data, 0, data.length, opts);
      if (b == null) return null;
      if (b.getConfig() != Bitmap.Config.ARGB_8888) {
        Bitmap c = b.copy(Bitmap.Config.ARGB_8888, true);
        b.recycle();
        if (c == null) return null;
        c.setPremultiplied(false);
        b = c;
      }
      return b;
    } catch (OutOfMemoryError | RuntimeException e) {
      return null;
    }
  }
}
