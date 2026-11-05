#!/bin/bash
# Starts the Mac app and reads its log: it must open its window, find the
# GPU, show a frame, open and close its text field twice with the keys coming
# back to the app each time (its keyboard self-test), play a test file with
# the VLC it carries (its video self-test), still be running after 40
# seconds and quit cleanly when asked. No screenshots; the log never holds a
# link.
#   bash tools/mac-smoke.sh path/to/Marigold.app
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
app="$1"
bin="$app/Contents/MacOS/Marigold"
log="$(mktemp)"
# Its own data folder's state is whatever the last run left: the first start
# here is a first run (setup), which is what a new Mac sees.
"$bin" -MarigoldKeyboardSelfTest YES -MarigoldVideoSelfTest "$here/mac/smoke.ts" -MarigoldLogToStderr YES \
  >"$log" 2>&1 &
pid=$!
sleep 40
fail=0
if ! kill -0 "$pid" 2>/dev/null; then
  echo "smoke: the app is not running"
  fail=1
fi
# A clean quit (the app takes a plain kill as ⌘Q): the app thread stops and saves.
kill -TERM "$pid" 2>/dev/null || true
for _ in $(seq 1 20); do
  kill -0 "$pid" 2>/dev/null || break
  sleep 0.5
done
if kill -0 "$pid" 2>/dev/null; then
  echo "smoke: the app didn't quit"
  kill -KILL "$pid" 2>/dev/null || true
  fail=1
fi
out="$(cat "$log")"
tail -n 60 <<<"$out"
for want in "window\] macOS" "GPU " "first frame shown" "keyboard\] self-test: ok" "video\] self-test: ok" \
  "app thread done"; do
  grep -q "$want" <<<"$out" || { echo "smoke: no \"$want\" in the log"; fail=1; }
done
if grep -qE "renderer init failed|shader compile failed|pipeline failed|no Metal layer" <<<"$out"; then
  echo "smoke: drawing failed"
  fail=1
fi
if grep -qE "https?://" <<<"$out"; then
  echo "smoke: a link in the log"
  fail=1
fi
rm -f "$log"
[ "$fail" = 0 ] && echo "smoke: ok"
exit "$fail"
