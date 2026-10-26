#!/usr/bin/env bash
# Re-vendors a product's upstream engine at a new upstream ref and moves the pin in its UPSTREAM file.
#   scripts/sync_upstream.sh <product> [ref]     (ref: branch, tag or commit; default main)
# Stages the result for review. Upstream changes to files the product adapted rather than vendored (WATCH) and to
# shared submodules are listed, not applied: port them by hand, then build, test and commit.
set -euo pipefail
source "$(dirname "$0")/upstream_common.sh"
ref="${2:-main}"

for path in $VENDORED; do
    if [ -n "$(git status --porcelain -- "$product_dir/$path")" ]; then
        echo "$product_dir/$path has uncommitted changes; commit or discard them first" >&2
        exit 1
    fi
done

old="$(fetch_upstream "$COMMIT")"
new="$(fetch_upstream "$ref")"
if [ "$old" = "$new" ]; then
    echo "$product is already vendored at $ref (${new:0:12})"
    exit 0
fi
echo "syncing $product from ${old:0:12} to ${new:0:12} ($ref)"

for path in $VENDORED; do
    git rm -r -q --cached -- "$product_dir/$path"
    rm -rf -- "${product_dir:?}/$path"
    git read-tree --prefix="$product_dir/$path/" -u "$new:$path"
done

for sub in $SUBMODULES; do
    sha="$(gitlink "$new" "$sub")"
    git update-index --cacheinfo "160000,$sha,$product_dir/$sub"
    git submodule update --init -- "$product_dir/$sub"
done

sed -i.bak "s/^COMMIT=.*/COMMIT=\"$new\"/" "$pin_file" && rm -f "$pin_file.bak"
git add -- "$pin_file"

echo
echo "vendored changes (staged):"
git --no-pager diff --cached --stat -- $(for path in $VENDORED; do echo "$product_dir/$path"; done) $(for sub in $SUBMODULES; do echo "$product_dir/$sub"; done)

echo
echo "upstream changes to adapted files, to port by hand:"
git --no-pager diff --stat "$old" "$new" -- $WATCH || true

for sub in $SHARED_SUBMODULES; do
    before="$(gitlink "$old" "$sub")"
    after="$(gitlink "$new" "$sub")"
    if [ "$before" != "$after" ]; then
        echo
        echo "upstream moved $sub from ${before:0:12} to ${after:0:12}; third_party/$sub is shared by all"
        echo "products, so update it deliberately if needed (git -C third_party/$sub checkout $after)"
    fi
done

echo
echo "next: review 'git diff --cached', port the changes above, build and run the tests, then commit"
