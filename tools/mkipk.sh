#!/bin/bash
# Packages a staged app folder as a webOS .ipk, in the same layout
# ares-package writes (checked against a Legacy release): an `ar` archive of
# debian-binary, control.tar.gz and data.tar.gz, with the app under
# usr/palm/applications/<id>/ and usr/palm/packages/<id>/packageinfo.json.
#   bash tools/mkipk.sh <staged app folder> <output folder>
set -euo pipefail
app="${1:?usage: mkipk.sh <app folder> <out folder>}"
out="${2:?usage: mkipk.sh <app folder> <out folder>}"
read -r id version main < <(python3 -c '
import json, sys
a = json.load(open(sys.argv[1]))
print(a["id"], a["version"], a["main"])' "$app/appinfo.json")

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
data="$work/data"
mkdir -p "$data/usr/palm/applications/$id" "$data/usr/palm/packages/$id"
cp -R "$app"/. "$data/usr/palm/applications/$id/"
printf '{\n  "id": "%s",\n  "version": "%s",\n  "app": "%s"\n}\n' "$id" "$version" "$id" \
  > "$data/usr/palm/packages/$id/packageinfo.json"
find "$data" -type d -exec chmod 755 {} +
find "$data" -type f -exec chmod 644 {} +
chmod 755 "$data/usr/palm/applications/$id/$main"

# Reproducible archives: fixed owner, order and time; gzip without a name.
stamp="${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2>/dev/null || echo 0)}"
tarflags=(--format=gnu --owner=0 --group=0 --numeric-owner --sort=name --mtime="@$stamp")
size=$(du -sb "$data" | cut -f1)
cat > "$work/control" <<CONTROL
Package: $id
Version: $version
Section: misc
Priority: optional
Architecture: arm
Installed-Size: $size
Maintainer: N/A <nobody@example.com>
Description: This is a webOS application.
webOS-Package-Format-Version: 2
webOS-Packager-Version: x.y.x
CONTROL
(cd "$data" && tar "${tarflags[@]}" -cf - usr | gzip -9n > "$work/data.tar.gz")
(cd "$work" && tar "${tarflags[@]}" -cf - control | gzip -9n > "$work/control.tar.gz")
printf '2.0\n' > "$work/debian-binary"

mkdir -p "$out"
out="$(cd "$out" && pwd)"
ipk="$out/${id}_${version}_arm.ipk"
rm -f "$ipk"
(cd "$work" && ar rcD "$ipk" debian-binary control.tar.gz data.tar.gz)

# Verify what was written, the way Legacy's arm.sh does: unpack the ar,
# check the member order, the executable bit and that nothing private went in.
check="$(mktemp -d)"
trap 'rm -rf "$work" "$check"' EXIT
(cd "$check" && ar x "$ipk")
[ "$(ar t "$ipk" | tr '\n' ' ')" = "debian-binary control.tar.gz data.tar.gz " ] \
  || { echo "mkipk: wrong member order" >&2; exit 1; }
mode=$(tar tvzf "$check/data.tar.gz" | awk -v f="usr/palm/applications/$id/$main" '$6 == f {print $1}')
[ "$mode" = "-rwxr-xr-x" ] || { echo "mkipk: $main has mode '${mode:-missing}'" >&2; exit 1; }
if tar tzf "$check/data.tar.gz" | grep -Ei '\.(csv|tsv|m3u8?|log|env|pem|key)$|local\.properties|test-link|credential|secret'; then
  echo "mkipk: private or data files in the package" >&2
  exit 1
fi
echo "$ipk ($(du -h "$ipk" | cut -f1))"
