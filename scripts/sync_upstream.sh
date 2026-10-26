#!/usr/bin/env bash
# Re-vendors a product's upstream engine at a new upstream ref and moves the pin in its UPSTREAM file.
#   scripts/sync_upstream.sh <product> [ref]     (ref: branch, tag or commit; default main)
# For a new product, write products/<product>/UPSTREAM with COMMIT="" and run this to do the first import.
# Stages the result for review. Upstream changes to files the product adapted rather than vendored (WATCH) and to
# shared submodules are listed, not applied: port them by hand, then build, test and commit.
set -euo pipefail
source "$(dirname "$0")/upstream_common.sh"
ref="${2:-main}"

for path in $VENDORED; do
    if [ -n "$(git status --porcelain -- "$product_dir/$path" 2>/dev/null)" ]; then
        echo "$product_dir/$path has uncommitted changes; commit or discard them first" >&2
        exit 1
    fi
done

old=""
if [ -n "$COMMIT" ]; then
    old="$(fetch_upstream "$COMMIT")"
fi
new="$(fetch_upstream "$ref")"
if [ "$old" = "$new" ]; then
    echo "$product is already vendored at $ref (${new:0:12})"
    exit 0
fi
if [ -n "$old" ]; then
    echo "syncing $product from ${old:0:12} to ${new:0:12} ($ref)"
else
    echo "importing $product's engine at ${new:0:12} ($ref)"
fi

for path in $VENDORED; do
    if [ -n "$(git ls-files -- "$product_dir/$path")" ]; then
        git rm -r -q --cached -- "$product_dir/$path"
    fi
    rm -rf -- "${product_dir:?}/$path"
    git read-tree --prefix="$product_dir/$path/" -u "$new:$path"
done

for sub in $SUBMODULES; do
    sha="$(gitlink "$new" "$sub")"
    if ! git config -f .gitmodules --get-regexp '^submodule\..*\.path$' | awk '{print $2}' | grep -qx "$product_dir/$sub"; then
        # first import: register the submodule with the URL upstream uses
        url="$(git show "$new:.gitmodules" | git config -f - --get "submodule.$sub.url")"
        git submodule add --force -- "$url" "$product_dir/$sub" >/dev/null
    fi
    git update-index --cacheinfo "160000,$sha,$product_dir/$sub"
    git submodule update --init -- "$product_dir/$sub"
done

sed -i.bak "s/^COMMIT=.*/COMMIT=\"$new\"/" "$pin_file" && rm -f "$pin_file.bak"
git add -- "$pin_file"

echo
echo "vendored changes (staged):"
git --no-pager diff --cached --stat -- $(for path in $VENDORED; do echo "$product_dir/$path"; done) $(for sub in $SUBMODULES; do echo "$product_dir/$sub"; done)

echo
if [ -n "$old" ]; then
    echo "upstream changes to adapted files, to port by hand:"
    git --no-pager diff --stat "$old" "$new" -- $WATCH || true
else
    echo "upstream files to adapt by hand (not vendored):"
    git ls-tree -r --name-only "$new" -- $WATCH | sed 's/^/  /'
fi

for sub in $SHARED_SUBMODULES; do
    upstream="$(gitlink "$new" "$sub")"
    ours="$(gitlink "third_party/$sub")"
    if [ "$upstream" != "$ours" ]; then
        echo
        echo "upstream pins $sub at ${upstream:0:12}, but third_party/$sub (shared by all products) is at"
        echo "${ours:0:12}; update it deliberately if needed (git -C third_party/$sub checkout $upstream)"
    fi
done

echo
echo "next: review 'git diff --cached', port the changes above, build and run the tests, then commit"
