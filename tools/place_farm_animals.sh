#!/bin/sh
# Place the farm animals in a scene and splice them in (ADR-209, Part 1).
#
# Two halves because the decisions and the file surgery want different tools: the decisions need the
# engine's own terrain, ecology and obstacle queries (so they are C++), and the surgery needs to
# leave 7,200 lines of somebody else's floats exactly as they were (so it is a text-preserving
# Python splice, not a JSON round trip).
#
#   tools/place_farm_animals.sh [scene.json] [--seed N]
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SCENE="${1:-$REPO/examples/world/glowmere-valley-2.scene.json}"
shift 2>/dev/null || true
FRAGMENT="${TMPDIR:-/tmp}/avgen-farm-fragment.json"
"$REPO/build/release/tools/avgen_place_farm_animals" "$SCENE" --emit "$FRAGMENT" "$@"
"$REPO/tools/splice_farm_animals.py" "$FRAGMENT" "$SCENE"
"$REPO/tools/refresh_scene_fingerprint.py"
