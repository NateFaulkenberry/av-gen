#!/usr/bin/env bash
# §15/§16 experiment: what does the editor's workspace canvas cost as a function of its pixels?
#
# Runs the LIVE editor (the configuration the complaint is about, not a headless bench) at one
# window size and several `--canvas-scale` values, and reports the scene-pass GPU time and the
# frame wall clock at each. The point is the exponent: if the scene pass were fully
# resolution-bound the GPU time would fall as the pixel count does, and ADR-137 records a prior
# that it falls only 44% as fast. Nothing here is a fix; it is the measurement a fix has to beat.
#
#   tools/canvas_scale_sweep.sh <project> <WxH> <frames> <reps> <outdir> [preview-mode]
set -uo pipefail
PROJ="${1:?project}"; SIZE="${2:-2560x1600}"; FRAMES="${3:-400}"; REPS="${4:-3}"
OUT="${5:?outdir}"; MODE="${6:-workspace}"
mkdir -p "$OUT"
SCALES="1.0 0.85 0.71 0.58 0.5"
# Interleaved (ADR-051/ADR-181): every scale runs once per round and the rounds repeat, so a
# machine that drifts during the session charges the drift to all the arms rather than to one.
for r in $(seq 1 "$REPS"); do
  for s in $SCALES; do
    ./tools/gpu-lock.sh ./build/release/src/avgen --project "$PROJ" --play --frames "$FRAMES" \
        --size "$SIZE" --preview-mode "$MODE" --canvas-scale "$s" --profile-cpu \
        > "$OUT/s${s}-r${r}.log" 2>&1
    echo "scale $s rep $r -> $OUT/s${s}-r${r}.log"
  done
done
