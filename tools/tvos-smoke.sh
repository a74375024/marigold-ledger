#!/bin/bash
# Starts the Apple TV app in a tvOS Simulator and reads its log: it must open
# its window, compile its shader, show a frame and still be running after
# half a minute. No screenshots; the log never holds a link.
#   bash tools/tvos-smoke.sh path/to/Marigold.app
set -euo pipefail
app="$1"
runtime="$(xcrun simctl list runtimes -j | python3 -c '
import json, sys
r = [x for x in json.load(sys.stdin)["runtimes"] if x.get("platform") == "tvOS" and x.get("isAvailable")]
print(r[-1]["identifier"] if r else "")')"
device="$(xcrun simctl list devicetypes -j | python3 -c '
import json, sys
d = [x for x in json.load(sys.stdin)["devicetypes"] if x["name"].startswith("Apple TV 4K")]
print(d[-1]["identifier"] if d else "")')"
[ -n "$runtime" ] && [ -n "$device" ] || { echo "smoke: no tvOS Simulator on this machine" >&2; exit 1; }
udid="$(xcrun simctl create marigold-smoke "$device" "$runtime")"
trap 'xcrun simctl shutdown "$udid" >/dev/null 2>&1 || true; xcrun simctl delete "$udid" >/dev/null 2>&1 || true' EXIT
xcrun simctl boot "$udid"
xcrun simctl bootstatus "$udid" -b >/dev/null
xcrun simctl install "$udid" "$app"
xcrun simctl launch "$udid" org.marigold.app
sleep 30
log="$(xcrun simctl spawn "$udid" log show --style compact --last 5m --predicate 'subsystem == "org.marigold.app"' || true)"
echo "$log" | tail -n 40
fail=0
xcrun simctl spawn "$udid" launchctl list | grep -q "org.marigold.app" || { echo "smoke: the app is not running"; fail=1; }
for want in "window\] tvOS" "GPU " "first frame shown"; do
  echo "$log" | grep -q "$want" || { echo "smoke: no \"$want\" in the log"; fail=1; }
done
if echo "$log" | grep -qE "renderer init failed|shader compile failed|pipeline failed|no Metal layer"; then
  echo "smoke: drawing failed"; fail=1
fi
[ "$fail" = 0 ] && echo "smoke: ok"
exit "$fail"
