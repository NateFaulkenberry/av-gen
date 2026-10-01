#!/bin/bash
# All You Got, art pass 2: regenerate, validate, render and measure, in one go (run from the repository root).
#
#   tools/liminal/render_pass2.sh preview      # 960x540, ~2-4 min:  $REVIEW/all-you-got-pass2-preview.mp4
#   tools/liminal/render_pass2.sh final        # 1920x1080 h264 q90: $REVIEW/all-you-got-pass2.mp4
#
# AVGEN overrides the engine (default: build/release/src/avgen). The binary loads shaders from the source tree, so
# while someone is editing shaders, render with a pinned copy: a binary plus `git archive <its commit> shaders`,
# exported through AVGEN_SHADER_DIR (see PROGRESS-art.md, "The pinned engine").
set -euo pipefail
MODE=${1:-preview}
AVGEN=${AVGEN:-./build/release/src/avgen}
REVIEW=${REVIEW:-$HOME/Desktop/av-gen-review/24-liminal-space/pass2}
PROJECT=examples/liminal/all-you-got-pass2.json
mkdir -p "$REVIEW"
python3 tools/liminal/make_all_you_got_pass2.py --clearance
if ./build/release/src/avgen --project "$PROJECT" --audit-routes /tmp/all-you-got-pass2-audit.json 2>&1 | grep -i "warn\|error"; then
    echo "the project does not load clean; not rendering" >&2
    exit 1
fi
case "$MODE" in
    preview) OUT="$REVIEW/all-you-got-pass2-preview.mp4"; SIZE=960x540; Q=80 ;;
    final)   OUT="$REVIEW/all-you-got-pass2.mp4"; SIZE=1920x1080; Q=90 ;;
    *) echo "mode: preview | final" >&2; exit 2 ;;
esac
tools/gpu-lock.sh "$AVGEN" --headless --project "$PROJECT" --render "$OUT" --range 0:258 --size "$SIZE" --fps 30 \
    --codec h264 --quality "$Q" --particle-warmup 30
python3 tools/liminal/pass2_av.py --video "$OUT" --out "$REVIEW/analysis-$MODE"
echo "rendered $OUT"
