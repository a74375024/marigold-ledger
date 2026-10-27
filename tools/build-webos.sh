#!/bin/bash
# Builds the webOS binary, checks its ABI and packages the .ipk.
#   MARIGOLD_SDK=/opt/arm-webos-linux-gnueabi_sdk-buildroot bash tools/build-webos.sh
# MARIGOLD_BUILD_NUMBER (CI run number) becomes the patch version: 0.1.<n>.
set -euo pipefail
cd "$(dirname "$0")/.."
: "${MARIGOLD_SDK:?set MARIGOLD_SDK (bash tools/fetch-sdk.sh prints it)}"
export MARIGOLD_SDK
build="${MARIGOLD_BUILD_DIR:-build-webos}"
gen=()
command -v ninja >/dev/null && gen=(-G Ninja)

cmake -S . -B "$build" "${gen[@]}" -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-webos.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --parallel
bash tools/abi-check.sh "$build/src/marigold"

stage="$build/stage"
rm -rf "$stage"
mkdir -p "$stage/fonts" "$stage/icons" "$stage/licenses"
"$MARIGOLD_SDK/bin/arm-webos-linux-gnueabi-strip" -o "$stage/marigold" "$build/src/marigold"
cp app/icon.png app/icon-large.png app/splash.png "$stage/"
cp app/fonts/*.ttf "$stage/fonts/"
cp app/icons/*.png "$stage/icons/"
cp LICENSE "$stage/licenses/LICENSE-GPL-3.0.txt"
cp CREDITS.md "$stage/licenses/CREDITS.md"
cp app/fonts/InterDisplay-OFL.txt "$stage/licenses/"
cp vendor/qrcodegen/LICENSE "$stage/licenses/qrcodegen-MIT.txt"
version="0.1.${MARIGOLD_BUILD_NUMBER:-0}"
python3 - "$stage/appinfo.json" "$version" <<'PY'
import json, sys
a = json.load(open("app/appinfo.json"))
a["version"] = sys.argv[2]
json.dump(a, open(sys.argv[1], "w"), indent=2)
PY
bash tools/mkipk.sh "$stage" "$build/ipk"
