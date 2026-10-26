#!/bin/bash
# Talking Heads foil1 (the owner: "let's try applying D hybrid foil to the busts in talking heads with the music able
# to drive the colour bands"): the look test's review set, into ~/Desktop/av-gen-review/92-talking-heads/foil1/:
#   01 a minute of the full scene on Feline stems (the main view); 02 a close-up of one head, its bands moving to its
#   source; 03 porcelain vs foil stills from the same frames; checks: the seam check on every head, 1080p benches at
#   one to four busts for foil and porcelain (the same session). Every render goes through tools/gpu-lock.sh.
#   tools/talking_heads/foil1.sh <work dir> [section ...]   sections: full close compare checks bench small
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
W="${1:?a work directory}"
shift
SECTIONS="${*:-full close compare checks bench small}"
OUT="$HOME/Desktop/av-gen-review/92-talking-heads/foil1"
mkdir -p "$OUT/compare" "$OUT/checks"
python3 "$REPO/tools/talking_heads/build_variant.py" --out "$W/foil" > /dev/null
python3 "$REPO/tools/talking_heads/build_variant.py" --out "$W/porcelain" --porcelain > /dev/null
F="$W/foil/talking-heads.json"
P="$W/porcelain/talking-heads.json"
R() { python3 "$REPO/tools/talking_heads/review.py" "$@" > "$W/last-render.log" 2>&1 || { echo "render failed: $*"; tail -5 "$W/last-render.log"; }; }
has() { [[ " $SECTIONS " == *" $1 "* ]]; }
CLOSE="-0.34,0.50,1.05,0.0,0.42,0.30"   # three-quarter, close on the one-bust layout's head

if has full; then
  R clip --project "$F" --range 50:110 --stems --out "$OUT/01-full-scene-one-minute-feline-stems.mp4"
fi
if has close; then
  # the synth alone (broadcast: no geometry moves, so the bands are the motion), close on its head: the bands drift
  # with its level, sweep on its onsets, widen and shine while it speaks
  R clip --project "$F" --range 60:85 --sources synth --cam=$CLOSE --out "$OUT/02-close-synth-head-bands-follow-its-source.mp4"
  # the same seconds, the bass alone (codec: the blocks displace the engraving with the material)
  R clip --project "$F" --range 60:75 --sources bass --cam=$CLOSE --out "$OUT/02b-close-bass-head.mp4"
fi
if has compare; then
  for t in 62 82 96; do
    R still --project "$P" --at $t --stems --out "$W/porcelain-$t.png"
    R still --project "$F" --at $t --stems --out "$W/foil-$t.png"
    ffmpeg -v error -y -i "$W/porcelain-$t.png" -i "$W/foil-$t.png" -filter_complex "[0][1]hstack" \
      "$OUT/compare/porcelain-left-foil-right-${t}s.png"
    cp "$W/porcelain-$t.png" "$OUT/compare/porcelain-${t}s.png"
    cp "$W/foil-$t.png" "$OUT/compare/foil-${t}s.png"
  done
  R still --project "$P" --at 72 --sources synth --cam=$CLOSE --out "$W/porcelain-close-72.png"
  R still --project "$F" --at 72 --sources synth --cam=$CLOSE --out "$W/foil-close-72.png"
  ffmpeg -v error -y -i "$W/porcelain-close-72.png" -i "$W/foil-close-72.png" -filter_complex "[0][1]hstack" \
    "$OUT/compare/porcelain-left-foil-right-close-synth-72s.png"
fi
if has checks; then
  : > "$OUT/checks/seam.txt"
  for H in th1 th2 th3 th4; do
    echo "== $H" >> "$OUT/checks/seam.txt"
    python3 "$REPO/tools/talking_heads/seam_check.py" --project "$F" --prefix $H --out "$OUT/checks/seam" >> "$OUT/checks/seam.txt" 2>&1
    echo "$H exit $?" >> "$OUT/checks/seam.txt"
  done
  python3 "$REPO/tools/talking_heads/lifecycle_check.py" --project "$F" > "$OUT/checks/lifecycle.txt" 2>&1; echo "lifecycle exit $?" >> "$OUT/checks/lifecycle.txt"
  python3 "$REPO/tools/talking_heads/isolation_check.py" --project "$F" --mute all > "$OUT/checks/isolation.txt" 2>&1; echo "isolation exit $?" >> "$OUT/checks/isolation.txt"
fi
if has bench; then
  for rep in 1 2; do
    for v in "1:bass" "2:bass,synth" "3:drums,bass,synth" "4:drums,bass,synth,other"; do
      R bench --project "$F" --range 56: --frames 360 --sources "${v#*:}" --out "$OUT/checks/bench-foil-${v%%:*}-busts.$rep.json"
      R bench --project "$P" --range 56: --frames 360 --sources "${v#*:}" --out "$OUT/checks/bench-porcelain-${v%%:*}-busts.$rep.json"
    done
  done
fi
if has small; then
  for f in "$OUT"/*.mp4; do
    case "$f" in *-small.mp4) continue ;; esac
    if [ "$(stat -f %z "$f")" -gt 30000000 ]; then
      ffmpeg -v error -y -i "$f" -c:v libx264 -crf 28 -preset slow -c:a aac -b:a 128k "${f%.mp4}-small.mp4"
    fi
  done
fi
echo done
