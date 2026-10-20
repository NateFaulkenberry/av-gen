#!/bin/bash
# Render one 1080p still of each look-dev view through AV Gen (headless, offline tier), each under the GPU lock.
#   examples/rainbow-world/lookdev/render_views.sh <lookdev dir> <tag> [views...]
# Writes <lookdev dir>/stills/<tag>/<view>.png. --render paths resolve against the project's folder.
set -u
DIR=$(cd "$1" && pwd)
TAG=$2
shift 2
VIEWS=${*:-"wide close rainbow clouds fisheye"}
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$DIR/stills/$TAG"
for v in $VIEWS; do
  rm -rf "$DIR/_r_$v"
  AVGEN_GPU_LOCK_TIMEOUT=14400 "$ROOT/tools/gpu-lock.sh" "$ROOT/build/release/src/avgen" --headless \
    --project "$DIR/lookdev_$v.json" --render "_r_$v" --range 0:0.03 --log warn > "$DIR/stills/$TAG/$v.log" 2>&1
  rc=$?
  cp "$DIR/_r_$v/frame_000000.png" "$DIR/stills/$TAG/$v.png" 2>/dev/null
  echo "$v exit $rc"
done
