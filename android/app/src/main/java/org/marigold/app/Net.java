package org.marigold.app;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ConnectException;
import java.net.HttpURLConnection;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.net.UnknownHostException;
import java.security.SecureRandom;
import java.security.cert.X509Certificate;
import java.util.List;
import java.util.Locale;
import java.util.Map;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLException;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

/**
 * One HTTP transfer for the native worker pool (src/platform/android/
 * http.cpp), on the calling worker thread. The body goes to native in pieces
 * as it arrives; native decides the caps and when to stop. Redirects are
 * followed here (HttpURLConnection won't cross http and https, which IPTV
 * links often do). Returns "" or a short error code; never logs a link.
 */
final class Net {
  private Net() {}

  // The device's stock agent ("Dalvik/2.1.0 (Linux; U; Android 9; AFTMM
  // Build/PS7633)"), what any app on it sends, without our name.
  private static final String USER_AGENT = stockAgent();

  private static String stockAgent() {
    String agent = System.getProperty("http.agent");
    return agent == null || agent.isEmpty() ? "Dalvik/2.1.0 (Linux; U; Android)" : agent;
  }
  private static final int MAX_REDIRECTS = 8;
  private static volatile SSLSocketFactory trustAll;

  static String fetch(long call, String method, String url, String headers, byte[] body, int timeoutMs,
                      boolean follow, boolean insecure) {
    long started = System.nanoTime();
    String verb = method.isEmpty() ? (body != null ? "POST" : "GET") : method.toUpperCase(Locale.ROOT);
    HttpURLConnection c = null;
    try {
      URL current = new URL(url);
      int hops = 0;
      while (true) {
        if (Bridge.nativeHttpStopped(call)) return "cancelled";
        c = (HttpURLConnection) current.openConnection();
        c.setInstanceFollowRedirects(false);
        c.setConnectTimeout(timeoutMs);
        c.setReadTimeout(timeoutMs);
        c.setUseCaches(false);
        if (insecure && c instanceof HttpsURLConnection) {
          // A personal server (Emby on the LAN) with its own certificate.
          HttpsURLConnection https = (HttpsURLConnection) c;
          https.setSSLSocketFactory(trustAllFactory());
          https.setHostnameVerifier((host, session) -> true);
        }
        boolean agent = false;
        for (String line : headers.split("\n")) {
          int colon = line.indexOf(':');
          if (colon <= 0) continue;
          String name = line.substring(0, colon).trim();
          String value = line.substring(colon + 1).trim();
          if (name.equalsIgnoreCase("User-Agent")) agent = true;
          c.setRequestProperty(name, value);
        }
        if (!agent) c.setRequestProperty("User-Agent", USER_AGENT);
        c.setRequestMethod(verb);
        if (body != null && !verb.equals("GET") && !verb.equals("HEAD")) {
          c.setDoOutput(true);
          c.setFixedLengthStreamingMode(body.length);
          try (OutputStream out = c.getOutputStream()) {
            out.write(body);
          }
        }
        int status = c.getResponseCode();
        boolean redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        if (redirect && follow) {
          String location = c.getHeaderField("Location");
          if (location == null || ++hops > MAX_REDIRECTS) return "redirect";
          current = new URL(current, location);
          if (status == 303 || ((status == 301 || status == 302) && verb.equals("POST"))) {
            verb = "GET";
            body = null;
          }
          c.disconnect();
          continue;
        }
        Bridge.nativeHttpHead(call, status, Bridge.utf8(mimeOf(c.getContentType())), Bridge.utf8(headerBlock(c, status)),
            retryAfter(c), elapsed(started));
        InputStream in;
        try {
          in = c.getInputStream();
        } catch (IOException e) {
          in = c.getErrorStream();  // the body of a 4xx or 5xx
        }
        if (in != null) {
          try (InputStream body2 = in) {
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = body2.read(buf)) > 0) {
              if (!Bridge.nativeHttpChunk(call, buf, n)) break;  // a cap, a cancel or the consumer is done
            }
          }
        }
        Bridge.nativeHttpMs(call, elapsed(started));
        return "";
      }
    } catch (SocketTimeoutException e) {
      return "timeout";
    } catch (UnknownHostException e) {
      return "transport (dns)";
    } catch (ConnectException e) {
      return "transport (connect)";
    } catch (SSLException e) {
      return "transport (tls)";
    } catch (IOException | RuntimeException e) {
      return "transport";
    } finally {
      if (c != null) c.disconnect();
    }
  }

  private static int elapsed(long started) {
    return (int) ((System.nanoTime() - started) / 1_000_000L);
  }

  // "text/html; charset=utf-8" -> "text/html", as Legacy's rede does.
  private static String mimeOf(String contentType) {
    if (contentType == null) return "";
    int semi = contentType.indexOf(';');
    String type = semi >= 0 ? contentType.substring(0, semi) : contentType;
    return type.replace(" ", "").replace("\t", "").toLowerCase(Locale.ROOT);
  }

  private static String headerBlock(HttpURLConnection c, int status) {
    StringBuilder b = new StringBuilder("HTTP/1.1 ").append(status).append("\r\n");
    for (Map.Entry<String, List<String>> h : c.getHeaderFields().entrySet()) {
      if (h.getKey() == null) continue;
      for (String v : h.getValue()) b.append(h.getKey()).append(": ").append(v).append("\r\n");
    }
    return b.toString();
  }

  private static int retryAfter(HttpURLConnection c) {
    String v = c.getHeaderField("Retry-After");
    if (v == null) return 0;
    try {
      return Math.max(0, Integer.parseInt(v.trim()));
    } catch (NumberFormatException e) {
      return 0;
    }
  }

  private static SSLSocketFactory trustAllFactory() throws IOException {
    SSLSocketFactory f = trustAll;
    if (f != null) return f;
    try {
      TrustManager[] all = {new X509TrustManager() {
        @Override public void checkClientTrusted(X509Certificate[] chain, String authType) {}
        @Override public void checkServerTrusted(X509Certificate[] chain, String authType) {}
        @Override public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
      }};
      SSLContext ctx = SSLContext.getInstance("TLS");
      ctx.init(null, all, new SecureRandom());
      trustAll = f = ctx.getSocketFactory();
      return f;
    } catch (Exception e) {
      throw new IOException("tls");
    }
  }
}
