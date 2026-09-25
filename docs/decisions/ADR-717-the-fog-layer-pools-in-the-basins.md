# ADR-717: the fog layer pools in the basins

Status: accepted. Date: 2026-09-25. Builds ADR-715's proposed follow-up. The owner's ruling: "POOL".

## Context

ADR-715 built `fogGroundFollow`, which puts the layer's top at `fogHeight + follow * ground(x, z)`.
The fog's depth at a point is then `fogHeight - (1 - follow) * ground`. Raising the follow
**reduces** the contrast between valley and ridge. At 1 the fog is a blanket of constant thickness
that climbs the hills. The ADR proposed a fix: measure the layer from a **low-passed** ground, so
fog collects in valleys and thins over ridges. The owner ruled to build it.

ADR-705 and ADR-715 set the constraints:

- One layer has three readers: the march, the surface integral `applyFog`, and the particle
  estimate. They must describe the same air.
- `fogShape` has no free lane (z is the follow and w is Horizon Density).
- ADR-441 allows no shims, and ADR-421 allows no control that does nothing.

## Decision

### The basin bake

- **What it is.** `TerrainHeightField::basin` is the height grid passed through a separable
  Gaussian with σ = `world::kTerrainBasinSigma` = 24 m. The kernel is truncated at 3σ (±72 m) and
  renormalised, and the grid's edge samples are repeated outward. The half-maximum width is 56 m,
  inside the 50–100 m range ADR-715 proposed.
  - In a valley the basin sits above the floor. On a ridge it sits below the crest.
  - On ground that is linear across the kernel (a plain hillside) the basin *is* the ground, so a
    slope is not treated as a basin.
- **When it is made.** `world::poolTerrainHeight` runs inside `bakeTerrainHeight`: the same cache
  miss, cached under the same key, reused by pointer. Nothing runs per frame.
- **How it reaches the GPU.** The upload is now **RG32F**: r is the ground, g is the basin. The
  binding (group 0 binding 12, particle render group binding 12) is unchanged, so no new bind-group
  entry was needed.
  - `terrainGroundAt` still reads `.r` and is untouched.
  - `terrainGroundPairAt` reads both channels from the same four texel loads.
  - The CPU twins are `TerrainGround::groundAt`, `basinAt` and `referenceAt`.

### `fogPooling` (0..1, default 0)

- **Semantics.** The layer's top is `fogHeight + mix(follow * ground, basin, pooling)`.
  - 0 is ADR-715's layer.
  - 1 measures `fogHeight` from the basin, whatever the follow is. The layer's surface is then the
    basin lifted by `fogHeight`: nearly flat across each basin, deep over valley floors, and below
    the crests on ridges.
- **Where it lives.** `scene/fogPooling` is an ordinary parameter in the fog group, hard-clamped to
  0..1. Both serialisers keep it:
  - the scene file writes the key only when it is not 0;
  - the project document carries it through `parameters`.
- **UI.** The Environment panel shows a "Pool in valleys" row after "Follow ground". The row says so
  when the scene has no terrain, and says that Follow ground has no effect at 1.
- **No blur-radius control.** See "The basin width" below.

### Does `fogGroundFollow` still make sense? Yes, as a different look, and it is inert at pooling 1

