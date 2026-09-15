# ADR-205: Nine farm animals from one turntable scene

**Status:** Accepted
**Date:** 2026-09-14

A low-poly farm animal pack, `FarmAnimalsLowpoly.blend`, to be imported as environmental props —
animals hiding in the woods for a UFO to lift out of them. Animation desirable; not allowed to block
the import.

It did not block the import. All nine animals came through rigged, animated, textured and correctly
scaled, with **zero engine changes and zero importer warnings**, which makes this the second pack in
a row (after ADR-192's aliens) that the glTF path took exactly as it was handed. The interesting
parts are all in the ten minutes before the exporter ran.

## What the file actually contained

24 objects in two collections. `Models` holds the nine animals — **an armature plus one skinned mesh
child each**, never combined, never instanced — and `Render` holds the apparatus of the turntable
they were presented in: a camera parented to an empty that carries a 180-frame spin action, three
area lights, and a 648 × 31 m backdrop plane.

The animals are laid out **along X**, one per slot, from the horse at x = −1.76 to the chick at
x = +2.41. That is a property of the presentation, not of the animal, and the export zeroes it.

| animal | bones | source action | frames |
|---|---:|---|---:|
| Bull | 26 | `BullWalk` | 28 |
| Cow | 27 | `CowWalk` | 28 |
| Horse | 29 | `HorseWalk` | 28 |
| Sheep | 25 | `SheepWalk` | 28 |
| Goat | 27 | `GoatWalk` | 28 |
| Pig | 25 | `PigWalk` | 28 |
| Rooster | 22 | `RoosterWalk` | 25 |
| Chicken | 17 | `ChickenWalk` | 25 |
| Chick | 14 | `ChickWalk` | 25 |

One action apiece, and it is a walk. There is no idle and no run; this import invented neither.
Every animal shares one material and one **16 × 16 colour-palette JPEG**, read a texel at a time —
which is why the files carry no other maps and why the whole pack is one material and one texture.

## Three things the export has to do that "select and export" does not

**Delete the other eight animals, not hide them.** Two reasons, and the second is the one that
bites. An exporter set to "selected objects" still walks parents, so a hidden object that is
somebody's parent ships anyway — that much ADR-192 already knew. But Blender's glTF exporter in
`ACTIONS` mode tries *every action in the file* against the armature it is exporting. Leave
`CowWalk` in the file while exporting the bull and you get a `bull.glb` carrying nine animations,
eight of them driving a skeleton that is not there.

**Apply the non-armature modifiers.** The pig is modelled as a half-pig with a Mirror modifier, and
the export must run with `export_apply=False` — applying modifiers would bake the armature away. So
the exporter applies everything in the stack that is not the Armature modifier, by hand, first.
Without that step `pig.glb` is 386 vertices of left-hand side and nothing says so.

**Clear the NLA tracks.** The cow's armature carries an `[Action Stash]` track holding *the bull's*
action. `export_nla_strips=False` ignores stashes at export time, but the strip is a **user** of the
action, and it would have kept `BullWalk` alive through the sweep that deletes the other eight.

## The clip is called `Walk` on all nine

The source names it per species because it has to: nine rigs share one file. Once each animal is its
own GLB the species is the filename, and a scene author writing `"animation": {"state": "Walk"}`
should get a walk out of whichever animal they placed. ADR-192 made the same call on the alien
clips. The source names are recorded in `assets/farm/ATTRIBUTION.md`, which is where a rename
belongs.

## Sampling, and a measurement that was wrong twice before it was right

The source F-curves are **Bezier**, with 5–14 keys over a 25–28 frame cycle. glTF has no Bezier
sampler — only `STEP`, `LINEAR` and `CUBICSPLINE` — so the curve has to be sampled, and
`export_force_sampling` (on by default) does that. `export_optimize_animation_size`, also on by
default, then thins the samples back down to the ones a straight line can rejoin.

The first measurement said that thinning moved the bull's bounding box **7%** and the cow's **12%**
mid-stride, and the thinning was duly turned off. Both numbers were wrong. Blender's exporter maps
frame *f* to time *f*/fps, not (*f*−1)/fps, and the comparison was reading the GLB **one frame
early** — on a 28-frame walk cycle, one frame of leg swing is exactly a 7–12% change in the length
of the bounding box. Re-measured against the same GLBs at the right times, the optimizer changes
**nothing**: it only ever drops a sample its neighbours already reproduce.

Sampling every frame with no thinning is still what ships, but for the real reason rather than the
invented one: the engine samples a clip at continuous render time, not at frame boundaries, and
between frames the thinning is a tolerance rather than an identity. It costs 19 KB on a 367 KB file.

The same pass found a genuine defect worth fixing: without `export_anim_slide_to_zero` the first
sample lands at `frame_start`/fps = 1/30 s rather than at 0, so the clip opens by holding its first
pose for a frame and closes a frame short of its own loop point.

**With all three settled, the exported clips reproduce Blender exactly.** Every frame of all nine
animals, skinned on the CPU straight out of the GLB by the specification's own formula and compared
against Blender's evaluated mesh: **0.00% difference in every dimension, on 237 frames.**

## The scale was already right, and the 0.8996 is a red herring

Every armature in the source is scaled 0.8996, and the mesh parented under it carries a
`matrix_parent_inverse` that cancels exactly that. Blender's exporter writes the armature node with
scale 0.8996 and computes the inverse binds in armature space, so the two cancel again on the way
out: joint-global × inverse-bind is the identity at rest, and the rest-pose vertex data **is** the
metres the animal stands in. Verified rather than assumed — the CPU skinner above reproduces
`POSITION` exactly at rest for all nine.

So, at scale 1.0:

| | W | H | L |
|---|---:|---:|---:|
| bull | 0.76 | **1.77** | 2.65 |
| horse | 0.57 | **1.75** | 2.32 |
| cow | 0.69 | **1.57** | 2.36 |
| sheep | 0.50 | **0.94** | 1.25 |
| goat | 0.26 | **0.74** | 0.95 |
| pig | 0.43 | **0.69** | 1.26 |
| rooster | 0.16 | **0.43** | 0.36 |
| chicken | 0.15 | **0.34** | 0.31 |
| chick | 0.05 | **0.11** | 0.10 |

Metres, and the right ones. Each animal stands on Y = 0 (the hooves dip below by at most a tenth of
a percent of the height), is centred on X, and faces **+Z**, which is where the glTF specification
says the front of an asset faces — `export_yup` maps Blender's −Y, where all nine were modelled
facing, onto exactly that.

## Two warnings that are not damage

**`There are more than 4 joint vertex influences`** (eight of nine). glTF's base specification
carries four influences per vertex, which is also exactly what `scene::SkinInfluence` holds. Blender
keeps the four heaviest and renormalises; every exported vertex's weights sum to 1.0 within 1e-5.

**`Mesh Cube.0NN is not valid, and may be exported wrongly`** (bull, cow, horse, chicken, rooster).
This one deserved the look it got, because "may be exported wrongly" is exactly what a corrupted
import sounds like. `mesh.validate()` reports a change on those five source meshes — and changes
nothing structural: vertex, edge and face counts are identical before and after, there are no loose
vertices or edges, and the exported geometry matches the source count for count. Across all nine:
**no degenerate triangles, no non-unit normals, no NaN**, and every index in range.

A third message, `Animation target pose.bones["Tail"] not found` (56 times, sheep only), is the
sheep's action animating four bones a rig revision deleted — `Chest.001`, `Tail`, `Tail.001`,
`Tail.002`. This sheep has no tail. All 25 bones it does have are animated; there is nothing to fix.

## Where things are, and what the repository keeps

* `assets/farm/*.glb` — nine animals, 2.5 MB, **gitignored**. Not for size: like the aliens, and
  unlike every other pack here, the source carries **no recorded licence** — no author, no URL, and
  the only copyright string in the whole file belongs to the sRGB profile inside its texture. That
  is flagged in three places rather than assumed away.
* `assets/farm/ATTRIBUTION.md` — the inventory, the licence warning, the three animals that needed
  handling, the two warnings that are harmless and why, and the regeneration command.
* `assets/farm.manifest.json` — all nine as `creature` entries with their real metre sizes. Inert
  until a recipe names it, like `assets/city.manifest.json`; nothing globs the manifest directory.
* `tools/export_farm_animal.py`, `tools/make_farm_animals.sh` — one command regenerates all nine.
* `examples/farm/farm-animals.{json,scene.json}` — one of each, in profile, to scale, all walking.
  In the examples menu under Lab.
* `tests/unit/test_farm_animals.cpp` — 361,314 assertions over three cases.

## The limitation a scene author has to know

**A scattered farm animal does not move.** `world::scatter` merges an asset's primitives into one
mesh per material and the merge does not carry skin influences, so a manifest entry placed as a
scatter layer draws in its bind pose, identically, forever — which is fine for a herd on a far
hillside and wrong for anything the camera looks at. An animal that has to move must arrive as a
`gltf` node with an `animation` state: by hand in a scene file, or as a hero, which is a real
`NodeKind::Gltf` node and does copy the rig. This is ADR-192's per-instance rig copy seen from the
other side, and it is recorded here rather than worked around.

Related: `variation.lean` is 0 on every manifest entry and should stay there. A bush may lean; an
animal standing on four legs that leans is an animal falling over.

## Measured

1920 × 480, one GPU lock, all nine animals in one frame: **10,991 triangles, 7 draws, 10 shadow
draws, 23.0 ms GPU** (scene 14.8, shadow 5.7), and 300 offline frames at 152 fps. Rendered twice;
the sequence hash is `e7062575ceae354f` both times and all 300 PNGs are byte-for-byte identical, so
the pose is a pure function of the timeline the way ADR-086 requires.
