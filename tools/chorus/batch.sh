#!/bin/bash
# batch.sh <outdir> <sheet-name> <WxH> <supersample> <t> <look>... : build, render each look, contact sheet.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$1"; SHEET="$2"; SIZE="$3"; SS="$4"; T="$5"; shift 5
mkdir -p "$OUT"
names=()
for l in "$@"; do names+=("${l//-/_}"); done
python3 "$ROOT/examples/chorus-field/build.py" "${names[@]}" > /dev/null || exit 1
files=()
for l in "$@"; do
  "$ROOT/tools/chorus/still.sh" "$l" "$T" "$OUT/$l.png" "$SIZE" "$SS" | grep -v "exit=0"
  files+=("$OUT/$l.png")
done
python3 "$ROOT/tools/chorus/contact.py" "$OUT/$SHEET" "${files[@]}"