- **For "fog sits in valleys", pooling supersedes follow.** The owner's original ask is pooling.
  Follow cannot deliver it (ADR-715's finding), and pooling does (measured below).
- **Follow still describes a look nothing else gives:** a ground mist of constant thickness that
  hugs every surface, including the hilltops. It is also the only way to get fog in a valley that
  lies wholly above the flat plane *without* thinning it on the rims.
- **They are one reference, not two layers.** Pooling blends *from* whatever follow describes
  *toward* the basin, so between 0 and 1 follow sets what pooling starts from.
  - At pooling 1 the follow does nothing, by construction. The panel says so rather than leaving a
    silent no-op (ADR-421).
- **The rejected alternative: `follow * mix(ground, basin, pooling)`.** It keeps follow alive
  everywhere but makes pooling do nothing at follow 0, which is every shipped scene. That is the
  worse no-op of the two.
- **Follow is not cut.** ADR-441's rule is about removing effects the owner rejected. Follow is
  owner-ruled (ADR-715), and its look is still distinct.

### The readers and the lane

- **One new lane:** `FrameUniforms::fogPool`, appended at the end (x = pooling, yzw = 0).
  - The **march reads the frame's lane** (`frame.fogPool.x`) and does not copy it into
    VolumeUniforms, so the surface fog and the march cannot be handed different values.
  - `ParticleUniforms` appends its own `fogPool` after `anchors` (the particle pipelines bind no
    frame group).
  - Both `static_assert`s count the new vec4.
- **Merge note for the lead.** Effect Library Wave 2 appends `starsA/B/C` to `FrameUniforms` as well.
  Keep both appends, in the same order in `scene_renderer.hpp` and `common.wgsl`, and add both
  terms to the `static_assert` sum.
- **Every reader takes the ground branch** when `follow > 0 || pooling > 0` and a terrain is bound.
  - `fogGroundReference` returns ADR-715's `follow * terrainGroundAt(...)` when pooling is 0, so
    ADR-715's frame is reproduced bit for bit, not merely to within rounding.
  - Otherwise it returns the mix, computed from the pair read.
- **The CPU zeroes the lane** without a terrain, or for a hand-built field that has no basin
  (`TerrainGround::poolingLane`). The shader's `terrainMap1.w` gate still holds on its own.
- **The surface integral is unchanged.** It uses ADR-715's eight chords against a reference that is
  now piecewise linear, integrated with the same closed form.

## Evidence

The sheets are in the session scratchpad (`pooling/`) and in
`~/Desktop/av-gen-review/8-terrain-fog/pooling/`. They are 960×540 at t = 2 s, with arms at pooling
0 / 0.5 / 1.

### Pooling 0 changes nothing

The sequence hash was compared against the pre-change binary, which was run with its own copy of
the pre-change shaders.

| Frame | Sequence hash | Pre and post |
|---|---|---|
| Glowmere t = 60 | `e1c8f6fe9d303416` | equal |
| Volumetric lab | `0df89e3716561152` | equal |
| Glowmere with `scene/fogPooling` 0 in its parameters | `d3d1bebfa935b7e7` | equal |

The old binary ignores the unknown parameter in the third row.

### It pools, measured

The terrain is ridged ground on a 0.25 slope, so the valley at x = 50 lies **above** the ridge at
x = −100. Each value is the metres of full-density air in the column from the ground to 80 m above
it, integrated from the march's own term:

| Arm | valley −50 | valley 50 | ridge −100 | ridge 0 | ridge 100 |
|---|---|---|---|---|---|
| flat | 34.8 | 9.8 | 17.3 | 0.12 | 0.00 |
| follow 1 | 7.28 | 7.28 | 7.28 | 7.28 | 7.28 |
| pooling 0.5 | 26.1 | 13.6 | 7.19 | 0.25 | 0.01 |
| pooling 1 | 17.5 | 17.5 | 0.51 | 0.51 | 0.51 |

- **Pooling 1:** every valley holds 34× the air of every ridge, including the high valley against
  the low ridge.
- **Flat plane:** the high valley holds half the air of the low ridge.
- **Follow 1:** the blanket. Every spot holds the same column.

### The readers agree

- **On affine ground they agree exactly.** The basin of a plane is that plane, so the chords are
  exact there: within 0.02 m of air over 432 pooled cases, as ADR-715's own test requires.
- **On ADR-715's valley fold** the worst gap is 1.5% (pooling 0.5), 1.1% (pooling 1) and 0.7%
  (follow 1 + pooling 0.5), under ADR-715's 5% bound. Follow alone is 0.65%.
- **On rougher test terrain the gap is large, and it is ADR-715's chord rule, not pooling.** The
  ridged and rolling test grounds have 100–150 m features. Eight chords over a 260–280 m ray are
  33–35 m each.

  | Terrain | follow 1 alone | pooling 0.5 | pooling 1 | follow 1 + pooling 0.5 |
  |---|---|---|---|---|
  | ridged | 12.8% | 10.8% | 7.5% | 5.6% |
  | rolling | 17.0% | 7.2% | **39.8%** | 31.5% |

  The worst rolling cases are rays that **graze** the layer's top, where a chord's metre of sagitta
  is much of the air the ray sees. In absolute terms that is 3.8 m of air at pooling 1, against
  6.0 m for follow alone.
  - This is recorded, and regression-bounded at 0.15 (ridged) and 0.5 (rolling).
  - The remedy is more chords, and it applies to both controls. 16 pieces would cut the sagitta 4×.
    It is not taken here: it changes ADR-715's follow frames and doubles the ground reads.

### What the sheets show, looked at

- **Glowmere as shipped (surface fog only, dark fog colour).** Establishing shot: 84% of pixels move
  by more than 2 levels at 1, and 68% at 0.5. By eye it is subtle: the far hillside hazes slightly.
  The night fog colour (0.10, 0.18, 0.32) barely reads against the ground at any setting. The oblique
  and top-down aerials show the same.
- **Fog lab on the Glowmere terrain** (compact layer, top 3 m, density 0.06, a pale fog colour, march
  200 m plus the surface pass). At 1 the layer lifts onto the lower, concave flank of the big hill
  behind the fence, and the near river floor clears slightly. On Glowmere's relief that is what "the
  neighbourhood's mean ground" means. The river valley is not visibly deeper in these framings,
  because the flat plane at 3 m already fills it.

### The basin width

σ = 64 m was rendered as an experiment and was **not** shipped (sheet 13). Mean luminance of the
fog-lab establishing frame, where more fog is brighter:

| Region | pooling 0 | pooling 1, σ = 24 | pooling 1, σ = 64 |
|---|---|---|---|
| Hill flank | 29.9 | 66.3 | 93.3 |
| Far valley | 87.4 | 87.5 | 96.8 |
| Near floor | 72.8 | 66.8 | 61.3 |

A wider kernel is **not** more pooling. It brings the summit into the mean, so the flank hazes
further. 24 m is kept.

A width control does not earn its place: it is a re-bake, not a per-frame value, and neither width
turns the method into "fog lies only in low ground". See Consequences.

## Tests

- **CPU** (`test_terrain_height.cpp`):
  - A flat map has a flat basin.
  - A V valley's basin sits `k σ √(2/π)` above the fold, and equals the slope away from it.
  - A ridge's basin lies below its crest.
  - The bake pools at the shipped width, and a different width gives a different basin (the
    control).
  - `referenceAt` is ADR-715's follow exactly at pooling 0, and the basin at 1.
  - An unpooled field gets lane 0.
  - `scene/fogPooling` reaches the environment, clamps, and round-trips the scene file. The key is
    absent at 0.
  - It survives the project document after two frames, with a fresh-load control.
  - The Environment panel path exists.
- **GPU, maths** (`test_height_fog_gpu.cpp`):
  - At pooling 0 the shipped functions are memcmp-equal to ADR-715's two functions, restated in the
    kernel. At 0.5 more than half differ (the control).
  - On affine ground: exact agreement, basin parity with the CPU twin, and the profile against a CPU
    restatement at the `referenceAt` altitude.
  - On curved ground: the table above, bounded.
  - The column table above.
- **GPU, plumbing** (`test_terrain_fog_gpu.cpp`):
  - Without a terrain, pooling is byte-identical at 0.5 and 1, and the lane is 0.
  - At pooling 0 with follow 0.6, a nonsense basin gives a byte-identical frame. At 1 it moves the
    frame (the control).
  - With a terrain, each reader on its own (surface only, march only) moves more than 10% of the
    frame, towards more fog.

**Break demonstrations.** Each break below turned its case red, and each was restored.
- **A:** the pair read uses `.r` for the basin. The column test fails, because the pooled layer
  becomes the 7.28 m blanket.
- **C:** the march passes 0 in place of `frame.fogPool.x`. The march arm finds no movement.
- **D:** the mix uses a fixed 0.5. The CPU-restatement checks fail (53).
- **E:** `applyFog` ignores the lane. The surface arm finds no movement.
- **F:** the scene serialiser drops the key. The round-trip case fails.

**What the tests cannot see.**
- **B did not fail.** Taking the pair branch at pooling 0 (`pooling < 0.0`) gives memcmp-equal
  results: `mix(a, b, 0)` is exact and the pair's r channel is the same arithmetic as
  `terrainGroundAt`. So bit-identity at 0 does not *depend* on the early return. The early return
  is kept because it saves the second channel's use and states the rule.
- **The particle estimate has no test of its own.** It calls the same `fogGroundProfileAt` with
  `ParticleUniforms::fogPool.x`, which is filled from the same `poolingLane`. The size of that
  struct is held by its `static_assert`. Nothing asserts that the particle pass's fog *moves*.

## Consequences

- **What pooling is, and what it is not.** The basin is the neighbourhood mean of the ground. Fog
  therefore gathers wherever the ground is concave: valley floors, but also the foot of a big hill.
  It thins wherever the ground is convex. That is what the ruling's "measure from a blurred ground"
  specifies, and it pools every valley, including ones above the flat plane (measured above).
  - It is not "fog lies only in the low ground". That look needs a **fill**: a depression-fill
    (priority-flood) water level, which is flat across each closed basin and equal to the ground
    wherever the ground drains.
  - A river valley that drains would get no fog from a fill. The two constructions answer
    different questions, and the owner should see these sheets before choosing whether a fill is
    wanted.

- **Cost.** The ground branch now runs when pooling is on as well. Each pooled ground read is the
  same four texel loads as before, now reading two channels. The bake adds a 73-tap separable blur, threaded like the
  sampling: Glowmere's whole bake logged 15.5–31 ms with the blur, on a machine at load average 130, so
  that is not a controlled comparison with ADR-715's 21 ms. The texture doubles to about 800 KB on Glowmere.
- **The basin width is fixed at 24 m.** A world whose valleys are an order of magnitude wider or
  narrower would want a different σ. Such a world does not exist yet, and at that point σ becomes a
  terrain-node setting that is part of the bake key. It should not be a fog parameter, because
  changing it means a re-bake.
