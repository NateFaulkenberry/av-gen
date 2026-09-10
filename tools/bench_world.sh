#!/bin/sh
# The deterministic world benchmark. Same scene, same camera, same seed, same frame count; only the
# one variable under test changes.
#
# WARNING (ADR-051): the ms/frame column here is the whole process divided by the frame count, so it
# contains the scene build. That is fine for variants that load the same scene and differ in what
# they render; it is badly wrong for variants that load different amounts of ecology, because
# eleven scatter layers take two seconds longer to load than none and this charges all of it to the
# frames. That is where "1.5 ms per scatter layer" came from, and it was not a frame cost at all.
# Use tools/bench_ab.sh, which reads the per-frame median avgen reports for itself.
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
  # The renderer's own per-frame median, not the process wall clock divided by the frame count.
  # That division charges scene build to every frame -- eleven scatter layers take two seconds
  # longer to load than none, which over a hundred frames is twenty milliseconds a frame of
  # one-time work, linear in layer count and utterly convincing. It is what produced this
  # project's "1.5 ms per scatter layer" and the optimisation spec written against it.
  MS=$($BIN --headless --composition "$S" --frames "$FRAMES" --fps 30 --size "$SIZE" \
        --capture "$OUT/$v.png" 2>&1 | sed -nE 's/.*median ([0-9.]+) ms.*/\1/p' | tail -1)
  [ -n "$MS" ] || MS="n/a"
  VOL=$(echo "$P" | sed -nE 's/.*volume=([0-9.-]+).*/\1/p')
  SHA=$(echo "$P" | sed -nE 's/.*shadow=([0-9.-]+).*/\1/p')
  AO=$(echo "$P" | sed -nE 's/.*ao=([0-9.-]+).*/\1/p')
  DR=$(echo "$P" | sed -nE 's/.*draws=([0-9]+).*/\1/p')
  IN=$(echo "$P" | sed -nE 's/.*instances=([0-9]+\/[0-9]+).*/\1/p')
  printf "%-10s %10s %10s %10s %10s %9s %9s\n" "$v" "$MS" "$VOL" "$SHA" "$AO" "$DR" "$IN"
done
rm -rf "$OUT"
