#!/bin/bash
# batch.sh <outdir> <sheet-name> <WxH> <supersample> <t> <look>... : build, render every look under ONE
# acquisition of the GPU lock (the machine is shared; one wait instead of one per still), contact sheet.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$1"; SHEET="$2"; SIZE="$3"; SS="$4"; T="$5"; shift 5
mkdir -p "$OUT"
names=()
for l in "$@"; do names+=("${l//-/_}"); done
python3 "$ROOT/examples/chorus-field/build.py" "${names[@]}" > /dev/null || exit 1
export AVGEN_GPU_LOCK_TIMEOUT=${AVGEN_GPU_LOCK_TIMEOUT:-14400}
AVGEN_ROOT="$ROOT" OUT="$OUT" SIZE="$SIZE" SS="$SS" T="$T" "$ROOT/tools/gpu-lock.sh" bash -c '
  T2=$(python3 -c "print($T + 1.0/60)")
  for l in "$@"; do
    TMP=$(mktemp -d "${TMPDIR:-/tmp}/chorus-batch.XXXXXX")
    "$AVGEN_ROOT/build/release/src/avgen" --headless --project "$AVGEN_ROOT/examples/chorus-field/$l.json" \
      --render "$TMP" --range "$T:$T2" --size "$SIZE" --supersample "$SS" > "$TMP/log.txt" 2>&1
    code=$?
    f=$(ls "$TMP"/*.png 2>/dev/null | head -1)
    [ -n "$f" ] && mv "$f" "$OUT/$l.png"
    [ $code -ne 0 ] && { echo "$l exit=$code"; grep -iE "error" "$TMP/log.txt" | head -3; }
    rm -rf "$TMP"
  done' _ "$@"
files=()
for l in "$@"; do [ -f "$OUT/$l.png" ] && files+=("$OUT/$l.png"); done
python3 "$ROOT/tools/chorus/contact.py" "$OUT/$SHEET" "${files[@]}"
