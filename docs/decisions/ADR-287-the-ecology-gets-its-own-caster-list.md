# ADR-287: The ecology gets its own caster list, and it is not the camera's

**Status:** Accepted
**Date:** 2026-09-18
**Context:** ADR-265 decision 3, which recorded this gap, measured it and routed it. ADR-046 (the
entity second cull), ADR-029 (the GPU cull), ADR-051 (one indirect buffer), ADR-108 (material parts
share a cull), ADR-112 (the shadow range).

## Context

A shadow caster list is a second cull. This engine had three caster paths and only two of them knew
it: entities got ADR-046's second pass over the objects the camera rejected, terrain chunks got a
per-frame distance test, and procedural instances got nothing. `ProceduralRenderer::update` built
**one** `FrustumPlanes`, from `scene.camera`, and `drawShadow` issued its indirect draws against the
args that cull wrote.

ADR-265 measured it on Glowmere multicam: **62,284 procedural instances, 496 surviving the camera
cull, and 496 drawn into the shadow maps.** One number where there should be two.

## Decisions

### 1. Two lists, one classification, one compaction

The Shadow Lab proposed "another cull dispatch per object against the union of the cascade frusta,
its own indirect-args region and its own per-level bind groups". The dispatch is the expensive third
of that and it is not necessary: the per-record work -- the world sphere, the distance, the projected
radius, the ladder -- is identical for both lists, and only the volume test differs.

So `cs_cull_classify` runs **once** and writes two verdicts. `lodIndex` holds the camera
classification at `[0, count)` and the shadow one at `[count, 2 * count)`; the compaction's level
axis runs `0..2 * lodCount - 1`, with levels at or above `lodCount` reading the second half. The
classification's three rejections are separated into the half both lists share -- distance, screen
size, ADR-038's density thinning -- and the two volume tests, and the rung is computed once from the
camera and used by both.

Two properties follow from the rung being shared, and both are load-bearing:

* **an instance in both lists is on the same rung in both**, so a caster is never rasterised at a
  different mesh from the one the frame shows -- which is what keeps a shadow the shadow of the
  thing on screen;
* the caster list is **neither a subset nor a superset** of the camera list. An instance behind the
  camera that a cascade contains is a caster and not a draw; an instance the camera sees at 200 m is
  a draw and not a caster, because the cascades stop at 77 m (ADR-112) and nothing past that can
  write a texel.

### 2. The volumes are the frame's, not the object's

`shaders/cull.wgsl` gains a second uniform at group 0 binding 7: the frame's shadow views as up to
eight world-space plane sets. Frame-global, because the volumes are a property of the frame -- one
784-byte buffer written once, instead of 768 bytes appended to every object's own uniform. A caster
is kept when **some** view contains it, because the shadow passes draw one list into every view and
each view rejects what it cannot see on its own.

`SceneRenderer` hands over the plane sets it already built for the entity second cull rather than
fitting a second set beside them, so the two caster rules cannot disagree about a volume (§37). An
empty span is how a frame says it has no shadow maps, and the ecology's shadow draws are then not
recorded at all.

### 3. One indirect buffer, two slot ranges

The caster list's indirect args and stats go to `kShadowSlotBase + slot` in the same two buffers as
the camera list's. ADR-051 measured the cost per distinct indirect **buffer** a pass reads -- eleven
buffers cost 1.2 ms more than one for the same 72 draws -- and this frame reads one in five passes.
A slot is an offset and costs nothing per draw.

The draw bind groups are the part that genuinely doubles: a group binds **one** level's slice of the
visible list, so the second list needs its own `kMaxLodLevels` of them per object. They are built
for any culled object rather than only for a caster, because whether an object casts is a per-frame
property and these are cached across frames.

### 4. The CPU proof is a different proof

`DrawItem::fullyCulled` -- the whole-object rejection that lets the renderer skip an object's
dispatches and draws before encoding them -- was consulted by every pass, and that single line is
what made a tree the camera cannot see stop casting. There are now two: `objectFullyCulled` against
the camera and `objectFullyCulledForShadows`, which requires **every** shadow view to reject the
whole object, since one that does not is a view something casts into.

### 5. `shadowCullLodLevel` is the decision, and it is device-free

`rendering::shadowCullLodLevel` is the CPU statement of the rule, mirrored by the shader, so the
invariant can be tested without a device -- and it reaches the ladder by calling `cullLodLevel` with
its rejection block switched off rather than by copying the loop, because a ladder written twice is
one that disagrees with itself the first time somebody edits a copy.

### 6. Where the second list begins is this frame's rung count, not the high-water mark

Found by reading the code this ADR had just written, and it needed a second attempt at a probe
before it was a finding rather than a suspicion.

The two lists share one `visible` buffer and the caster list's slices begin at the object's rung
count. The cull buffers are grow-only, so a rung count that goes **down** left the shader laying its
lists out at `counts.y` -- this frame's count -- while the draw groups read `cullLodCount`, the
high-water mark. The shadow draw then read a slice nobody had written this frame, and the object
stopped casting: the same symptom this whole ADR is about, reintroduced by the machinery that fixes
it.

