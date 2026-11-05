#!/bin/bash
# Builds the Mac app on a Mac with Xcode's command line tools.
#   bash tools/build-mac.sh [vlckit-cache-dir]
# Makes build-mac/Marigold.app (Apple Silicon and Intel in one, signed ad
# hoc: no developer account) and build-mac/marigold-mac-NN.dmg. The path of
# the .app is printed last.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
# shellcheck source=mac.conf
. "$here/mac.conf"
cache="${1:-$HOME/.cache/marigold-vlckit-mac}"
build="${MARIGOLD_BUILD_NUMBER:-dev}"
out="$root/build-mac"

# The framework, at a path without spaces (the release's folder has some).
fw="$cache/$MARIGOLD_VLCKIT_MAC_VERSION/VLCKit.framework"
if [ ! -d "$fw" ]; then
  mkdir -p "$cache/$MARIGOLD_VLCKIT_MAC_VERSION"
  tmp="$(mktemp -d)"
  curl -fsSL --retry 3 "$MARIGOLD_VLCKIT_MAC_URL" -o "$tmp/vlckit.tar.xz"
  echo "$MARIGOLD_VLCKIT_MAC_SHA256  $tmp/vlckit.tar.xz" | shasum -a 256 -c - >&2
  tar -xJf "$tmp/vlckit.tar.xz" -C "$tmp"
  pkg="$tmp/VLCKit - binary package"
  ditto "$pkg/VLCKit.xcframework/macos-arm64_x86_64/VLCKit.framework" "$fw"
  cp "$pkg/COPYING.txt" "$cache/$MARIGOLD_VLCKIT_MAC_VERSION/COPYING.txt"
  rm -rf "$tmp"
fi

cmake -S "$root" -B "$out" -DMARIGOLD_MAC_APP=ON -DCMAKE_BUILD_TYPE=Release \
  -DMARIGOLD_BUILD_TESTS=OFF -DMARIGOLD_BUILD_PREVIEW=OFF \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DMARIGOLD_VLCKIT="$fw" >&2
# One compile per core: these Macs have 3 cores and 7 GB (Build 133).
cmake --build "$out" --target Marigold --parallel "$(sysctl -n hw.ncpu)" >&2

app="$out/src/platform/macos/Marigold.app"
[ -x "$app/Contents/MacOS/Marigold" ] || { echo "build-mac: no $app" >&2; exit 1; }
res="$app/Contents/Resources"
mkdir -p "$app/Contents/Frameworks" "$res/assets/fonts" "$res/assets/icons" "$res/licenses"
rm -rf "$app/Contents/Frameworks/VLCKit.framework"
ditto "$fw" "$app/Contents/Frameworks/VLCKit.framework"
# Headers stay out of the app.
rm -rf "$app/Contents/Frameworks/VLCKit.framework/Versions/A/Headers" \
  "$app/Contents/Frameworks/VLCKit.framework/Versions/A/PrivateHeaders" \
  "$app/Contents/Frameworks/VLCKit.framework/Versions/A/Modules" \
  "$app/Contents/Frameworks/VLCKit.framework/Headers" \
  "$app/Contents/Frameworks/VLCKit.framework/PrivateHeaders" \
  "$app/Contents/Frameworks/VLCKit.framework/Modules"
# The same fonts and sport icons as the TV apps, as plain files.
cp "$root"/app/fonts/InterDisplay-{Regular,Medium,Bold}.ttf "$res/assets/fonts/"
cp "$root"/app/icons/*.png "$res/assets/icons/"
cp "$cache/$MARIGOLD_VLCKIT_MAC_VERSION/COPYING.txt" "$res/licenses/VLCKit-COPYING.txt"
cp "$root/app/fonts/InterDisplay-OFL.txt" "$res/licenses/"

# The icon, from one 1024-point picture.
set_dir="$(mktemp -d)/Marigold.iconset"
mkdir -p "$set_dir"
for s in 16 32 128 256 512; do
  sips -z "$s" "$s" "$root/tools/mac/icon-1024.png" --out "$set_dir/icon_${s}x${s}.png" >/dev/null
  d=$((s * 2))
  sips -z "$d" "$d" "$root/tools/mac/icon-1024.png" --out "$set_dir/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$set_dir" -o "$res/Marigold.icns"
rm -rf "$(dirname "$set_dir")"

# Signed ad hoc (Apple Silicon runs nothing unsigned): VLCKit first, then
# the app around it.
codesign --force --sign - "$app/Contents/Frameworks/VLCKit.framework"
codesign --force --sign - "$app"
codesign --verify --deep --strict "$app" >&2
lipo -info "$app/Contents/MacOS/Marigold" >&2

# The disk image: the app and a link to Applications, to drag it onto.
stage="$(mktemp -d)"
ditto "$app" "$stage/Marigold.app"
ln -s /Applications "$stage/Applications"
dmg="$out/marigold-mac-$build.dmg"
rm -f "$dmg"
# hdiutil now and then finds its disk busy: a few tries.
for try in 1 2 3; do
  if hdiutil create -quiet -volname "Marigold" -srcfolder "$stage" -ov -format UDZO "$dmg"; then break; fi
  [ "$try" = 3 ] && { echo "build-mac: no disk image" >&2; exit 1; }
  sleep 5
done
rm -rf "$stage"
ls -la "$out"/*.dmg >&2
echo "$app"
