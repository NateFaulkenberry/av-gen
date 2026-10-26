#!/bin/bash
# Talking Heads fx1 (14-brief-secondary-fx.md section 10): the four profiles with the rays hidden and the colours
# neutral, all three together, the fourth source, one to four busts, sources arriving and leaving, quiet and loud
# passages, a minute of the full scene on Feline stems, the checks and the 1080p numbers at one to four busts, into
# ~/Desktop/av-gen-review/92-talking-heads/fx1/. Every render goes through tools/gpu-lock.sh, one job per hold.
#   tools/talking_heads/fx1.sh <work dir>
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
W="${1:?a work directory}"
OUT="$HOME/Desktop/av-gen-review/92-talking-heads/fx1"
mkdir -p "$OUT/profiles-neutral-no-rays" "$OUT/profiles-colour" "$OUT/layouts" "$OUT/checks"
python3 "$REPO/tools/talking_heads/build_variant.py" --out "$W/colour" > /dev/null
python3 "$REPO/tools/talking_heads/build_variant.py" --out "$W/neutral" --neutral > /dev/null
C="$W/colour/talking-heads.json"
N="$W/neutral/talking-heads.json"
R() { python3 "$REPO/tools/talking_heads/review.py" "$@"; }
# 1. each profile alone (its source the only one, so it is the hero), neutral palette, rays dark; then in colour
for src in drums bass synth other; do
  R clip --project "$N" --range 75:87 --sources $src --no-beams --out "$OUT/profiles-neutral-no-rays/$src.mp4"
  R clip --project "$C" --range 75:87 --sources $src --out "$OUT/profiles-colour/$src.mp4"
done
# ... and side by side, the four profiles in one frame (neutral, no rays)
ffmpeg -v error -y -i "$OUT/profiles-neutral-no-rays/drums.mp4" -i "$OUT/profiles-neutral-no-rays/bass.mp4" \
  -i "$OUT/profiles-neutral-no-rays/synth.mp4" -i "$OUT/profiles-neutral-no-rays/other.mp4" \
  -filter_complex "[0]scale=960:540[a];[1]scale=960:540[b];[2]scale=960:540[c];[3]scale=960:540[d];[a][b][c][d]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0" \
  -c:v libx264 -crf 20 -pix_fmt yuv420p "$OUT/01-profiles-side-by-side-neutral-no-rays.mp4"
# 2. all three together: neutral with the rays dark, then the normal presentation
R clip --project "$N" --range 56:71 --stems --no-beams --out "$OUT/02-three-together-neutral-no-rays.mp4"
R clip --project "$C" --range 56:71 --stems --out "$OUT/03-three-together.mp4"
# 3. the fourth source (the mix as an `other` source) with the three stems: four busts
R clip --project "$C" --range 75:90 --sources drums,bass,synth,other --out "$OUT/04-four-busts-fourth-source.mp4"
# 4. layouts: one bust (the synth alone), two, three, four; and none (the hero on the mix)
R still --project "$C" --at 82 --sources synth --out "$OUT/layouts/one-bust.png"
R still --project "$C" --at 82 --sources bass,synth --out "$OUT/layouts/two-busts.png"
R still --project "$C" --at 82 --stems --out "$OUT/layouts/three-busts.png"
R still --project "$C" --at 82 --sources drums,bass,synth,other --out "$OUT/layouts/four-busts.png"
R still --project "$C" --at 82 --out "$OUT/layouts/no-sources-hero-on-the-mix.png"
R clip --project "$C" --range 76:86 --sources synth --out "$OUT/05-one-bust.mp4"
# 5. sources arriving and leaving: drums throughout, bass from 70 s, synth 80-100 s, the mix (as other) 90-105 s
R clip --project "$C" --range 60:110 --sources drums,bass,synth,other --spans "bass:70:,synth:80:100,other:90:105" \
  --out "$OUT/06-sources-arrive-and-leave.mp4"
# 6. quiet and loud passages (three stems)
R clip --project "$C" --range 15:30 --stems --out "$OUT/07-quiet-passage.mp4"
R clip --project "$C" --range 75:90 --stems --out "$OUT/08-loud-passage.mp4"
# 7. a minute of the full scene on Feline stems: the final view
R clip --project "$C" --range 50:110 --stems --out "$OUT/09-full-scene-one-minute-feline-stems.mp4"
# checks
python3 "$REPO/tools/talking_heads/lifecycle_check.py" --project "$C" > "$OUT/checks/lifecycle.txt" 2>&1 || true
python3 "$REPO/tools/talking_heads/isolation_check.py" --project "$C" > "$OUT/checks/isolation.txt" 2>&1 || true
python3 "$REPO/tools/talking_heads/ray_check.py" --project "$C" --stems > "$OUT/checks/rays.txt" 2>&1 || true
python3 "$REPO/tools/talking_heads/seam_check.py" --project "$C" --out "$OUT/checks/seam" > "$OUT/checks/seam.txt" 2>&1 || true
# 1080p numbers at one to four busts (and with no sources), three runs each
for rep in 1 2 3; do
  for v in "1:synth" "2:bass,synth" "3:drums,bass,synth" "4:drums,bass,synth,other"; do
    R bench --project "$C" --range 56: --frames 360 --sources "${v#*:}" --out "$OUT/checks/bench-${v%%:*}-busts.$rep.json"
  done
done
for f in "$OUT"/*.mp4 "$OUT"/*/*.mp4; do
  if [ "$(stat -f %z "$f")" -gt 30000000 ]; then
    ffmpeg -v error -y -i "$f" -c:v libx264 -crf 28 -preset slow -an "${f%.mp4}-small.mp4"
  fi
done
echo "$OUT"
