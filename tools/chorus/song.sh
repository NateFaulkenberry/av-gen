#!/bin/bash
# song.sh <look> <outdir> <clip a:b|-> <t>... : stills at each t (1280x720) and one clip (960x540, 30 fps), one GPU lock.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LOOK="$1"; OUT="$2"; CLIP="$3"; shift 3
mkdir -p "$OUT"
python3 "$ROOT/examples/chorus-field/build.py" "${LOOK//-/_}" > /dev/null || exit 1
AVGEN_GPU_LOCK_TIMEOUT=${AVGEN_GPU_LOCK_TIMEOUT:-14400} "$ROOT/tools/gpu-lock.sh" "$ROOT/tools/chorus/song_inner.sh" "$ROOT" "$LOOK" "$OUT" "$CLIP" "$@"
files=(); for t in "$@"; do [ -f "$OUT/$LOOK-t$t.png" ] && files+=("$OUT/$LOOK-t$t.png"); done
python3 "$ROOT/tools/chorus/contact.py" "$OUT/$LOOK-sections.jpg" "${files[@]}"
