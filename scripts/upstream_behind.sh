#!/usr/bin/env bash
# Lists the upstream commits a product's vendored engine is missing: those on the upstream ref since the pinned commit.
#   scripts/upstream_behind.sh <product> [ref]     (ref: upstream branch or tag; default main)
# Prints one tab-separated line per commit, newest first: <commit id> <date> <subject>; nothing when the pin is current.
# Changes nothing (it only fetches); CI's weekly upstream watch runs it for every products/*/UPSTREAM.
set -euo pipefail
source "$(dirname "$0")/upstream_common.sh"
ref="${2:-main}"

if [ -z "$COMMIT" ]; then
    echo "$product's engine has not been imported yet: run scripts/sync_upstream.sh $product <ref>" >&2
    exit 1
fi
pin="$(fetch_upstream "$COMMIT")"
head="$(fetch_upstream "$ref")"

if ! git merge-base --is-ancestor "$pin" "$head"; then
    echo "warning: $product's pin ${pin:0:12} is not on upstream $ref (was upstream's history rewritten?)" >&2
fi
git --no-pager log --format='%H%x09%as%x09%s' "$pin..$head"
