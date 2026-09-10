#!/bin/sh
# The deterministic world benchmark. Same scene, same camera, same seed, same frame count; only the
# one variable under test changes. Wall clock per frame is the headline because it is the number
# that cannot be wrong about itself; the pass timers beside it say where it went.
#
#   tools/bench_world.sh [resolution] [frames] [variant ...]
set -e
SIZE=${1:-2880x1800}; FRAMES=${2:-120}; shift 2 2>/dev/null || true
VARIANTS=${*:-"full novol noeco noshadow nowater n1 n3 n6"}
BIN=./build/release/src/avgen
OUT=$(mktemp -d)
printf "%-10s %10s %10s %10s %10s %9s %9s\n" scene ms/frame volumeMs shadowMs aoMs draws instances
for v in $VARIANTS; do
  S=examples/world/_bench/$v.scene.json
  [ -f "$S" ] || { echo "missing $S"; exit 1; }
  P=$($BIN --headless --composition "$S" --frames "$FRAMES" --fps 30 --size "$SIZE" --capture "$OUT/$v.png" 2>&1 | grep -E "passes:" | tail -1)
  T=$( { /usr/bin/time -p $BIN --headless --composition "$S" --frames "$FRAMES" --fps 30 --size "$SIZE" --capture "$OUT/$v.png" >/dev/null; } 2>&1 | awk '/^real/{print $2}')
  MS=$(echo "$T $FRAMES" | awk '{printf "%.1f", $1*1000/$2}')
  VOL=$(echo "$P" | sed -nE 's/.*volume=([0-9.-]+).*/\1/p')
  SHA=$(echo "$P" | sed -nE 's/.*shadow=([0-9.-]+).*/\1/p')
  AO=$(echo "$P" | sed -nE 's/.*ao=([0-9.-]+).*/\1/p')
  DR=$(echo "$P" | sed -nE 's/.*draws=([0-9]+).*/\1/p')
  IN=$(echo "$P" | sed -nE 's/.*instances=([0-9]+\/[0-9]+).*/\1/p')
  printf "%-10s %10s %10s %10s %10s %9s %9s\n" "$v" "$MS" "$VOL" "$SHA" "$AO" "$DR" "$IN"
done
rm -rf "$OUT"
