#!/bin/bash
# Talking Heads stop 1: the spec's section-10 clips and the headless 1080p numbers, into
# ~/Desktop/av-gen-review/92-talking-heads/stop1/. Every render goes through tools/gpu-lock.sh (review.py), one job per hold.
#   tools/talking_heads/stop1.sh [project.json]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
P="${1:-$REPO/examples/sonic-abstract/talking-heads.json}"
OUT="$HOME/Desktop/av-gen-review/92-talking-heads/stop1"
mkdir -p "$OUT"
R() { python3 "$REPO/tools/talking_heads/review.py" "$@" --project "$P"; }
R clip --wav none --range 0:12 --out "$OUT/01-dormant.mp4"
R clip --range 68:80 --only mouth --out "$OUT/02-mouth-emission.mp4"
R clip --range 120:132 --only eyes --out "$OUT/03-eye-emission.mp4"
R clip --range 72:84 --only vibration --stems --out "$OUT/04-spectral-vibration.mp4"
R clip --range 100:112 --only chroma --out "$OUT/05-chromatic-possession.mp4"
R clip --range 128:140 --only tear --stems --out "$OUT/06-signal-tearing.mp4"
R clip --range 60:80 --stems --out "$OUT/07-combined-feline-stems.mp4"
R clip --range 48:63 --wav trench --out "$OUT/07b-combined-trench-mix.mp4"
R clip --range 190:210 --out "$OUT/08-quiet-recovery.mp4"
R clip --range 40:64 --only mouth,eyes --out "$OUT/09-head-tilts-back-while-talking.mp4"
for rep in 1 2 3; do
  R bench --range 60: --frames 360 --stems --out "$OUT/bench-feline-stems.$rep.json"
  R bench --range 48: --frames 360 --wav trench --out "$OUT/bench-trench.$rep.json"
done
echo "$OUT"
