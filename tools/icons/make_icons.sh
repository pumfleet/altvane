#!/usr/bin/env bash
# Regenerates a product's icons and installer artwork in products/<product>/packaging/
# (macOS: needs iconutil and sips).
#   cmake --preset mac-arm64-debug -DALTVANE_BUILD_TOOLS=ON && cmake --build --preset mac-arm64-debug --target AltvaneIcons
#   tools/icons/make_icons.sh build/mac-arm64-debug/tools/icons/AltvaneIcons_artefacts/Debug/AltvaneIcons eq EQ
#   tools/icons/make_icons.sh <tool> compressor COMPRESSOR knee    (optional motif behind the mark: grid, knee or ceiling)
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
iconutil -c icns "$iconset" -o "$out/installer.icns"

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
sips -s format bmp "$work/png/banner.png" --out "$out/banner.bmp" > /dev/null
sips -s format bmp "$work/png/dialog.png" --out "$out/dialog.bmp" > /dev/null

echo "icons written to products/$product/packaging/"