`cullLodCount` is now this frame's number and a separate `cullLodCapacity` carries the grow-only
allocation, so the two cannot disagree; a change in either direction rebuilds the draw groups.

**The first probe could not fail, and that is worth writing down.** It used a one-record object,
and the compacted list of one record is `[0]` at every slice -- including an untouched one, because
the buffers are zeroed when allocated. The probe read the right answer out of the wrong place and
reported no defect. The second carries two records and arranges for the right answer to be `[1]`, a
value a zeroed slice cannot produce, and the far record is a thousand metres away so drawing it
instead is visibly nothing. Measured, ground darkening under the caster: **0.428 settled, 0.428 on a
second frame at the same rung count (the control), and -0.001 after dropping from four rungs to
two.** After the fix, 0.428 in all three.

Glowmere is byte-identical across this fix and its counters do not move, because nothing there
changes its rung count at runtime. It is reachable by a live edit of `lod.lodCount`, which is what
the authoring panel does.

## What it costs, measured

Glowmere multicam, frame 60, 1280x720, `--tier realtime`, deterministic to the byte over repeats.
The arm and its control are the same binary with `drawImpl`'s three shadow lines switched, so every
other change in the tree is held constant.

| | before | after |
|---|---|---|
| ecology instances the frame contains | 62,284 | 62,284 |
| surviving the camera cull | 485 | 485 |
| **drawn into the shadow maps** | **485** (the same number) | **288** |
| ecology shadow submissions, over three cascades | 3,396 inst / 652,659 tris | **2,070 inst / 428,601 tris** |
| indirect draws recorded | 266 | 266 |
| camera pass | unchanged | unchanged |

**The caster list is smaller, and the reason is the whole finding.** A shader-only probe that keeps
only the casters the camera rejected puts the split at **208 gained and 405 shed**: 208 instances
that cast nothing before now cast, and 405 instances the camera keeps are past the 77 m the cascades
reach and were being drawn into every cascade to be clipped. 80 instances are in both lists.

So the honest headline is not "a correct shadow pass draws far more casters". On a scene whose view
distance is several times its shadow range it draws **39% fewer**, and the 208 it adds are the ones
the complaint was about.

**And the delivered picture does not move.** Six frames across the multicam run (31, 61, 91, 121,
151, 181) are byte-identical across the fix. That is not a void comparison: the same capture path,
on the same frames, changes its hash when ADR-286 lands. It means two things, and the second is a
limitation rather than a result:

* nothing that was in the picture is missing -- the 405 shed casters were provably writing nothing,
  and the counters and the pixels agree about that;
* none of the 208 gained casters throws a shadow into shot **on these six frames**, because
  Glowmere's key is steep enough that an off-screen caster's shadow is off-screen too. The run of a
  shadow is `height / tan(elevation)`, and Glowmere's ecology is short. A scene with a low sun, or
  tall scatter near the frame edge, is where this shows.

The rendered proof is therefore built rather than found:
`tests/rendering/test_shadow_lab_gpu.cpp` "a procedural instance the camera cannot see casts into
shot" puts a 6 m box 18 m up and 46 m off the left of frame, where 34 degrees of elevation runs its
shadow 26.7 m back into the middle of the picture. Before: the ground reads 0.7448 against an open
floor of 0.7440 -- the arm is bit-identical to a control with `castsShadow` off. After: 0.4255, a
43% darkening, with the control unmoved.

## Consequences

`AVGEN_FRAME_COUNTERS=1` prints `ecology cull: 62284 records, 485 visible, 288 casting`. Those last
two were one number and no counter in the frame could have said so.

`ProceduralStats` gains `shadowInstances`, `shadowLodCounts` and `shadowCullObjects`; `CullCounts`
gains `shadowVisible` and `shadowLod`; `readIndirectArgs` and `readVisibleIndices` take a
`shadowList` flag, because the bytes `drawShadow` addresses are now different bytes.

The per-object cull buffers double: `lodIndex`, `blockSums` and the visible list all carry two
lists. The compaction's reduce/top/scatter dispatches double for an object that casts and is inside
a cascade; the classification does not. An object no shadow view can reach pays exactly what it paid
before, which on an outdoor frame is most of the ecology, because the cascades stop at 77 m.

`tests/unit/test_shadow_lab.cpp` "a procedural instance a cascade can see is submitted to the shadow
passes" loses its `[!shouldfail]` tag. Its seven fixtures are a tree off the left of frame, a tree
above the top of it, a 4 cm-thick panel, an off-origin cap on a non-uniform scale, a tree in the
middle of frame, and **two controls that must come back rejected** -- one past the cascades and off
frame, one straight down the lens at 150 m. Not one of them is a centred cube, which is the shape
that hid ADR-199's cull-radius bug for as long as it hid it.
