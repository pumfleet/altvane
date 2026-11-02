#!/usr/bin/env python3
"""Print the release notes for one product's release: its CHANGELOG section and how to install an unsigned build.

    scripts/release_notes.py <product> <version>

The section is the one headed `## <version>` in products/<product>/CHANGELOG.md. Its headings move up a level and its
hard-wrapped lines are joined, since GitHub keeps line breaks in release notes."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
product, version = sys.argv[1], sys.argv[2]
changelog = (root / "products" / product / "CHANGELOG.md").read_text()
match = re.search(rf"^## {re.escape(version)}\n(.*?)(?=^## |\Z)", changelog, re.S | re.M)
if not match:
    sys.exit(f"products/{product}/CHANGELOG.md has no '## {version}' section")
notes = match.group(1).strip()
notes = re.sub(r"^### ", "## ", notes, flags=re.M)
notes = re.sub(r"\n {2,}(?=\S)", " ", notes)  # a list item's continuation lines
notes = re.sub(r"(?<=[^\n])\n(?=[^\n\-#*\s|])", " ", notes)  # a paragraph's

platforms_file = root / "products" / product / "PLATFORMS"
platforms = None
if platforms_file.exists():
    platforms = {line.strip() for line in platforms_file.read_text().splitlines()
                 if line.strip() and not line.startswith("#")}


def builds_on(system: str) -> bool:
    return platforms is None or any(p.startswith(system) for p in platforms)


install = ["## Installing", "", "These builds are not signed yet.", ""]
if builds_on("Darwin"):
    install.append("- **macOS:** if macOS says the installer can't be opened, open System Settings → Privacy & Security "
                   "and choose **Open Anyway**.")
if builds_on("Windows"):
    install.append("- **Windows:** if SmartScreen stops the installer, choose **More info → Run anyway**.")
if builds_on("Linux"):
    install.append("- **Linux:** unzip and copy the plug-ins into `~/.vst3` or `~/.lv2`.")
install += ["", "Pick the AVX2 build on x86-64 machines from about 2013 onwards; the plain x86-64 build runs everywhere."]
print(notes + "\n\n" + "\n".join(install))
