# ADR-583: The BVH is built over what does not move, and kept while it has not

- Status: Accepted (2026-09-21)
- Extends ADR-351 (the Embree tracer), ADR-383 (path-traced sequences). Owed by
  `docs/offline-backend-audit.md` step 11 ("BVH reuse across frames") and priced by
  `docs/metal-rt-decision.md`. Related: ADR-360 (a render must be reproducible), ADR-182 (a probe
  that cannot fail), ADR-170 (minima, never means).

## Problem

`app::TraceSequence` made a fresh `pathtrace::PathTracer` per frame, and `PathTracer::render` made a
fresh `EmbreeScene`, so every frame of a path-traced range rebuilt the whole acceleration structure
-- device included. `docs/metal-rt-decision.md` measured that build at 1,669 ms on the Tree of Life
(3.1M triangles, contended), which prices a 240-frame shot at 6.7 minutes of BVH before a ray.

## What actually changes from frame to frame

Measured before designing anything, by diffing consecutive snapshots of the two shipped scenes
object by object (a temporary probe in the sequence loop; not committed).

**Tree of Life** (`examples/treeisland/tree-of-life-floating-island.json`, t = 8 s):

- **All 33 meshes, all 3,102,341 triangles, change every frame in world space.** Not one vertex
  changes in object space: each mesh's new positions equal `M_new * inverse(M_old) * old` to
  1e-5 m. The island drifts as a rigid body. No rigs, no CPU deformation.
- ADR-360's mesh wind is `common.wgsl`'s vertex stage -- GPU only. The snapshot never sees it, so
  the path tracer renders the tree unbent. (A parity gap, recorded here, not introduced here.)
- The 4 procedural objects (4,492 instances) do not move. The camera does not move at t = 8 s.

**Glowmere valley 2** (`examples/world/glowmere-valley-2-multicam.json`, t = 12 s):

- 252 meshes, 268,985 triangles. **21-28 meshes (41-60k triangles) change per frame**, every one
  of them a skinned character (`rook`, `tide`, the visitors): their entity transforms move every
  frame and their poses change on most frames (on some frames the change is purely rigid).
- 73 procedural objects, 67,770 instances. **314-342 instance transforms change per frame**
  (river and tarn lilies, drifting petals, the visitor proxies). No procedural *source* changes.
- 224 lights, unchanged. The camera is static at this second.
- Particles are GPU-only and never reach the snapshot (the capability report already says so).

So the dominant kind of change in both scenes is **rigid motion**, and the snapshot as it stood --
positions baked into world space -- turned every rigid motion into "every vertex changed". A BVH
keyed on world-space vertices would have been rebuilt on every frame of the Tree of Life.

## Decision

1. **The BVH is built over object space.** `TriangleMesh` gains `objectPositions` (after skinning,
   before the entity transform), `objectToWorld` and `deforming` (posed by a rig). `positions` is
   unchanged and remains the world-space truth that shading, emission and motion read.
