#!/usr/bin/env bash
# Checks that a product's vendored upstream engine is byte-identical to the commit pinned in its UPSTREAM file.
#   scripts/check_upstream.sh <product>
# Compares the working tree (so uncommitted edits count) and the staged submodule pins. Exits non-zero on drift.
set -euo pipefail
source "$(dirname "$0")/upstream_common.sh"

if [ -z "$COMMIT" ]; then
    echo "$product's engine has not been imported yet: run scripts/sync_upstream.sh $product <ref>" >&2
    exit 1
fi
commit="$(fetch_upstream "$COMMIT")"
status=0

for path in $VENDORED; do
    upstream="$(git rev-parse "$commit:$path")"
    ours="$(worktree_tree "$product_dir/$path")"
    if [ "$upstream" = "$ours" ]; then
        echo "ok        $product_dir/$path"
    else
        echo "MODIFIED  $product_dir/$path differs from upstream ${COMMIT:0:12}:$path" >&2
        git --no-pager diff --stat "$upstream" "$ours" >&2
        status=1
    fi
done

for sub in $SUBMODULES; do
    upstream="$(gitlink "$commit" "$sub")"
    ours="$(gitlink "$product_dir/$sub")"
    if [ "$upstream" = "$ours" ]; then
        echo "ok        $product_dir/$sub @ ${ours:0:12}"
    else
        echo "MODIFIED  $product_dir/$sub is pinned at ${ours:0:12}, upstream at ${upstream:0:12}" >&2
        status=1
    fi
done

if [ $status -ne 0 ]; then
    echo >&2
    echo "The vendored engine must stay identical to upstream. Revert the changes, or move the pin with" >&2
    echo "scripts/sync_upstream.sh $product <ref>." >&2
fi
exit $status
