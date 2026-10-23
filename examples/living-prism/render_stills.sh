#!/bin/bash
# Render 1080p stills of a Living Prism project at given seconds (the camera route is a pure function of time, so a
# still at t is the frame play shows at t). One avgen process per still; the caller holds the GPU lock round the batch.
#   examples/living-prism/render_stills.sh <project.json> <outdir> <t1> [t2 ...]
set -u
PROJ=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
OUT=$2
shift 2
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
for t in "$@"; do
  rm -rf "$OUT/_r"
  "$ROOT/build/release/src/avgen" --headless --project "$PROJ" --render "$OUT/_r" --size 1920x1080 \
    --range "$t:$(python3 -c "print($t+0.02)")" --log warn > "$OUT/t$t.log" 2>&1
  rc=$?
  cp "$OUT/_r/frame_000000.png" "$OUT/t$t.png" 2>/dev/null
  echo "t=$t exit $rc"
done
rm -rf "$OUT/_r"
