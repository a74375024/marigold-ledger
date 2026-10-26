#!/bin/bash
# Writes vendor/legacy/patches/<file>.patch for every vendored file, as a diff
# against Legacy at the pinned commit (vendor/legacy/UPSTREAM.md).
#   bash tools/legacy-diff.sh [path to a nuvio-native-legacy checkout]
# Without a path, the pinned commit is fetched into a temporary folder.
set -euo pipefail
cd "$(dirname "$0")/.."
pin=$(sed -n 's/^- Pinned commit: `\([0-9a-f]*\)`.*/\1/p' vendor/legacy/UPSTREAM.md)
up="${1:-}"
if [ -z "$up" ]; then
  up="$(mktemp -d)"
  trap 'rm -rf "$up"' EXIT
  git -C "$up" init -q
  git -C "$up" fetch -q --depth 1 https://github.com/iqui27/nuvio-native-legacy "$pin"
  git -C "$up" checkout -q FETCH_HEAD
fi
[ "$(git -C "$up" rev-parse HEAD)" = "$pin" ] || { echo "legacy-diff: checkout is not at $pin" >&2; exit 1; }
out=vendor/legacy/patches
rm -rf "$out"
mkdir -p "$out"
for f in vendor/legacy/src/*; do
  name=$(basename "$f")
  if ! diff -u --label "upstream/src/$name" --label "vendor/legacy/src/$name" \
      "$up/src/$name" "$f" > "$out/$name.patch"; then
    :  # differences found: keep the patch
  else
    rm -f "$out/$name.patch"   # identical to upstream
  fi
done
echo "legacy-diff: $(ls "$out" | wc -l) patched files, $(ls vendor/legacy/src | wc -l) vendored"
