#!/bin/sh
# Re-export the nine farm animals from the source .blend (ADR-205).
#
# The source holds all nine laid out along X for a turntable render, sharing one 16x16 palette
# texture, plus a `Render` collection (camera, backdrop, three area lights, an animated empty) that
# is authoring apparatus. Each animal is an armature with one skinned mesh child and one `*Walk`
# action; the export script deletes the other eight animals and the render rig rather than hiding
# them, because an exporter set to "selected objects" still walks parents and because Blender's
# ACTIONS mode tries every action in the file against the armature it is exporting.
#
#   tools/make_farm_animals.sh ~/Desktop/FarmAnimalsLowpoly.blend assets/farm
set -e
BLEND="${1:?usage: make_farm_animals.sh <source.blend> <out-dir>}"
OUT="${2:?usage: make_farm_animals.sh <source.blend> <out-dir>}"
BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
SCRIPT="$(dirname "$0")/export_farm_animal.py"
mkdir -p "$OUT"

# file : mesh object : armature object : source action
set -- \
  "bull:Bull:BullArmature:BullWalk" \
  "cow:Cow:CowArmature:CowWalk" \
  "horse:Horse:HorseArmature:HorseWalk" \
  "pig:Pig:PigArmature:PigWalk" \
  "sheep:Sheep:SheepArmature:SheepWalk" \
  "goat:Goat:GoatArmature:GoatWalk" \
  "chicken:Chicken01:ChickenArmature:ChickenWalk" \
  "rooster:Rooster01:RoosterArmature:RoosterWalk" \
  "chick:Chick:ChickArmature:ChickWalk"
for spec in "$@"; do
  name=$(echo "$spec" | cut -d: -f1)
  mesh=$(echo "$spec" | cut -d: -f2)
  arm=$(echo "$spec" | cut -d: -f3)
  action=$(echo "$spec" | cut -d: -f4)
  "$BLENDER" -b "$BLEND" --python "$SCRIPT" -- "$mesh" "$arm" "$action" "$OUT/$name.glb" >/dev/null 2>&1
  printf '%-10s %s\n' "$name" "$(ls -l "$OUT/$name.glb" | awk '{print $5}') bytes"
done
