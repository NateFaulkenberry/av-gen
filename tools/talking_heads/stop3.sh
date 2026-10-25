#!/bin/bash
# Talking Heads stop 3 (the multi-head scene, 07-owner-notes-stop2.md): clips, checks and the headless 1080p numbers,
# into ~/Desktop/av-gen-review/talking-heads/stop3/. Every render goes through tools/gpu-lock.sh, one job per hold.
#   tools/talking_heads/stop3.sh [project.json]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
P="${1:-$REPO/examples/sonic-abstract/talking-heads.json}"
OUT="$HOME/Desktop/av-gen-review/talking-heads/stop3"
mkdir -p "$OUT"
R() { python3 "$REPO/tools/talking_heads/review.py" "$@" --project "$P"; }
EYES="-0.10,0.44,0.62,0.0,0.43,0.18"     # close on the middle head's face
R clip --range 56:68 --stems --close --only beams --out "$OUT/01-softened-jaw-rays-follow-the-mouth-close.mp4"
R clip --range 56:76 --stems --out "$OUT/02-three-heads-feline-stems.mp4"
R clip --range 30:50 --stems --shut --cam="$EYES" --out "$OUT/03-darting-eyes-close-gate-shut.mp4"
R clip --range 30:55 --stems --shut --out "$OUT/04-scanning-gate-shut.mp4"
R clip --range 80:100 --stems --out "$OUT/05-scanning-with-music-feline-stems.mp4"
R clip --range 60:80 --wav night --out "$OUT/06-night-shift-mix.mp4"
R clip --range 48:63 --wav trench --out "$OUT/07-trench-mix.mp4"
python3 "$REPO/tools/talking_heads/anchor_check.py" --project "$P" --out "$OUT/beam-origins" --poses 0:0,24:10 \
    --prefix th2 --view three-quarter || true
{ echo "== Feline Footwear, stems"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P" --stems
  echo "== Feline Footwear, the mix"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P"
  echo "== Night Shift, the mix"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P" --wav night
  echo "== Trench, the mix"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P" --wav trench
} > "$OUT/checks-rays-gates-scans.txt" 2>&1
for rep in 1 2 3; do
  R bench --range 56: --frames 360 --stems --out "$OUT/bench-feline-stems.$rep.json"
  R bench --range 60: --frames 360 --wav night --out "$OUT/bench-night-shift.$rep.json"
done
# small copies of anything over 30 MB
for f in "$OUT"/*.mp4; do
  if [ "$(stat -f %z "$f")" -gt 30000000 ]; then
    ffmpeg -v error -y -i "$f" -c:v libx264 -crf 28 -preset slow -an "${f%.mp4}-small.mp4"
  fi
done
echo "$OUT"
