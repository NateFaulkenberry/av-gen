#!/usr/bin/env bash
# Give a git worktree the assets it cannot get from git.
#
# `assets/` is half tracked and half not: the manifests and attribution files are in the repository,
# and the things they point at -- 1782 of the 1842 files, every .glb, .wav, .hdr and texture -- are
# gitignored because they are large and unchanging. So `git worktree add` produces a checkout whose
# manifests all resolve to nothing, and the tests that load them fail in ways that look like code
# faults and are not: "audio file not found", an empty squad, a missing environment map.
#
# This symlinks every asset file the worktree is missing back to the primary checkout. Symlinks
# rather than copies because these are gigabytes and read-only in practice, and per-file rather than
# one link over `assets/` because the directory itself contains tracked files that must stay real.
#
# Usage: tools/link-worktree-assets.sh [worktree-path ...]   (default: every worktree but the main one)
set -euo pipefail

primary="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$primary/assets"
[[ -d "$src" ]] || { echo "no assets/ at $src" >&2; exit 1; }

targets=("$@")
if [[ ${#targets[@]} -eq 0 ]]; then
    while read -r path; do
        [[ "$path" == "$primary" ]] && continue
        targets+=("$path")
    done < <(git -C "$primary" worktree list --porcelain | awk '/^worktree /{print $2}')
fi

for wt in "${targets[@]}"; do
    if [[ ! -d "$wt/assets" ]]; then
        echo "skip $wt (no assets/ -- not a checkout of this repository?)" >&2
        continue
    fi
    linked=0
    while read -r rel; do
        dest="$wt/assets/$rel"
        # Only ever fills a gap. A file the worktree genuinely has of its own is left alone, so
        # running this twice is a no-op and it can never overwrite work.
        [[ -e "$dest" || -L "$dest" ]] && continue
        mkdir -p "$(dirname "$dest")"
        ln -s "$src/$rel" "$dest"
        linked=$((linked + 1))
    done < <(cd "$src" && find . -type f -not -type l | sed 's|^\./||')
    echo "$wt: linked $linked asset file(s)"
done
