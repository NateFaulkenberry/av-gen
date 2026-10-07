#!/bin/bash
# times.sh <look> <outdir> <WxH> <supersample> <t>... : stills of one look at several seconds, one GPU lock.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LOOK="$1"; OUT="$2"; SIZE="$3"; SS="$4"; shift 4
mkdir -p "$OUT"
python3 "$ROOT/examples/chorus-field/build.py" "${LOOK//-/_}" > /dev/null || exit 1
AVGEN_GPU_LOCK_TIMEOUT=${AVGEN_GPU_LOCK_TIMEOUT:-14400} AVGEN_ROOT="$ROOT" OUT="$OUT" SIZE="$SIZE" SS="$SS" LOOK="$LOOK" \
"$ROOT/tools/gpu-lock.sh" bash -c '
  for t in "$@"; do
    TMP=$(mktemp -d "${TMPDIR:-/tmp}/chorus-times.XXXXXX")
    t2=$(python3 -c "print($t + 1.0/60)")
    "$AVGEN_ROOT/build/release/src/avgen" --headless --project "$AVGEN_ROOT/examples/chorus-field/$LOOK.json" \
      --render "$TMP" --range "$t:$t2" --size "$SIZE" --supersample "$SS" > "$TMP/log.txt" 2>&1 || echo "$t failed"
    f=$(ls "$TMP"/*.png 2>/dev/null | head -1); [ -n "$f" ] && mv "$f" "$OUT/$LOOK-t$t.png"
    rm -rf "$TMP"
  done' _ "$@"
files=(); for t in "$@"; do [ -f "$OUT/$LOOK-t$t.png" ] && files+=("$OUT/$LOOK-t$t.png"); done
python3 "$ROOT/tools/chorus/contact.py" "$OUT/$LOOK-sections.jpg" "${files[@]}"
