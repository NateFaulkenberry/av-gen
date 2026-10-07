#!/bin/bash
# Render one still of a CHORUS FIELD look: still.sh <look> <t> <out.png> [WxH] [supersample]
# Runs under the GPU lock. Prints the avgen exit code.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LOOK="$1"; T="$2"; OUT="$3"; SIZE="${4:-1920x1080}"; SS="${5:-1}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/chorus-still.XXXXXX")"
T2=$(python3 -c "print($T + 1.0/60)")
AVGEN_GPU_LOCK_TIMEOUT=14400 "$ROOT/tools/gpu-lock.sh" "$ROOT/build/release/src/avgen" --headless --project "$ROOT/examples/chorus-field/$LOOK.json" \
    --render "$TMP" --range "$T:$T2" --size "$SIZE" --supersample "$SS" > "$TMP/log.txt" 2>&1
code=$?
f=$(ls "$TMP"/*.png 2>/dev/null | head -1)
if [ -n "$f" ]; then mv "$f" "$OUT"; fi
grep -iE "error|refus|fail" "$TMP/log.txt" | grep -v "minBindingSize" | grep -v "GPU errors: 0" | head -5
echo "exit=$code out=$OUT"
rm -rf "$TMP"
