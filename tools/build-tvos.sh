#!/bin/bash
# Builds the Apple TV app on a Mac with Xcode.
#   bash tools/build-tvos.sh [device|simulator] [vlckit-cache-dir]
# device (the default): an unsigned build-tvos/marigold-tvos-NN.ipa, for a
# signing service or Xcode to sign and install. simulator: the .app for the
# tvOS Simulator (the path is printed last).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
# shellcheck source=tvos.conf
. "$here/tvos.conf"
kind="${1:-device}"
cache="${2:-$HOME/.cache/marigold-tvvlckit}"
build="${MARIGOLD_BUILD_NUMBER:-dev}"

xcf="$cache/$MARIGOLD_VLCKIT_VERSION/TVVLCKit-binary/TVVLCKit.xcframework"
if [ ! -d "$xcf" ]; then
  mkdir -p "$cache/$MARIGOLD_VLCKIT_VERSION"
  tmp="$(mktemp)"
  curl -fsSL --retry 3 "$MARIGOLD_VLCKIT_URL" -o "$tmp"
  echo "$MARIGOLD_VLCKIT_SHA256  $tmp" | shasum -a 256 -c - >&2
  tar -xJf "$tmp" -C "$cache/$MARIGOLD_VLCKIT_VERSION" TVVLCKit-binary/TVVLCKit.xcframework TVVLCKit-binary/COPYING.txt
  rm -f "$tmp"
fi

case "$kind" in
  device) sdk=appletvos; slice=tvos-arm64; arch=arm64; out="$root/build-tvos" ;;
  # The simulator runs on this Mac's own kind of processor (Apple Silicon or
  # Intel); the framework's simulator slice has both.
  simulator) sdk=appletvsimulator; slice=tvos-arm64_x86_64-simulator; arch="$(uname -m)"; out="$root/build-tvos-sim" ;;
  *) echo "build-tvos: device or simulator" >&2; exit 2 ;;
esac

cmake -S "$root" -B "$out" -G Xcode \
  -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES="$arch" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DMARIGOLD_VLCKIT="$xcf/$slice/TVVLCKit.framework" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO >&2
cmake --build "$out" --config Release --target Marigold -- -quiet CODE_SIGNING_ALLOWED=NO >&2

app="$out/src/platform/tvos/Release-$sdk/Marigold.app"
[ -x "$app/Marigold" ] || { echo "build-tvos: no $app" >&2; exit 1; }
[ -f "$app/Frameworks/TVVLCKit.framework/TVVLCKit" ] || { echo "build-tvos: TVVLCKit not embedded" >&2; exit 1; }
# The same fonts and sport icons as the TV package, as plain files.
mkdir -p "$app/assets/fonts" "$app/assets/icons" "$app/licenses"
cp "$root"/app/fonts/InterDisplay-{Regular,Medium,Bold}.ttf "$app/assets/fonts/"
cp "$root"/app/icons/*.png "$app/assets/icons/"
[ -f "$app/Assets.car" ] || echo "build-tvos: warning: no Assets.car (the app icon and Top Shelf are missing)" >&2
# VLC's licence travels with it.
cp "$cache/$MARIGOLD_VLCKIT_VERSION/TVVLCKit-binary/COPYING.txt" "$app/licenses/TVVLCKit-COPYING.txt"
cp "$root/app/fonts/InterDisplay-OFL.txt" "$app/licenses/"

ls -la "$app" >&2

if [ "$kind" = device ]; then
  stage="$(mktemp -d)"
  mkdir -p "$stage/Payload"
  cp -R "$app" "$stage/Payload/"
  ipa="$out/marigold-tvos-$build.ipa"
  rm -f "$ipa"
  (cd "$stage" && zip -qry "$ipa" Payload)
  rm -rf "$stage"
  echo "$ipa"
else
  echo "$app"
fi
