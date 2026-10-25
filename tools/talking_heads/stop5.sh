#!/bin/bash
# Talking Heads stop 5 (the owner on stop 4: "reshape fully open mouth to more of an 'oh' sound / look rather than a
# half smile"): the mouth stills, a slow open/close with rays, the three heads on the Feline stems, and the checks, into
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
python3 "$REPO/tools/talking_heads/anchor_check.py" --project "$P" --out "$OUT/checks/beam-origins" \
    --poses 0:0,24:16,32:16 --prefix th2 --view three-quarter > "$OUT/checks/anchor-check.txt" 2>&1 || true
{ echo "== Feline Footwear, stems"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P" --stems
  echo "== Feline Footwear, the mix"; python3 "$REPO/tools/talking_heads/ray_check.py" --project "$P"
} > "$OUT/checks/rays-gates-looks.txt" 2>&1 || true
for f in "$OUT"/*.mp4; do
  if [ "$(stat -f %z "$f")" -gt 30000000 ]; then
    ffmpeg -v error -y -i "$f" -c:v libx264 -crf 28 -preset slow -an "${f%.mp4}-small.mp4"
  fi
done
echo "$OUT"
