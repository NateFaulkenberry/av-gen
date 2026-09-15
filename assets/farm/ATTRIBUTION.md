# Farm animals

Nine animals exported from a single low-poly source file.

## Source

`FarmAnimalsLowpoly.blend` — a low-poly farm animal pack holding all nine animals in one scene,
laid out along X for a turntable render. Supplied by the project owner; the file is not in this
repository.

> **Licence: not recorded.** The source file carries no licence text, no author, and no URL — the
> only copyright string in it belongs to the sRGB profile inside its texture — and none was supplied
> with it. Fill this in before these assets are distributed outside the project. Every pack here
> except this one and the aliens has its licence travelling with its meshes
> (`assets/quaternius/License_Standard.txt`, `assets/imported/ATTRIBUTION.md`).

## What ships, and what does not

The source has 24 objects. Eighteen are the animals — nine armatures, each with one skinned mesh
child — and six are the apparatus of the turntable render they were presented in:

| dropped | why |
|---|---|
| `Camera` and its parent `Empty` | the turntable. The `Empty` carries a 180-frame `EmptyAction` that spins it |
| `Area`, `Area.001`, `Area.002` | three studio area lights |
| `Plane` | a 648 × 31 m backdrop with a bevel modifier, material `BG` |
| `Dots Stroke` material | a Blender grease-pencil default with no user |
| the other eight animals, per file | see below |

The export **deletes** all of this rather than hiding it, for two reasons. An exporter set to
"selected objects" still walks parents, so a hidden object that is somebody's parent ships anyway.
And Blender's glTF exporter in `ACTIONS` mode tries *every* action in the file against the armature
it is exporting: leaving `CowWalk` in the file while exporting the bull yields a `bull.glb` with
nine animations, eight of them driving the wrong skeleton.

## The animals

One GLB each. One skinned mesh, one skin, one material, one clip. All nine share a single 16×16
colour-palette texture and read their base colour a texel at a time from it, which is why the files
carry no other maps and why the whole pack is one material.

| file | joints | triangles | size (W × H × L, metres) | bytes |
|---|---:|---:|---|---:|
| `bull.glb` | 27 | 2,788 | 0.76 × 1.77 × 2.65 | 367,032 |
| `cow.glb` | 27 | 2,744 | 0.69 × 1.57 × 2.36 | 362,628 |
| `horse.glb` | 29 | 2,504 | 0.57 × 1.75 × 2.32 | 338,908 |
| `sheep.glb` | 25 | 2,202 | 0.50 × 0.94 × 1.25 | 299,944 |
| `goat.glb` | 27 | 1,924 | 0.26 × 0.74 × 0.95 | 273,964 |
| `pig.glb` | 25 | 1,424 | 0.43 × 0.69 × 1.26 | 215,020 |
| `rooster.glb` | 22 | 1,923 | 0.16 × 0.43 × 0.36 | 262,108 |
| `chicken.glb` | 17 | 1,923 | 0.15 × 0.34 × 0.31 | 251,020 |
| `chick.glb` | 15 | 1,500 | 0.05 × 0.11 × 0.10 | 199,172 |

The source is authored in **metres**, so these are placed at **scale 1.0** — like
`assets/aliens/*.glb`, and unlike `assets/imported/alien.gltf`, which is in centimetres and needs
0.01. Each animal stands on Y = 0, centred on X, facing +Z, which is the direction the glTF
specification says the front of an asset faces. The joint counts above are the source's bones plus,
for the bull and the chick, the one neutral bone Blender adds to hold vertices that carry no weight.

## Animations

One per animal, and on every one of the nine it is called **`Walk`**. The source names it per
species — `BullWalk`, `CowWalk`, `ChickWalk` — which is what it has to be when nine rigs share one
file; once each animal is its own GLB the species is the filename, and a scene author writing
`"animation": {"state": "Walk"}` should get a walk out of whichever animal they placed. The same
judgement renamed the alien clips in ADR-192.

Clips are 25 or 28 frames at 30 fps, exported **sampled at every frame**: the source F-curves are
Bezier, glTF has no Bezier sampler, and sampling is what carries the easing across. Checked frame by
frame against Blender's own posed mesh, the exported clips reproduce it exactly.

| file | clip | frames | seconds |
|---|---|---:|---:|
| `bull.glb` | `Walk` (from `BullWalk`) | 28 | 0.900 |
| `cow.glb` | `Walk` (from `CowWalk`) | 28 | 0.900 |
| `horse.glb` | `Walk` (from `HorseWalk`) | 28 | 0.900 |
| `sheep.glb` | `Walk` (from `SheepWalk`) | 28 | 0.900 |
| `goat.glb` | `Walk` (from `GoatWalk`) | 28 | 0.900 |
| `pig.glb` | `Walk` (from `PigWalk`) | 28 | 0.900 |
| `rooster.glb` | `Walk` (from `RoosterWalk`) | 25 | 0.800 |
| `chicken.glb` | `Walk` (from `ChickenWalk`) | 25 | 0.800 |
| `chick.glb` | `Walk` (from `ChickWalk`) | 25 | 0.800 |

There is no idle, no run and no eat. The source has one action per animal and this import invented
nothing.

## Three that needed handling

**The pig was half a pig.** Its mesh carries a Mirror modifier ahead of its Armature modifier, and
the export must run with `export_apply=False` — applying modifiers would bake the armature away. So
the exporter applies every *non-armature* modifier by hand first. Without that step `pig.glb` is
386 vertices of left-hand side.

**The cow's armature had the bull's action stashed on it**, in an `[Action Stash]` NLA track. Stashes
are Blender's undo for "what was this rig doing before", and `export_nla_strips=False` ignores them,
but the strip is a user of the action and would have kept `BullWalk` alive through the sweep that
deletes the other eight. The exporter clears the NLA tracks first.

**The sheep's action animates four bones that do not exist** — `Chest.001`, `Tail`, `Tail.001`,
`Tail.002`, left over from a rig revision; this sheep has no tail. Blender reports
`Animation target pose.bones["Tail"] not found` 56 times and drops them, which is correct: all 25
bones the sheep does have are animated, and the export is complete. Nothing was done about this
because there is nothing to do.

## Two harmless warnings

**`There are more than 4 joint vertex influences`** (eight of the nine). glTF's base specification
carries four influences per vertex, which is also exactly what AV Gen's `SkinInfluence` holds;
Blender keeps the four heaviest and renormalises. Verified: every exported vertex's weights sum to
1.0 to within 1e-5.

**`Mesh Cube.0NN is not valid, and may be exported wrongly`** (bull, cow, horse, chicken, rooster).
Blender's `mesh.validate()` reports a change on those five source meshes. It is not a topology
problem: vertex, edge and face counts are identical before and after validation, there are no loose
vertices or edges, and the exported geometry matches the source count for count — 1,396 vertices and
2,788 triangles for the bull, against 1,396 and 1,418 quads-and-triangles in the `.blend`. No
degenerate triangles, no non-unit normals and no NaN in any of the nine.

## What the manifest says and does not say

`assets/farm.manifest.json` registers all nine as `creature` entries with their real metre sizes.
A scattered instance is **static** — `world::scatter` merges an asset's primitives per material and
the merge does not carry skin influences, so a scattered farm animal draws in its bind pose. That is
fine for a distant herd and wrong for anything the camera is looking at. An animal that has to move
must arrive as a `gltf` node with an `animation` state, which is what
`examples/farm/farm-animals.scene.json` does.

## Regenerating

    tools/make_farm_animals.sh ~/Desktop/FarmAnimalsLowpoly.blend assets/farm
