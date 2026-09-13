#!/bin/bash
# Creates an isolated worktree for a parallel agent, with the gitignored assets linked in.
#
#   tools/new-worktree.sh geo agent/geo
#
# A fresh worktree is missing ~950 MB of gitignored assets and the failures look like code bugs:
# `sequence.get_state` reports hasAudio:false, Glowmere's directed shot has no environment map, and
# neither failure names an asset. `assets/audio` and `assets/hdri` already exist in a worktree
# because their manifest.json is tracked, so the *files* inside are linked, not the directories.
set -euo pipefail

NAME="$1"
BRANCH="$2"
MAIN="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$MAIN/../av-gen-wt-$NAME"

git -C "$MAIN" worktree add -b "$BRANCH" "$DEST" main

for d in kenney quaternius nature terrain imported; do
    [ -e "$MAIN/assets/$d" ] && [ ! -e "$DEST/assets/$d" ] && ln -s "$MAIN/assets/$d" "$DEST/assets/$d"
done
for dir in audio hdri; do
    mkdir -p "$DEST/assets/$dir"
    for f in "$MAIN/assets/$dir"/*; do
        b="$(basename "$f")"
        [ "$b" = "manifest.json" ] && continue
        [ ! -e "$DEST/assets/$dir/$b" ] && ln -s "$f" "$DEST/assets/$dir/$b"
    done
done

# So `git add -A` in the worktree never commits a symlink.
EX="$(git -C "$DEST" rev-parse --git-path info/exclude)"
printf 'assets/kenney\nassets/quaternius\nassets/nature\nassets/terrain\nassets/imported\nassets/audio/*\nassets/hdri/*\n' >> "$EX"

echo "worktree: $DEST on $BRANCH"
echo "configure: cmake --preset release -S $DEST -B $DEST/build/release -DCPM_SOURCE_CACHE=$MAIN/.cache/cpm"
