# Shared by check_upstream.sh and sync_upstream.sh; sourced, not run.
# Loads products/<product>/UPSTREAM and makes sure its commit is available locally.

root="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
cd "$root"

product="${1:?usage: $(basename "$0") <product> ...}"
pin_file="products/$product/UPSTREAM"
[ -f "$pin_file" ] || { echo "$pin_file not found: $product does not vendor an upstream engine" >&2; exit 1; }

REPO="" COMMIT="" VENDORED="" SUBMODULES="" SHARED_SUBMODULES="" WATCH=""
# shellcheck disable=SC1090
source "$pin_file"
product_dir="products/$product"

# fetch an upstream ref (branch, tag or commit) and print its commit id
fetch_upstream() {
    local ref="$1"
    if git cat-file -e "$ref^{commit}" 2>/dev/null; then
        git rev-parse "$ref^{commit}"
        return
    fi
    git fetch --quiet --no-tags "$REPO" "$ref" >&2
    git rev-parse "FETCH_HEAD^{commit}"
}

# the tree id of a working-tree directory, including uncommitted and untracked (non-ignored) changes
worktree_tree() {
    local dir="$1" index
    index="$(mktemp)"
    cp "$(git rev-parse --git-path index)" "$index"
    GIT_INDEX_FILE="$index" git add -A -- "$dir"
    GIT_INDEX_FILE="$index" git write-tree --prefix="$dir/"
    rm -f "$index"
}

# the commit a submodule (gitlink) is pinned to, in a commit or (with no commit) in the index
gitlink() {
    if [ $# -eq 2 ]; then
        git ls-tree "$1" -- "$2" | awk '$2 == "commit" { print $3 }'
    else
        git ls-files -s -- "$1" | awk '$1 == "160000" { print $2 }'
    fi
}
