#!/bin/bash
# Repository hygiene, run by CI on every push:
#  - no channel lists, playback logs, exports or credential files are tracked;
#  - no stream links or login parameters in any tracked file;
#  - every commit is authored by the project's noreply identity.
#   bash tools/hygiene.sh
set -euo pipefail
cd "$(dirname "$0")/.."
fail=0
allow="tools/hygiene-allow.txt"
noreply="344543110+a74375024@users.noreply.github.com"

files=$(git ls-files)

bad=$(grep -Ei '\.(csv|tsv|m3u|m3u8|log|env|pem|key|p12|jks|keystore|token)$|(^|/)local\.properties$|(^|/)\.env' <<<"$files" || true)
if [ -n "$bad" ]; then
  echo "hygiene: files that must never be committed:" >&2
  echo "$bad" >&2
  fail=1
fi

# Text files as an array: names can hold spaces and dashes (the Apple TV
# asset catalog's do).
mapfile -t text < <(git grep -I -l '' -- . || true)
# Xtream live links and playlist/API links that carry a login. The reserved
# .invalid domain is allowed (tests use it).
links=$(grep -nEI 'https?://[^ "'"'"']+/live/[^/ "'"'"']+/[^/ "'"'"']+/[0-9]+|(get|player_api|xmltv)\.php\?[^ "'"'"']*(username|password)=' -- "${text[@]}" 2>/dev/null \
        | grep -v '\.invalid' || true)
if [ -n "$links" ]; then
  echo "hygiene: stream links or login links in tracked files:" >&2
  echo "$links" >&2
  fail=1
fi
exempt=$(grep -v '^#' "$allow" | sed '/^$/d')
creds=$(grep -nEI '[?&](username|password|passwd|token)=[A-Za-z0-9%._-]{3,}' -- "${text[@]}" 2>/dev/null \
        | grep -vF -f <(sed 's/$/:/' <<<"$exempt") || true)
if [ -n "$creds" ]; then
  echo "hygiene: login parameters in tracked files:" >&2
  echo "$creds" >&2
  fail=1
fi

# Commit identity: authors must be the noreply identity. Committers may also be
# GitHub itself (merges made on github.com).
while IFS='|' read -r sha ae ce; do
  if [ "$ae" != "$noreply" ] || { [ "$ce" != "$noreply" ] && [ "$ce" != "noreply@github.com" ]; }; then
    echo "hygiene: commit $sha has identity $ae / $ce" >&2
    fail=1
  fi
done < <(git log --format='%h|%ae|%ce')

if [ "$fail" -ne 0 ]; then exit 1; fi
echo "hygiene: ok ($(wc -l <<<"$files") files, $(git rev-list --count HEAD) commits)"
