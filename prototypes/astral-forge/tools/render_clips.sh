#!/bin/bash
# Renders the six tests at 1080p30 and encodes them (TEST 06 with its audio). Usage: render_clips.sh [tests...]
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${OUT:-$HOME/Desktop/av-gen-review/37-astral-forge/clips}
X=$ROOT/build/release/prototypes/astral-forge/avgen_astral_forge
SONG=${SONG:-$ROOT/assets/audio/trench.wav}
T6START=${T6START:-66.54}
mkdir -p "$OUT"
for t in "${@:-1 2 3 4 5 6}"; do
  d="$OUT/frames-t$t"; rm -rf "$d"
  extra=""; [ "$t" = 6 ] && extra="--song $SONG --song-start $T6START"
  "$ROOT/tools/gpu-lock.sh" "$X" --test $t --clip "$d" --fps 30 $extra 2>&1 | grep -E "rror| t=" ; rc=${PIPESTATUS[0]}
  echo "test $t render rc=$rc"
  if [ "$t" = 6 ]; then
    ffmpeg -y -v error -framerate 30 -i "$d/f%05d.png" -ss $T6START -i "$SONG" -map 0:v -map 1:a -shortest -c:v libx264 -pix_fmt yuv420p -crf 17 -c:a aac -b:a 256k "$OUT/t06-audio-$(basename "$SONG" | sed 's/\.[^.]*$//' | tr 'A-Z ' 'a-z-').mp4"
  else
    ffmpeg -y -v error -framerate 30 -i "$d/f%05d.png" -c:v libx264 -pix_fmt yuv420p -crf 17 "$OUT/t0$t.mp4"
  fi
  echo "test $t encoded rc=$?"
done
