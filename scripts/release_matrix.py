#!/usr/bin/env python3
"""Print the release build matrix for one product, as GitHub Actions JSON.

    scripts/release_matrix.py <product>

Every entry of .github/release-matrix.json, except platforms the product's PLATFORMS file leaves out
(no PLATFORMS file: every platform)."""
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent
product = sys.argv[1]
entries = json.loads((root / ".github" / "release-matrix.json").read_text())
platforms_file = root / "products" / product / "PLATFORMS"
if platforms_file.exists():
    platforms = {line.strip() for line in platforms_file.read_text().splitlines()
                 if line.strip() and not line.startswith("#")}
    entries = [e for e in entries if e["platform"] in platforms]
if not entries:
    sys.exit(f"{product} builds on no release platform")
print(json.dumps({"include": entries}, separators=(",", ":")))
