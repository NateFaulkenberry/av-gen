# ADR-192: Six aliens from one modular source

**Status:** Accepted
**Date:** 2026-09-14

## What was asked, and what the file actually contained

A modular alien pack, described as "Unreal Engine 5 Mannequin/Epic skeletal rig, 4 heads, 3 bodies,
2 backpacks, 26 animations". Three of those four claims survived inspection.

**It is an Auto-Rig Pro file, not an Epic mannequin.** 378 bones, of which **89 deform**; the rest
are controls, IK targets and `*_ref` bones. That matters because a naive export ships all 378 and
AV Gen's palette cap is 256 — the skin would have been rejected outright with
`"{} joints exceeds the {} the palette holds; skin skipped"`. Exporting deform bones only gives 89,
comfortably inside the cap, and Blender re-parents each deform bone to its nearest deform ancestor.

**45 objects, 9 of which are the character.** The other 36 are 26 zero-triangle `cs_*` control
shapes, two rig empties, and — 32 MB of the 33 MB file — a 4K authoring HDR.

The export **deletes** the apparatus rather than hiding it. An exporter set to "selected objects"
still walks parents, so a hidden object that is somebody's parent ships anyway.

## The variants

Six, chosen to span all four heads, all three bodies, and both packs plus no pack, so they read as
different characters rather than as six arrangements of one. 4,098–6,601 triangles, which is the
source's own range. One 89-joint skeleton, one 128×128 atlas and all 26 clips, shared by all six.

Clip names were cleaned at export: `_Runing` → `Running`, `Jump_runing` → `Jump_running`,
`Flying-jet` → `Flying_jet`, `Dying_forward_` → `Dying_forward`, and the leading underscore dropped
from the five oldest actions. A scene author should be typing `Running`.

## No engine changes were needed

Nothing in `src/` was touched. The importer took the assets as they are: `1 rig / 90 joints /
26 clips`, bounds 1.66–1.79 m, **zero warnings**. Two things made that true rather than lucky.

**Morph targets are not exported.** AV Gen refuses them in two places on purpose — the geometry
import warns `morph targets ignored`, and the animation import drops morph-weight channels because
`AnimationPath` has only translation, rotation and scale. Shipping them would have added ~104 KB per
variant that nothing reads and two warnings to every load of every character. The `.blend` keeps all
ten shape keys per head and body, and `tools/export_alien_variant.py --morphs` restores them the day
the engine grows a use for them. **That is the whole migration**, which is why the flag exists.

**The scale is already right.** The source is in metres, so these are placed at scale 1.0 — unlike
`assets/imported/alien.gltf`, which is in centimetres and needs 0.01 in every scene that uses it.

## The limitation that could not be worked around

**Each variant carries its own copy of all 26 animations: 86% of each file, 3.9 MB of 4.5.**

This is not an export choice. AV Gen has no mechanism to bind one file's clips to another file's
skeleton — `Importer::importClips` only fills rigs built in the same `loadGltf` call — and
`Composition` deep-copies the entire rig per node instance by design:

> ADR-086: rigs are copied per node instance, not per asset. Two nodes on the same character file are
> two characters, and they must be able to be doing different things; sharing one pose between them
> is the bug, not the saving.

Meshes and textures *are* deduplicated per asset path in the same loop. Rigs are not. So six
variants cost six copies on disk, and six instances of one variant cost six copies of its keyframe
data in memory. `assets/quaternius/animations/` (43 clips on a shared rig) has been unreachable for
the same reason since it was imported.

The fix is a real one — clips behind a `shared_ptr` on `SkinnedRig`, so the *pose* stays per instance
while the *keyframes* are shared — and it is a change to a type that half the animation system holds
by value. Recorded rather than attempted here.

## Measured

1280×800, 180 frames, one GPU lock. **These are pass timings, not frame times**: the offline path's
`queueWait` was 26 ms of a 27 ms frame, so the frame is queue-bound and its p50 says nothing about
character cost.

| characters | scene pass | shadow pass | draws | triangles |
|---:|---:|---:|---:|---:|
| 1 | 4.52 ms | 1.25 ms | 4 | 4,291 |
| 6 | 6.16 ms | 15.47 ms | 22 | 35,149 |
| 25 | 7.54 ms | 14.75 ms | 84 | 144,115 |
| 50 | 7.93 ms | 11.34 ms | 167 | 289,449 |

**Fifty skinned characters add 3.4 ms to the scene pass** — about 3.3 draws each, which is the head,
the body and sometimes a pack. The shadow pass is the one to watch: it jumps an order of magnitude
between one character and six and then stops growing, which is the cascades picking up casters rather
than a per-character cost. Worth a proper look before a scene ships a crowd.

## Where things are

* `assets/aliens/*.glb` — six variants, **gitignored**: 27 MB, and with no recorded licence, which
  is the second reason. `assets/aliens/ATTRIBUTION.md` is what the repository keeps.
* `tools/export_alien_variant.py`, `tools/make_aliens.sh` — one command regenerates all six.
* `examples/characters/alien-squad.{json,scene.json}` — six variants, six clips, one frame. In the
  examples menu under Lab, beside the character it does not replace.
