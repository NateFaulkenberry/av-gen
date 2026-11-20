# ADR-2116: a crowd of skinned cast (1,024 palettes, shared poses, an opening phase)

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 10; owner msg 112: "get the place
  populated with as many aliens as possible").
- Code: `kMaxRigs` and slice sharing in `src/rendering/skinning.{hpp,cpp}`; `SkinnedRig::poseKey`, `adoptPose`,
  `sharedFrom` and `RigStats::shared` in `src/scene/animation.{hpp,cpp}`; `NodeAnimation::phase` in
  `src/scene/composition.{hpp,cpp}`; the headless `rigs:` line in `src/app/application.cpp`.
- Tests: `tests/unit/test_crowd_pose.cpp` (`[adr2116]`).

## Context

Neon Dreams had 25 Glowmere aliens, each a glTF node with its own rig, posed by timeline position and rotation
tracks. Asked for hundreds, a capacity probe (`build.py --crowd-test N`: every spot, then clones beside them; the
same 1080p offline still at 27 s, the Quay) measured:

| aliens | posed | rig CPU | skinned draws | GPU frame | scene pass | shadow | depth |
|---|---|---|---|---|---|---|---|
| 25 | 25 | 0.37 ms | 178 | 217 ms | 60 ms | 32 ms | 22 ms |
| 100 | 96 | 1.8 ms | 699 | 222 ms | 62 ms | 33 ms | 22 ms |
| 250 | 233 | 3.8 ms | 1,632 | 220 ms | 61 ms | 33 ms | 22 ms |
| 500 | 419 | 6.0 ms | 3,175 | 223 ms | 63 ms | 33 ms | 23 ms |

(The cloud pass, 90-117 ms, varies run to run by more than the whole cast costs.) Posing is about 15 us a rig;
drawing is a few microseconds a skinned draw. Neither is a wall. The wall was `kMaxRigs = 64`: past the 64th rig a
character got no joint palette and stood in its bind pose -- 436 T-poses in the 500 probe, with one warning line.
Impostors and baked far poses were not needed (ADR-151/153 found the same on other content): the 300 m rig cull and
the GPU's own culling already keep the far cast cheap.

## Decision

1. **1,024 palettes a frame.** The joint buffer is sized from the scene, as before; only the ceiling moved. A 90-joint
   alien's slice (this frame's and last frame's palette) is 11.5 KB, so the full buffer is 11.8 MB.
2. **A pose shared by rigs in step.** `SkinnedRig::poseKey` names a pose by (the asset's clip pack -- the shared
   `semanticsCache` --, the state, the exact clip second at the rig's sample time). A rig with layers, root motion,
   an external pose, a pending reseed or a cross-fade has no key: its pose is its own. In `updateRigs` the first
   rig with a key evaluates; a later rig with the same key and the same parts (checked, not trusted to the hash)
   copies its pose and palette (`adoptPose`) and keeps its own previous palette, so its motion vectors stay its own.
   The renderer draws such a rig from the source's slice, and uploads nothing for it, when both palettes (this
   frame's and last frame's) are byte-equal. A shared pose is the pose the rig would have computed: a lone rig sought
   to the same second computes the same palette (tested), so seek = play is untouched.
3. **An opening phase** (`animation.phase`, clip seconds at timeline second 0). The scene file's opening state used
   to start every node at clip second 0, so a crowd on one clip moved in step; a random rate (0.85-1.1) desynced them
   only slowly and made sharing impossible. With a phase from a few buckets (Neon Dreams: 8 per clip) and a few rates
   (0.92 / 1.0 / 1.08), a crowd reads as varied and still shares. A state asked for later (behaviour, cue,
   sequencer) starts when it is asked for, as before.
4. **The headless frame log says what posing cost**: `rigs: N posed P rate-limited R culled C joints J (shared S)
   cpu X ms | skinned draws D palettes Q (shared slices K, upload U KB)`.

## Consequences

- A crowd's cost is now its draws: about 6 skinned draws an alien (2-4 meshes; depth, scene and shadow passes).
- A rig past the 1,024th still stands in its bind pose with the same warning.
- The render job's "rig ladder reached the deliverable" line counts a 60 fps still loop's held frames as
  rate-limited; a 30 fps render with `updateHz` 0 or 30 poses every frame.
