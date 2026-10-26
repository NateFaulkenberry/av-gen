#!/bin/bash
# Talking Heads fx2 (17-brief-signal-corruption.md): the signal-corruption pass's review set, into
# ~/Desktop/av-gen-review/92-talking-heads/fx2/:
#   00 before/after (fx1's reviewed render beside fx2, the same seconds); each profile alone, quiet and loud; all
#   together; one and four busts; the neutral-colour, no-rays comparison; a minute of the full scene on Feline stems
#   (the main review view); face close-ups at the profiles' peaks (eyes and mouth bending with the face); the checks
#   (lifecycle, isolation, rays, seams incl. the jaw and under-chin) and the 1080p numbers at one to four busts.
# Every render goes through tools/gpu-lock.sh (review.py), one job per hold.
#   tools/talking_heads/fx2.sh <work dir> [section ...]    sections: baseline profiles together layouts neutral
#                                                          full close checks bench small (default: all)
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
W="${1:?a work directory}"
shift
SECTIONS="${*:-baseline profiles together layouts neutral full close checks bench small}"
OUT="$HOME/Desktop/av-gen-review/92-talking-heads/fx2"
FX1="$HOME/Desktop/av-gen-review/92-talking-heads/fx1"
mkdir -p "$OUT/profiles" "$OUT/layouts" "$OUT/neutral-no-rays" "$OUT/close" "$OUT/checks"
python3 "$REPO/tools/talking_heads/build_variant.py" --out "$W/colour" > /dev/null
python3 "$REPO/tools/talking_heads/build_variant.py" --out "$W/neutral" --neutral > /dev/null
C="$W/colour/talking-heads.json"
N="$W/neutral/talking-heads.json"
R() { python3 "$REPO/tools/talking_heads/review.py" "$@" > "$W/last-render.log" 2>&1 || { echo "render failed: $*"; tail -5 "$W/last-render.log"; }; }
has() { [[ " $SECTIONS " == *" $1 "* ]]; }
# the profiles by their default source; quiet = the stem's own intro or fade-in (Feline's drum and bass stems are
# level all song, so quiet is where they start), loud = 75-87 s
declare -a PROF=("drums:tracking:0:12" "bass:codec:5:17" "synth:broadcast:20:32" "other:ghosting:0:12")

if has baseline; then
  # fx1's reviewed three-together clip (56-71 s) on the LEFT, fx2's on the right: the same seconds and stems
  R clip --project "$C" --range 56:71 --stems --out "$W/after-56-71.mp4"
  ffmpeg -v error -y -i "$FX1/03-three-together.mp4" -i "$W/after-56-71.mp4" \
    -filter_complex "[0]scale=960:540[a];[1]scale=960:540[b];[a][b]hstack" \
    -c:v libx264 -crf 20 -pix_fmt yuv420p -map 1:a? -c:a aac "$OUT/00-before-after-three-together.mp4"
  for t in 2 6 10; do
    ffmpeg -v error -y -ss $t -i "$OUT/00-before-after-three-together.mp4" -frames:v 1 "$OUT/00-before-after-$((56 + t))s.png"
  done
fi

if has profiles; then
  # each profile alone (its source the only one: the one-bust layout), in colour, quiet then loud
  for p in "${PROF[@]}"; do
    IFS=: read -r src name q0 q1 <<< "$p"
    R clip --project "$C" --range $q0:$q1 --sources $src --out "$OUT/profiles/$name-$src-quiet.mp4"
    R clip --project "$C" --range 75:87 --sources $src --out "$OUT/profiles/$name-$src-loud.mp4"
  done
fi

if has together; then
  R clip --project "$C" --range 75:90 --stems --out "$OUT/01-all-together-three-stems.mp4"
  R clip --project "$C" --range 75:90 --sources drums,bass,synth,other --out "$OUT/02-all-together-four-sources.mp4"
fi

if has layouts; then
  R still --project "$C" --at 82 --sources synth --out "$OUT/layouts/one-bust.png"
  R still --project "$C" --at 82 --sources bass,synth --out "$OUT/layouts/two-busts.png"
  R still --project "$C" --at 82 --stems --out "$OUT/layouts/three-busts.png"
  R still --project "$C" --at 82 --sources drums,bass,synth,other --out "$OUT/layouts/four-busts.png"
  R clip --project "$C" --range 76:88 --sources bass --out "$OUT/03-one-bust.mp4"
  R clip --project "$C" --range 76:88 --sources drums,bass,synth,other --out "$OUT/04-four-busts.mp4"
  R clip --project "$C" --range 60:110 --sources drums,bass,synth,other --spans "bass:70:,synth:80:100,other:90:105" \
    --out "$OUT/05-sources-arrive-and-leave.mp4"
