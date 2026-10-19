#!/bin/bash
# E4/E5: profile each variant headless at 1080p, Ultra (fixed level, so the costs compare), no audio.
#   examples/rainbow-world/experiments/run_e45.sh <e45 dir> [variants...]
# Each run takes the GPU lock on its own (a few tens of seconds), so other agents interleave.
set -u
DIR=$(cd "$1" && pwd)
shift
VARIANTS=${*:-"base cloudcam cloud1 cloud4 grass50k grass150k grass300k"}
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$DIR/profiles"
for v in $VARIANTS; do
  AVGEN_GPU_LOCK_TIMEOUT=14400 "$ROOT/tools/gpu-lock.sh" "$ROOT/build/release/src/avgen" --live-profile --mode headless \
    --target-fps 30 --size 1920x1080 --quality ultra --no-audio --warmup 3 --measure 6 \
    --project "$DIR/$v.json" --json "$DIR/profiles/$v.json" --capture "$DIR/profiles/$v.png" \
    > "$DIR/profiles/$v.txt" 2>&1
  echo "$v exit $?"
done
