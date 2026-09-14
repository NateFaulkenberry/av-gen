#!/bin/sh
# Re-export the six alien variants from the source .blend (ADR-192).
#
# The source is an Auto-Rig Pro file: 378 bones of which 89 deform, nine character meshes, and a
# collection of `cs_*` control shapes and a 4K HDR that exist only for authoring. The export script
# deletes those rather than hiding them -- an exporter set to "selected objects" still walks parents,
# so a hidden object that is somebody's parent ships anyway.
#
#   tools/make_aliens.sh ~/Desktop/Alien_Modular_animated.blend assets/aliens
set -e
BLEND="${1:?usage: make_aliens.sh <source.blend> <out-dir>}"
OUT="${2:?usage: make_aliens.sh <source.blend> <out-dir>}"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
SCRIPT="$(dirname "$0")/export_alien_variant.py"
mkdir -p "$OUT"

# head body pack -- chosen to span all four heads, all three bodies, and both packs plus no pack,
# so the six read as different characters rather than as six arrangements of the same one.
set -- \
  "alien-scout:Head_1:Body:none" \
  "alien-diver:Head_Helmet:Body_Spacesuit:none" \
  "alien-elder:Head_Brain:Body:none" \
  "alien-pilot:Head_Mask:Body_Spacesuit:Backpack_Jet" \
  "alien-trooper:Head_Helmet:Body_Armor:Backpack_1" \
  "alien-ranger:Head_Mask:Body_Armor:Backpack_Jet"
for spec in "$@"; do
  name=$(echo "$spec" | cut -d: -f1)
  head=$(echo "$spec" | cut -d: -f2)
  body=$(echo "$spec" | cut -d: -f3)
  pack=$(echo "$spec" | cut -d: -f4)
  "$BLENDER" -b "$BLEND" --python "$SCRIPT" -- "$head" "$body" "$pack" "$OUT/$name.glb" >/dev/null 2>&1
  printf '%-16s %s\n' "$name" "$(ls -l "$OUT/$name.glb" | awk '{print $5}') bytes"
done
