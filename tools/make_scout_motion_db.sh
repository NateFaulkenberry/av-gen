#!/usr/bin/env bash
# The scout's own MotionPack and a baked motion database for it (ADR-825).
#
# Built from `assets/aliens/alien-scout.glb` (CC0, see assets/aliens/ATTRIBUTION.md) into the
# gitignored `assets/aliens/scout-pack/`: every alien shares the scout's skeleton (ADR-650), so one
# pack and one database serve all six. A scene points an entity at them with
#     "motionMatching": {"joints": ["foot.l", "foot.r", "head.x"], "contacts": ["foot.l", "foot.r"],
#                        "pack": "<rel>/assets/aliens/scout-pack",
#                        "database": "<rel>/assets/aliens/scout-pack/databases/scout.motiondb"}
# and the database is then loaded off the render thread instead of built at composition time.
#
# Usage: tools/make_scout_motion_db.sh [build-dir]   (default build/release)
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build/release}"
tool="$build/tools/avgen_motion"
out="$root/assets/aliens/scout-pack"
[[ -x "$tool" ]] || { echo "no $tool -- build avgen_motion first" >&2; exit 1; }
rm -rf "$out"
"$tool" pack "$root/assets/aliens/alien-scout.glb" --out "$out" --license CC0-1.0 --source "alien pack (scout)" \
    --contacts foot.l,foot.r --redistribution allowed --derivedDataAllowed true
"$tool" build-db "$out" --joints foot.l,foot.r,head.x --contacts foot.l,foot.r --name scout --force
"$tool" validate "$out"