2. **Two levels, always.** Meshes that share a bit-identical transform share one child scene (so
   the Tree of Life's 33 meshes are 3 children, and its canopy is still one single-level BVH); a
   rigged mesh is always a child of its own; each procedural source is a child, as before. The top
   level holds one instance per mesh group and one per procedural copy. The structure is a pure
   function of the snapshot -- never of history -- so a frame that reused and a frame built from
   scratch trace the same structure.
3. **Keep what is provably unchanged, rebuild the rest from scratch.** A child is kept only if its
   member list, counts and every vertex and index are bit-identical to the buffers Embree holds; it
   is compared against Embree's own copy, so no second copy is kept and no hash can collide. The top
   level is kept only if every child is and every transform is bit-identical. Anything else is
   rebuilt through the same code the first frame used.
4. **The control arm stays live.** `TraceSettings::reuseAcceleration` (false = rebuild everything,
   device included, as before) and `--pt-rebuild-bvh` on the CLI. `BvhReuse::TrustStructure` --
   change detection switched off -- exists for tests only.
5. **Every commit is joined** (`rtcJoinCommitScene`), including the small children that used to go
   through `rtcCommitScene`. With Embree's internal tasking a join uses exactly the calling threads
   and no pool; that made `docs/pathtrace/overview.md`'s "Embree starts no pool of its own" true
   (it was not, for instanced children), and it lets changed small children build side by side, one
   per thread. On Glowmere that took rebuilding 28 characters from 86 ms to 14 ms.

## Alternatives considered

- **Keep world space, reuse only when world positions are unchanged.** Exact and minimal, and it
  saves nothing on the Tree of Life, where every world-space vertex moves every frame. Rejected on
  the measurement above.
- **One child scene per mesh.** Simplest two-level shape. Rejected because it turns the Tree of
  Life's overlapping canopy meshes into a dozen overlapping BVHs every ray must descend; grouping by
  transform keeps the single-level canopy while still separating what moves differently.
- **Refit (`RTC_BUILD_QUALITY_REFIT`) for deforming meshes.** Cheaper than a rebuild, but a refit
  BVH differs from a rebuilt one, so a reused frame would no longer be bit-identical to a
  from-scratch frame, and refit quality decays over a walk cycle. The only deforming geometry in the
  shipped scenes is characters of ~2,000-6,500 triangles; Glowmere's 16-28 changed characters
  rebuild in 9-14 ms together. Not worth giving up bit identity for.
- **Embree's own dynamic scenes (`RTC_SCENE_FLAG_DYNAMIC`).** Embree's two-level builder skips
  unmodified geometries, but it would still see every Tree of Life mesh as modified (world space),
  and it hides the decision that has to be tested.
- **Split the top level into static and moving instances.** Would save most of Glowmere's remaining
  ~20 ms top-level rebuild, but which instances are "static" is a fact about history, and the
  structure would stop being a function of the snapshot alone. Not taken.
- **Change detection by version counters from the scene.** Cheaper, and it can miss: any writer
  that forgets to bump a counter produces a stale BVH, which is silently wrong output. Exact
  comparison costs a few milliseconds and cannot miss.

## Consequences

### Correctness

- **Reuse against the control arm: byte-identical EXRs.** Tree of Life 8.0-8.5 s and Glowmere
  12.0-12.5 s, 12 frames each at 320x180, 4 spp, depth 3; and 24 frames of each at 128x72, 1 spp,
  across three repeats of each arm. Every frame `cmp`-identical between `--pt-rebuild-bvh` and the
  default. No tolerance was needed, so none is quoted.
- **Why it can be exact:** the structure is a function of the snapshot alone, a kept child was
  built from bit-identical input by the same builder, and a rebuilt one goes through the first
  frame's code. Nothing a frame traces depends on which earlier frames ran.
- **Determinism (ADR-360):** two runs of the same range are byte-identical, and on the Tree of
  Life a range started at 8.25 s writes the same six frames as the full run's frames 6-11.
  **Glowmere does not reproduce mid-range -- on `main` either**: `main`'s own 12.25-12.5 s run
  differs from its full run's frames 6-11 in all six. That is scene evaluation --
  presumably the stateful characters and drifting scatter; not investigated -- and not the BVH: the reuse and rebuild arms of the mid-range run
  are byte-identical to each other. Recorded, not fixed here.
- **`test_pathtrace_bvh_reuse.cpp`**: a moved entity, an object-space deformation inside a shared
  group, a re-posed rig and one moved scatter instance each land at the new place, and each has a
  control through `BvhReuse::TrustStructure` that lands at the old one. With change detection
  forced off in the source, 5 of the 7 cases fail (22 assertions: rays at the old place, stale
  hits against a fresh build, and radiance differing from the rebuild arm); the two that still
  pass are the unchanged-frame case and appear/disappear, which change the structure and are
  caught by it.

### Against `main`, the output is not byte-identical, and that is the object-space BVH

The world-space triangles `main` intersected are now intersected as object-space triangles under
an instance transform, which moves t, u and v in the last bits. Measured on first hits alone
(1 spp, depth 1, AOVs): on Glowmere 46 of 57,600 depth samples differ, by at most 4e-7 relative, and
no object id changes. On the Tree of Life -- whose meshes sit under a rotated, scaled, translated
transform -- 7,277 depth samples differ, all but 2 within 1e-5 relative; 1 pixel's object id
changes (a grazing edge). In a Monte Carlo path those last-bit shifts decide some shadow and bounce
branches differently, so a 4-spp beauty frame differs from `main` in 19.6% of pixels on the Tree of
Life and 0.47% on Glowmere, at noise amplitude. That is the cost of the one decision that makes reuse
possible on the Tree of Life at all, and it is a change of noise, not of the picture.

### Speed

Minima over three repeats (ADR-170). The machine was shared: load averages 13-140 across the runs,
so every number is contended; the loads are in the run logs and the best runs were at 13-24.

| | Tree of Life (3.1M tri) | Glowmere valley 2 (352K stored tri, 67,799 instances) |
|---|---|---|
| BVH, frame 0 (full build) | 834 ms | 60 ms |
| BVH, frame N, rebuild every frame (control) | 714-879 ms, median 793 | 54-64 ms, median 58 |
| BVH, frame N, reuse | **20-22 ms**, median 21 | **21-35 ms**, median 31 |
| of which: exact compare | 13-17 ms | 0.5 ms |
| of which: children rebuilt | 0 ms | median 11 ms (16-28 characters) |
| of which: top level | 3 ms | median 19 ms (67,799 instances) |
| BVH over a 24-frame range, sum | 1.3 s (was 19.6 s) | 0.75 s (was 1.4 s) |
| wall, 24 frames at 128x72, 1 spp: `main` | 54.2 s | 10.6 s |
| wall, same: reuse | **38.2 s** | **9.9 s** |

So a 240-frame Tree of Life shot spends about **6 s** on BVHs (834 ms + 239 x 21 ms) instead of about
3.2 minutes (the
docs' 6.7 minutes was a contended 1,669 ms figure; frame 0 measured 782-851 ms here, on `main` and
on this branch alike). On Glowmere the remaining cost is the top level, which is rebuilt whenever
any instance moves -- see the alternatives for why it is not split.

**Trace speed is unchanged within the noise.** Single frames, `main` against this branch,
interleaved: Tree of Life 480x270 at 8 spp, render minima 37,809 ms and 35,830 ms; Glowmere at
16 spp, 3,868 ms and 3,983 ms (+3%, inside the spread of either arm at load 38-74). Grouping by
transform is what keeps the Tree of Life's canopy a single-level BVH.

### Memory

Embree's own allocations, from its memory monitor: Tree of Life **880 MB held, 1,000 MB peak** on a
full build, on `main` and on this branch alike; a reused frame holds the same 880 MB with **no
build peak at all**. Glowmere: 53 MB held, 64 MB peak on `main`, 55-60 MB on this branch. (Metal's
build, for scale, measured 546 MB plus 600 MB scratch.) Process peak RSS over a 3-frame run: Tree of
Life 12.53 GB on `main`, 12.74 GB reusing -- the object-space copy in the snapshot, 12 bytes per
vertex. The difference is that the BVH now lives between frames rather than being freed and
reallocated each one; its size does not change.

### Found on the way and fixed: a sequence kept every frame's AOVs and wrote none

`TraceFrameSource` pushed every frame's full framebuffer -- radiance, albedo, normal, emission,
motion, depth, id -- onto `aovFrames_` whenever `writeAovs` was on, which is the default, "so the
writer can emit the multi-layer EXR beside the beauty frame". No writer ever read it. So a range
kept seven buffers per frame until the job ended and put no AOV on disk. Measured on Glowmere at
960x540, 1 spp, with `--pt-aovs`: `main` peaks at 4.59 GB RSS over 6 frames and **6.09 GB over 48**,
about 36 MB per frame (about 140 MB at 1080p), with **0 AOV files written**. This branch peaks at
4.55 GB and 4.58 GB and writes 6 and 48 files.

The intent was plain from the comment, so the AOVs are now written the way it described: each
frame's multi-layer EXR is written in `submit` as soon as the frame is traced, then released with
the frame. For an EXR sequence it goes beside the beauty frame (`frame_000012.aovs.exr`); for a
video it goes in a sibling `<movie>_aovs/` folder (`app::traceSequenceAovFile`). With denoising on,
the feature buffers are folded to one sample along with the radiance, because otherwise the
`sampleCount = 1` that denoising sets would make every feature resolve to N times its value.
`TraceSequence::heldFrameBytes()` reports the finished-frame pixels the sequence is holding.
`test_trace_sequence.cpp` checks that it stays flat over six frames and that all six AOV files
exist. Against the old code it failed 11 assertions: the held bytes grew 72 KB -> 513 KB, and all
six files were missing.

### Found on the way, not fixed here

- `pathtrace::TraceJob` (single frame) has the denoise + AOV scaling defect described above: it sets
  `sampleCount = 1` after denoising and then writes feature AOVs that were accumulated over N
  samples, so albedo, emission and motion come out N times too large. This build has no OIDN, so it
  was not exercised.

- The path tracer renders the Tree of Life unbent: ADR-360's mesh wind is a GPU vertex stage and
  never reaches the snapshot.
- The Tree of Life's 4 procedural objects (4,492 instances) hold still while the island they may
  belong to drifts; whether the realtime renderer agrees was not checked.
- `tree-wood`'s four primitives each carry the full 288,560-vertex buffer, so those vertices are
  stored four times over in the snapshot and in the BVH.
