#!/bin/bash
# Render one hero proof composition to a still through the GPU lock.
#   examples/strange-attractor/hero/render_proof.sh <name> <out.png> [second] [WxH]
set -u
cd "$(dirname "$0")/../../.."
name=$1; out=$2; t=${3:-4}; size=${4:-1280x720}
tmp=$(mktemp -d)
tools/gpu-lock.sh build/release/src/avgen --headless --composition docs/prototypes/strange-attractor/hero/$name.scene.json \
    --render "$tmp" --range "$t:$(python3 -c "print($t+0.016)")" --fps 60 --size "$size" --particle-warmup 90 > "$tmp/log.txt" 2>&1
rc=$?
grep -E "render complete|GPU errors|\[error\]" "$tmp/log.txt" | tail -2
cp "$tmp/frame_000000.png" "$out" && echo "WROTE $out (avgen exit $rc)"
