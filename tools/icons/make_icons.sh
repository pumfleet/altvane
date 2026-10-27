#!/usr/bin/env bash
# Regenerates a product's icons and installer artwork in products/<product>/packaging/
# (uses iconutil and sips on macOS; elsewhere the .icns is assembled here and the .bmp files come from the tool).
#   cmake --preset mac-arm64-debug -DALTVANE_BUILD_TOOLS=ON && cmake --build --preset mac-arm64-debug --target AltvaneIcons
#   tools/icons/make_icons.sh build/mac-arm64-debug/tools/icons/AltvaneIcons_artefacts/Debug/AltvaneIcons eq EQ
#   tools/icons/make_icons.sh <tool> compressor COMPRESSOR knee    (optional motif behind the mark: grid, knee, ride or ess)
set -euo pipefail

tool="${1:?path to the AltvaneIcons executable}"
product="${2:?product directory name under products/, e.g. eq}"
label="${3:?label shown under the brand name, e.g. EQ}"
motif="${4:-grid}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/products/$product/packaging"
[ -d "$out" ] || { echo "no such directory: $out" >&2; exit 1; }
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

"$tool" "$work/png" "$label" "$motif"

# plug-in / standalone icon
cp "$work/png/icon_1024.png" "$out/icon.png"

# macOS installer icon
iconset="$work/installer.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
    cp "$work/png/icon_${size}.png" "$iconset/icon_${size}x${size}.png"
    cp "$work/png/icon_$((size * 2)).png" "$iconset/icon_${size}x${size}@2x.png"
done
if command -v iconutil > /dev/null; then
    iconutil -c icns "$iconset" -o "$out/installer.icns"
else
    # the same PNG-compressed entries iconutil writes
    python3 - "$work/png" "$out/installer.icns" <<'PY'
import struct, sys, pathlib
src = pathlib.Path(sys.argv[1])
types = [("icp4", 16), ("icp5", 32), ("ic07", 128), ("ic08", 256), ("ic09", 512),
         ("ic11", 32), ("ic12", 64), ("ic13", 256), ("ic14", 512), ("ic10", 1024)]
body = b""
for ostype, size in types:
    blob = (src / f"icon_{size}.png").read_bytes()
    body += ostype.encode() + struct.pack(">I", 8 + len(blob)) + blob
pathlib.Path(sys.argv[2]).write_bytes(b"icns" + struct.pack(">I", 8 + len(body)) + body)
PY
fi

# Windows icons: PNG-compressed .ico entries
python3 - "$work/png" "$out/icon.ico" "$out/installer.ico" <<'PY'
import struct, sys, pathlib, shutil
src = pathlib.Path(sys.argv[1])
sizes = [16, 24, 32, 48, 64, 128, 256]
blobs = [(s, (src / f"icon_{s}.png").read_bytes()) for s in sizes]
header = struct.pack("<HHH", 0, 1, len(blobs))
offset = 6 + 16 * len(blobs)
entries, data = b"", b""
for size, blob in blobs:
    dim = 0 if size >= 256 else size
    entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset + len(data))
    data += blob
out = pathlib.Path(sys.argv[2])
out.write_bytes(header + entries + data)
shutil.copy(out, sys.argv[3])
PY

# WiX installer artwork
if command -v sips > /dev/null; then
    sips -s format bmp "$work/png/banner.png" --out "$out/banner.bmp" > /dev/null
    sips -s format bmp "$work/png/dialog.png" --out "$out/dialog.bmp" > /dev/null
else
    cp "$work/png/banner.bmp" "$work/png/dialog.bmp" "$out/"
fi

echo "icons written to products/$product/packaging/"
