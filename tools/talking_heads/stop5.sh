#!/bin/bash
# Talking Heads stop 5: the "oh" mouth, the seam, the larger staggered busts and the background (10-brief-background.md):
# stills, clips, the heads-hidden background, the seam close-ups, the checks and the 1080p numbers, into
# ~/Desktop/av-gen-review/talking-heads/stop5/. Every render goes through tools/gpu-lock.sh.
#   tools/talking_heads/stop5.sh [project.json]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
P="${1:-$REPO/examples/sonic-abstract/talking-heads.json}"
OUT="$HOME/Desktop/av-gen-review/talking-heads/stop5"
mkdir -p "$OUT/mouth" "$OUT/checks"
R() { python3 "$REPO/tools/talking_heads/review.py" "$@" --project "$P"; }
PS() { python3 "$REPO/tools/talking_heads/pose_stills.py" --project "$P" "$@"; }
PS --out "$OUT/mouth" --poses 0:0,0:0.5,0:1,32:0,32:0.5,32:1 --views front,three-quarter --no-rays
PS --out "$OUT/mouth" --poses 0:0.5,0:1,32:1 --views three-quarter --tag mouth-with-rays
MOUTH3Q="-0.29,0.38,0.75,0.0,0.35,0.28"
R clip --range 30:45 --stems --slow-mouth th2 --cam="$MOUTH3Q" --out "$OUT/01-oh-slow-open-close-rays.mp4"
R clip --range 56:76 --stems --out "$OUT/02-three-heads-feline-stems.mp4"
# the background (10-brief-background.md): a quiet passage, an energetic one, and with the heads hidden
R clip --range 15:30 --stems --out "$OUT/03-background-quiet-passage.mp4"
R clip --range 75:90 --stems --out "$OUT/04-background-energetic-passage.mp4"
R clip --range 120:140 --stems --no-heads --out "$OUT/05-background-heads-hidden.mp4"
R still --at 22 --stems --no-heads --out "$OUT/background-heads-hidden-quiet.png"
R still --at 80 --stems --no-heads --out "$OUT/background-heads-hidden-energetic.png"
# the seam (the owner: "eliminate this seam"): every effect at full strength, close on the neck, measured
python3 "$REPO/tools/talking_heads/seam_check.py" --project "$P" --out "$OUT/seam" > "$OUT/checks/seam-check.txt" 2>&1 || true
python3 "$REPO/tools/talking_heads/anchor_check.py" --project "$P" --out "$OUT/checks/beam-origins" \
    --poses 0:0,24:16,32:16 --prefix th2 --view three-quarter > "$OUT/checks/anchor-check.txt" 2>&1 || true
{ echo "== Feline Footwear, stems"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P" --stems
  echo "== Feline Footwear, the mix"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P"
} > "$OUT/checks/rays-gates-looks.txt" 2>&1 || true
python3 "$REPO/tools/talking_heads/isolation_check.py" --project "$P" > "$OUT/checks/isolation.txt" 2>&1 || true
# 1080p numbers: before (the stop-5 heads without the background) and after, three runs each
for rep in 1 2 3; do
  R bench --range 56: --frames 360 --stems --no-bg --out "$OUT/checks/bench-before-no-background.$rep.json"
  R bench --range 56: --frames 360 --stems --out "$OUT/checks/bench-after-with-background.$rep.json"
done
for f in "$OUT"/*.mp4; do
  if [ "$(stat -f %z "$f")" -gt 30000000 ]; then
    ffmpeg -v error -y -i "$f" -c:v libx264 -crf 28 -preset slow -an "${f%.mp4}-small.mp4"
  fi
done
echo "$OUT"
