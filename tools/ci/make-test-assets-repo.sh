#!/usr/bin/env bash
# Assemble the PRIVATE test-asset repository from this machine's assets/, as a local git repository.
# For the owner, once, and again whenever the list changes. It never creates a remote, pushes, or
# touches a secret: it prints those commands for you to run (docs/development/gpu-ci-private-assets.md).
#
#   tools/ci/make-test-assets-repo.sh <out-dir> [--list tools/ci/test-assets.list]
#
# <out-dir> must be OUTSIDE this checkout (e.g. ~/Documents/GitHub/av-gen-test-assets). The files are
# copied (symlinks dereferenced) to <out-dir>/assets/<same relative path>, so tools/fetch-test-assets.sh
# can link each one back to the path the tests already read.
#
# tools/ci/test-assets.list names what goes in: one path or glob per line, relative to assets/ (a line
# ending in / takes that directory recursively).
# Everything in it must be gitignored HERE -- this script refuses a path the public repository tracks,
# because a tracked asset needs no private copy and a mistake the other way round is the dangerous one.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${1:?usage: make-test-assets-repo.sh <out-dir> [--list file]}"
shift
list="$root/tools/ci/test-assets.list"
[[ "${1:-}" == --list ]] && list="$2"

# The source is the PRIMARY checkout's assets/ (a worktree's are symlinks into it; both work).
src="$root/assets"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
case "$out/" in "$root"/*) echo "refusing: $out is inside the public checkout" >&2; exit 1 ;; esac
if [[ -d "$out/.git" ]] && [[ -n "$(git -C "$out" status --porcelain)" ]]; then
    echo "refusing: $out has uncommitted changes" >&2; exit 1
fi

files=()
while IFS= read -r line; do
    line="${line%%#*}"; line="$(echo "$line" | sed 's/[[:space:]]*$//')"
    [[ -z "$line" ]] && continue
    matched=0
    # A line ending in / is a whole directory, recursively; anything else is a glob.
    if [[ "$line" == */ ]]; then
        lister=(find -L "${line%/}" -type f)
    else
        lister=(compgen -G "$line")
    fi
    while IFS= read -r f; do
        f="${f#./}"
        [[ -f "$src/$f" ]] || continue
        if ! git -C "$root" check-ignore -q "assets/$f"; then
            echo "refusing: assets/$f is tracked by the public repository (not gitignored); remove it from $list" >&2
            exit 1
        fi
        files+=("$f"); matched=$((matched + 1))
    done < <(cd "$src" && "${lister[@]}" 2>/dev/null || true)
    [[ $matched -gt 0 ]] || { echo "refusing: '$line' in $list matches no file under $src" >&2; exit 1; }
done < "$list"

rm -rf "$out/assets"
total=0
for f in "${files[@]}"; do
    mkdir -p "$out/assets/$(dirname "$f")"
    cp -L "$src/$f" "$out/assets/$f"
    size=$(stat -f %z "$out/assets/$f")
    total=$((total + size))
    if [[ $size -gt 99000000 ]]; then
        echo "refusing: assets/$f is $((size / 1048576)) MB; GitHub rejects files over 100 MB" >&2; exit 1
    elif [[ $size -gt 50000000 ]]; then
        echo "warning: assets/$f is $((size / 1048576)) MB (GitHub warns above 50 MB)" >&2
    fi
done

(cd "$out" && find assets -type f | LC_ALL=C sort | xargs shasum -a 256 > SHA256SUMS)
cat > "$out/README.md" <<EOF
# av-gen-test-assets (PRIVATE)

Test assets for NateFaulkenberry/av-gen's CI. **Private. Not for distribution.** Some of these files
are purchased, commercially licensed models: never make this repository public, never copy them into
av-gen, an Actions cache or an artifact.

Made by \`tools/ci/make-test-assets-repo.sh\` from \`tools/ci/test-assets.list\`; read by
\`tools/fetch-test-assets.sh\`, which checks out the commit pinned in av-gen's
\`tools/ci/test-assets.lock\` and verifies \`SHA256SUMS\`.

${#files[@]} files, $((total / 1048576)) MB.
EOF

# The recommended home of the self-hosted GPU runner is this private repository; its workflow.
mkdir -p "$out/.github/workflows"
cp "$root/tools/ci/private-repo-gpu-workflow.yml" "$out/.github/workflows/gpu.yml"

[[ -d "$out/.git" ]] || git -C "$out" init -q -b main
git -C "$out" add -A
if git -C "$out" diff --cached --quiet; then
    echo "no change: $out already holds exactly this set"
else
    git -C "$out" commit -q -m "Test assets: ${#files[@]} files, $((total / 1048576)) MB (from av-gen $(git -C "$root" rev-parse --short HEAD))"
fi
sha=$(git -C "$out" rev-parse HEAD)
echo
echo "Built $out: ${#files[@]} files, $((total / 1048576)) MB, commit $sha"
echo "Nothing has been pushed. The next steps are yours (docs/development/gpu-ci-private-assets.md):"
echo "  gh repo create NateFaulkenberry/av-gen-test-assets --private --source '$out' --remote origin --push"
echo "  # later updates: git -C '$out' push origin main"
echo "  # then pin it: set 'commit $sha' in tools/ci/test-assets.lock and commit that"
