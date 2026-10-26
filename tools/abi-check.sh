#!/bin/bash
# Checks that the webOS binary only needs what every supported TV has.
#   bash tools/abi-check.sh build-webos/src/marigold
# Needs MARIGOLD_SDK (for the cross readelf/nm).
set -euo pipefail
bin="${1:?usage: abi-check.sh <binary>}"
here="$(cd "$(dirname "$0")" && pwd)"
tc="${MARIGOLD_SDK:?set MARIGOLD_SDK}/bin/arm-webos-linux-gnueabi"
fail=0

# 1. Shared libraries. libcurl and luna-service2 are dlopen'd at run time and
#    must never be linked (the SDK ships libcurl.so.4, the TVs only .so.5);
#    libstdc++ is linked statically because the TVs' copy is much older.
allowed="libSDL2-2.0.so.0 libSDL2_ttf-2.0.so.0 libGLESv2.so.2 libEGL.so.1 libdl.so.2
libpthread.so.0 libm.so.6 libc.so.6 ld-linux.so.3 librt.so.1"
for lib in $("$tc-readelf" -d "$bin" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
  if ! grep -qx "$lib" <<<"$(tr ' ' '\n' <<<"$allowed")"; then
    echo "abi-check: unexpected shared library $lib" >&2
    fail=1
  fi
done

# 2. glibc: nothing newer than the SDK sysroot's 2.12.
max=$("$tc-objdump" -T "$bin" | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' | sort -V | tail -1)
if [ -n "$max" ] && [ "$(printf '%s\n2.12\n' "$max" | sort -V | tail -1)" != "2.12" ]; then
  echo "abi-check: needs glibc $max, TVs have 2.12" >&2
  fail=1
fi

# 3. SDL and SDL_ttf calls must exist in the oldest supported versions
#    (webOS 3.4 ships SDL 2.0.2). webOS-only SDL entry points are dlsym'd.
undef=$("$tc-nm" -D --undefined-only "$bin" | awk '{print $2}')
check() {
  local prefix="$1" list="$2"
  for sym in $(grep -E "^${prefix}_" <<<"$undef" || true); do
    if ! grep -qx "$sym" "$list"; then
      echo "abi-check: $sym is not in $(basename "$list")" >&2
      fail=1
    fi
  done
}
check SDL "$here/abi/sdl2-2.0.2.txt"
check TTF "$here/abi/sdl2_ttf-2.0.12.txt"

if [ "$fail" -ne 0 ]; then exit 1; fi
echo "abi-check: ok (glibc ${max:-none}, $(grep -cE '^(SDL|TTF)_' <<<"$undef") SDL/TTF symbols)"
