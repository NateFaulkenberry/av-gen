#!/bin/bash
# Run by song.sh while it holds the GPU lock.
ROOT="$1"; LOOK="$2"; OUT="$3"; CLIP="$4"; shift 4
for t in "$@"; do
  TMP=$(mktemp -d "${TMPDIR:-/tmp}/chorus-song.XXXXXX")
  t2=$(python3 -c "print($t + 1.0/60)")
  "$ROOT/build/release/src/avgen" --headless --project "$ROOT/examples/chorus-field/$LOOK.json" --render "$TMP" \
    --range "$t:$t2" --size 1280x720 > "$TMP/log.txt" 2>&1 || echo "$t failed"
  f=$(ls "$TMP"/*.png 2>/dev/null | head -1); [ -n "$f" ] && mv "$f" "$OUT/$LOOK-t$t.png"
done
if [ "$CLIP" != "-" ]; then
  "$ROOT/build/release/src/avgen" --headless --project "$ROOT/examples/chorus-field/$LOOK.json" \
    --render "$OUT/$LOOK-${CLIP/:/-}s.mp4" --codec h264 --range "$CLIP" --size 960x540 --fps 30 > "$OUT/clip.log" 2>&1
  echo "clip exit=$?"
fi
