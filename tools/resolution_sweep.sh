#!/usr/bin/env bash
# §15/§16: what does one megapixel of the editor's canvas actually cost?
#
# ADR-137 records the prior that this renderer's scene pass is only "44% resolution-dependent"
# (640x400 is 0.26x the pixels of 1280x800 and 0.66x the scene time) and defers dynamic resolution
# behind it. That measurement was taken on a different world at a fraction of an editor canvas's
# size, and a dynamic-resolution controller has to be sized on the exponent that holds *here*.
#
# This runs headless at one aspect ratio and several linear scales, one process per point, and
# leaves the per-pass GPU medians in a --bench-json record for each. Headless, because the per-pass
# timeline medians are only reported on that path -- the live editor reports the GPU frame as a
# whole. The live arm is tools/canvas_scale_sweep.sh and answers a different question.
#
#   tools/resolution_sweep.sh <project> <baseWxH> <frames> <reps> <outdir>
set -uo pipefail
PROJ="${1:?project}"; BASE="${2:-2068x1326}"; FRAMES="${3:-200}"; REPS="${4:-3}"; OUT="${5:?outdir}"
BW=${BASE%x*}; BH=${BASE#*x}
mkdir -p "$OUT"
SCALES="1.00 0.85 0.71 0.58 0.50 0.35"
# Interleaved over rounds (ADR-051/ADR-181), so drift is charged to every point rather than to the
# ones that happened to run late.
for r in $(seq 1 "$REPS"); do
  for s in $SCALES; do
    W=$(python3 -c "print(max(16,int(round($BW*$s))//2*2))")
    H=$(python3 -c "print(max(16,int(round($BH*$s))//2*2))")
    ./tools/gpu-lock.sh ./build/release/src/avgen --project "$PROJ" --headless \
        --frames "$FRAMES" --size "${W}x${H}" --bench-json "$OUT/s${s}-r${r}.json" \
        > "$OUT/s${s}-r${r}.log" 2>&1
    echo "scale $s (${W}x${H}) rep $r -> $OUT/s${s}-r${r}.json"
  done
done
