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

# Link the whole directory when the worktree has no such directory, and otherwise link the entries
# *inside* it that the worktree is missing. The second case is not hypothetical: `assets/kenney` has
# 31 tracked files, so git creates the directory in every worktree, `[ ! -e ]` is false, and the
# untracked subdirectories (the city kits) silently never arrive. Two `test_city` cases then fail in
# every worktree for missing assets and look exactly like a code regression -- they were reported as
# a pre-existing defect by one agent and as "not mine" by another before anyone noticed the cause
# was this script. It is the same shape as the audio/hdri case below, which was already handled.
for d in kenney quaternius nature terrain imported; do
    [ -e "$MAIN/assets/$d" ] || continue
    if [ ! -e "$DEST/assets/$d" ]; then
        ln -s "$MAIN/assets/$d" "$DEST/assets/$d"
        continue
    fi
    # Directory exists because something in it is tracked: link each missing child instead.
    for f in "$MAIN/assets/$d"/*; do
        [ -e "$f" ] || continue
        b="$(basename "$f")"
        [ ! -e "$DEST/assets/$d/$b" ] && ln -s "$f" "$DEST/assets/$d/$b"
    done
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
printf 'assets/kenney\nassets/kenney/*\nassets/quaternius\nassets/quaternius/*\nassets/nature\nassets/nature/*\nassets/terrain\nassets/terrain/*\nassets/imported\nassets/imported/*\nassets/audio/*\nassets/hdri/*\n' >> "$EX"

echo "worktree: $DEST on $BRANCH"
echo "configure: cmake --preset release -S $DEST -B $DEST/build/release -DCPM_SOURCE_CACHE=$MAIN/.cache/cpm"
