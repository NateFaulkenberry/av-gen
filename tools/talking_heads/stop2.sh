#!/bin/bash
# Talking Heads stop 2 (the gated head, 05-owner-notes-stop1.md): clips, checks and the headless 1080p numbers, into
# ~/Desktop/av-gen-review/talking-heads/stop2/. Every render goes through tools/gpu-lock.sh, one job per hold.
#   tools/talking_heads/stop2.sh [project.json]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
P="${1:-$REPO/examples/sonic-abstract/talking-heads.json}"
OUT="$HOME/Desktop/av-gen-review/talking-heads/stop2"
mkdir -p "$OUT"
R() { python3 "$REPO/tools/talking_heads/review.py" "$@" --project "$P"; }
R clip --range 56:72 --stems --out "$OUT/01-gate-on-the-bass-stem-feline.mp4"
R clip --range 56:68 --stems --close --only beams --out "$OUT/02-jaw-and-neck-close-up.mp4"
R clip --wav none --range 0:30 --out "$OUT/03-scanning-the-room.mp4"
R clip --range 40:90 --stems --out "$OUT/04-full-feline-stems.mp4"
R clip --range 48:68 --wav trench --out "$OUT/05-trench-mix.mp4"
python3 "$REPO/tools/talking_heads/anchor_check.py" --project "$P" --out "$OUT/beam-origins" \
    --poses 0:0,0:14,24:14 --view three-quarter
python3 "$REPO/tools/talking_heads/anchor_check.py" --project "$P" --out "$OUT/beam-origins" \
    --poses 0:0,24:14 --view front
for rep in 1 2 3; do
  R bench --range 56: --frames 360 --stems --out "$OUT/bench-feline-stems.$rep.json"
  R bench --range 48: --frames 360 --wav trench --out "$OUT/bench-trench.$rep.json"
done
echo "$OUT"