fi

if has neutral; then
  # every slot grey, the rays dark: the profiles must still be told apart by their corruption alone
  for p in "${PROF[@]}"; do
    IFS=: read -r src name q0 q1 <<< "$p"
    R clip --project "$N" --range 75:87 --sources $src --no-beams --out "$OUT/neutral-no-rays/$name-$src.mp4"
  done
  ffmpeg -v error -y -i "$OUT/neutral-no-rays/tracking-drums.mp4" -i "$OUT/neutral-no-rays/codec-bass.mp4" \
    -i "$OUT/neutral-no-rays/broadcast-synth.mp4" -i "$OUT/neutral-no-rays/ghosting-other.mp4" \
    -filter_complex "[0]scale=960:540[a];[1]scale=960:540[b];[2]scale=960:540[c];[3]scale=960:540[d];[a][b][c][d]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0" \
    -c:v libx264 -crf 20 -pix_fmt yuv420p "$OUT/06-neutral-no-rays-four-profiles-side-by-side.mp4"
  R clip --project "$N" --range 75:90 --sources drums,bass,synth,other --no-beams --out "$OUT/07-neutral-no-rays-four-together.mp4"
fi

if has full; then
  # the main review view: a minute of the full scene on Feline's three stems
  R clip --project "$C" --range 50:110 --stems --out "$OUT/08-full-scene-one-minute-feline-stems.mp4"
fi

if has close; then
  # each profile alone, a static three-quarter camera close on the face, through its loud passage: the eyes, lids and
  # mouth stay on the face whatever the corruption does to it
  for p in "${PROF[@]}"; do
    IFS=: read -r src name q0 q1 <<< "$p"
    R clip --project "$N" --range 78:84 --sources $src --no-beams --cam=-0.36,0.47,0.98,0.0,0.40,0.34 \
      --out "$OUT/close/$name-$src-face.mp4"
    ffmpeg -v error -y -i "$OUT/close/$name-$src-face.mp4" -vf "fps=2,scale=480:270,tile=4x3" -frames:v 1 \
      "$OUT/close/$name-$src-face-sheet.png"
  done
fi

if has checks; then
  python3 "$REPO/tools/talking_heads/lifecycle_check.py" --project "$C" > "$OUT/checks/lifecycle.txt" 2>&1; echo "lifecycle exit $?" >> "$OUT/checks/lifecycle.txt"
  python3 "$REPO/tools/talking_heads/isolation_check.py" --project "$C" --mute all > "$OUT/checks/isolation.txt" 2>&1; echo "isolation exit $?" >> "$OUT/checks/isolation.txt"
  python3 "$REPO/tools/talking_heads/ray_check.py" --project "$C" --stems > "$OUT/checks/rays.txt" 2>&1; echo "rays exit $?" >> "$OUT/checks/rays.txt"
  : > "$OUT/checks/seam.txt"
  for P in th1 th2 th3 th4; do
    echo "== $P" >> "$OUT/checks/seam.txt"
    python3 "$REPO/tools/talking_heads/seam_check.py" --project "$C" --prefix $P --out "$OUT/checks/seam" >> "$OUT/checks/seam.txt" 2>&1
    echo "$P exit $?" >> "$OUT/checks/seam.txt"
  done
fi

if has bench; then
  # 1080p numbers at one to four busts, three runs each
  for rep in 1 2 3; do
    for v in "1:bass" "2:bass,synth" "3:drums,bass,synth" "4:drums,bass,synth,other"; do
      R bench --project "$C" --range 56: --frames 360 --sources "${v#*:}" --out "$OUT/checks/bench-${v%%:*}-busts.$rep.json"
    done
  done
fi

if has small; then
  for f in "$OUT"/*.mp4 "$OUT"/*/*.mp4; do
    case "$f" in *-small.mp4) continue ;; esac
    if [ "$(stat -f %z "$f")" -gt 30000000 ]; then
      ffmpeg -v error -y -i "$f" -c:v libx264 -crf 28 -preset slow -c:a aac -b:a 128k "${f%.mp4}-small.mp4"
    fi
  done
fi
echo done
