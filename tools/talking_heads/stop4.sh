#!/bin/bash
# Talking Heads stop 4 (the hinged mouth, lids, palettes, per-source effects, neck swivel, atmosphere): stills, clips,
# checks and the headless 1080p numbers, into ~/Desktop/av-gen-review/talking-heads/stop4/. Every render goes through
# tools/gpu-lock.sh, one job per hold.
#   tools/talking_heads/stop4.sh [project.json]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
P="${1:-$REPO/examples/sonic-abstract/talking-heads.json}"
OUT="$HOME/Desktop/av-gen-review/talking-heads/stop4"
mkdir -p "$OUT/mouth" "$OUT/checks"
R() { python3 "$REPO/tools/talking_heads/review.py" "$@" --project "$P"; }
PS() { python3 "$REPO/tools/talking_heads/pose_stills.py" --project "$P" "$@"; }
# the mouth: shut, half and fully open; front and three-quarter; upright and tilted 32 degrees (rays off: a close
# camera sits inside their cones), then fully open with its rays
PS --out "$OUT/mouth" --poses 0:0,0:0.5,0:1,32:0,32:0.5,32:1 --views front,three-quarter --no-rays
PS --out "$OUT/mouth" --poses 0:1,32:1 --views three-quarter --tag mouth-with-rays
# the eyes and lids: a blink's half and shut, close
PS --out "$OUT/mouth" --poses 0:0 --views front --no-rays --lids 0.5 --dist 0.32 --tag lids-half
PS --out "$OUT/mouth" --poses 0:0 --views front --no-rays --lids 1 --dist 0.32 --tag lids-shut
MOUTH3Q="-0.29,0.38,0.75,0.0,0.35,0.28"   # three-quarter on the middle head's mouth
EYES="-0.07,0.42,0.62,0.0,0.40,0.25"      # close on the middle head's eyes
R clip --range 30:45 --stems --slow-mouth th2 --cam="$MOUTH3Q" --out "$OUT/01-mouth-slow-open-close-rays.mp4"
R clip --range 56:76 --stems --out "$OUT/02-three-heads-feline-stems.mp4"
R clip --range 30:45 --stems --shut --cam="$EYES" --out "$OUT/03-eyes-blinks-close-gate-shut.mp4"
R clip --range 30:55 --stems --shut --out "$OUT/04-looking-around-gate-shut.mp4"
R clip --range 80:100 --stems --out "$OUT/05-looking-around-with-music.mp4"
R clip --range 100:120 --stems --shut --out "$OUT/06-atmosphere-gate-shut.mp4"
R clip --range 120:140 --stems --out "$OUT/07-atmosphere-with-music.mp4"
R clip --range 48:63 --wav trench --out "$OUT/08-trench-mix.mp4"
python3 "$REPO/tools/talking_heads/anchor_check.py" --project "$P" --out "$OUT/checks/beam-origins" \
    --poses 0:0,24:11.5,32:11.5 --prefix th2 --view three-quarter > "$OUT/checks/anchor-check.txt" 2>&1 || true
{ echo "== Feline Footwear, stems"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P" --stems
  echo "== Feline Footwear, the mix"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P"
  echo "== Night Shift, the mix"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P" --wav night
  echo "== Trench, the mix"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P" --wav trench
} > "$OUT/checks/rays-gates-looks.txt" 2>&1 || true
python3 "$REPO/tools/talking_heads/isolation_check.py" --project "$P" > "$OUT/checks/isolation.txt" 2>&1 || true
for rep in 1 2 3; do
  R bench --range 56: --frames 360 --stems --out "$OUT/checks/bench-feline-stems.$rep.json"
  R bench --range 56: --frames 360 --stems --no-atmos --out "$OUT/checks/bench-feline-stems-no-atmosphere.$rep.json"
  R bench --range 60: --frames 360 --wav night --out "$OUT/checks/bench-night-shift.$rep.json"
done
# small copies of anything over 30 MB
for f in "$OUT"/*.mp4; do
  if [ "$(stat -f %z "$f")" -gt 30000000 ]; then
    ffmpeg -v error -y -i "$f" -c:v libx264 -crf 28 -preset slow -an "${f%.mp4}-small.mp4"
  fi
done
echo "$OUT"
