#!/bin/bash
# Downloads, verifies and relocates the webOS SDK into $1 (default /opt).
#   bash tools/fetch-sdk.sh [/opt]
# Prints the SDK folder to use as MARIGOLD_SDK.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=sdk.conf
. "$here/sdk.conf"
dest="${1:-/opt}"
sdk="$dest/arm-webos-linux-gnueabi_sdk-buildroot"
if [ ! -x "$sdk/bin/arm-webos-linux-gnueabi-g++" ]; then
  mkdir -p "$dest"
  tmp="$(mktemp)"
  curl -fsSL --retry 3 "$MARIGOLD_SDK_URL" -o "$tmp"
  echo "$MARIGOLD_SDK_SHA256  $tmp" | sha256sum -c - >&2
  tar -xzf "$tmp" -C "$dest"
  rm -f "$tmp"
  # The buildroot SDK stores the build host's absolute paths; relocate once.
  "$sdk/relocate-sdk.sh" >/dev/null
fi
echo "$sdk"
