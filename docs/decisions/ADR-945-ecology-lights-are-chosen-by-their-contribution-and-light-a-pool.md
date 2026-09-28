# ADR-945: Ecology lights are chosen by what they add to the picture, and each lights a pool

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-053 (the light a glowing ecology casts), ADR-905 (`scene/ecologyLight` as a parameter)
**Implemented by:** `scene::glowPool`, `scene::chooseEcologyLights`, `scene::layerGlowStrength`
(`src/scene/ecology_lights.{hpp,cpp}`); `Composition::updateEcologyLights` (`src/scene/composition.cpp`)
**Tests:** `tests/unit/test_ecology_light_selection.cpp` (`[adr945]`, CPU);
`tests/rendering/test_ecology_pools_gpu.cpp` (`[adr945]`, GPU)

## Context

The owner, on GV3: the tiny glowing mushrooms pulse to the beat, but in the wide shots they are
specks. The owner chose "more light on the ground": each glowing patch should throw a pool of glow on
the ground around it, and the pool should pulse with it.

ADR-053 already makes glowing scatter layers cast light. Each 9 m cell of a glowing layer becomes one
point light, and its power follows the layer's gain (ADR-905), so the light already pulses with a kick
route on the layer. The gv3-int stream could not make that light show up in the wides with data alone.
It set `ecologyLightRange` to 300 m and stepped `scene/ecologyLight` up to x10. Even so, in 41.1, 113.1
and 117.1 the ground changed only x0.95–1.06 between a kick and 300 ms later. Only the fungi patch
nearest the lens got a violet pool. Reading `updateEcologyLights` showed three causes:

1. **Nearest first.** About 200 lights were taken in order of distance, whatever each one's power. So
   the faint 0.035–0.06 glow of the trees, ferns and grass beside the camera filled the budget. In
   GV3's 41.1 the old rule lit the same set at a 120 m range and at 300 m: both renders have sequence
   hash `08c309155c437d45`.
2. **A speck, not a pool.** Each light stood at half its layer's height, which is 0.14 m for a 0.28 m
   mushroom. A point light at height h lights flat ground as h / (h² + r²)^1.5, so all of its light
   fell inside about half a metre. At 200 m that is less than a pixel.
3. **Its reach was fixed** at 4x the cell's spread, and nothing in the UI named it.

The other route, an emissive ground term, runs into the known defect that `groundGlow` does nothing
under an authored ground program (`paintedGround2`).

## Decision

**Choose by contribution to the picture.** Each patch scores its power (base cluster power × the
node's base boost × the layer's *base* gain) over its squared distance from the eye. The distance is
never taken as less than the light's own range. A patch scores 0 when its whole reach is outside the
view frustum (planes at the viewport's aspect) or when it is farther than the distance limit. The
`budget` highest scores are chosen, ties broken by cluster order. Lights are written highest first,
so a crowded distant froxel (32 lights at most) keeps the pools that matter most.

**Don't pop.** A light's intensity is multiplied by a weight that rises from 0 at the budget's cut to
1 at 25% above it, and falls to 0 over the last 15% of the distance limit. A pool that crosses either
line as the camera moves fades in or out instead of switching.

**The beat moves brightness, not the choice.** The threshold and the ranking read the layer gain's
*base* value. Intensity still reads the *final* value. So a kick route that quadruples the fungi
brightens their pools without reshuffling which patches have lights on every beat.

**Light a pool.** `glowPool` stands the light at half the pool's radius R over the patch's ground
point, with a window range of 2R. At the pool's edge the irradiance is 9% of its centre, and the
window makes the tail soft. R = max(reach, the patch's spread, twice the organ height), so a tall
glowing tree still lights from its crown. The intensity is unchanged by where the light stands: it is
the same power spread wider. A bigger reach makes a bigger, fainter pool, and `scene/ecologyLight`
stays the brightness.

**Skip faint layers.** A layer casts no pools when its glow strength is below the threshold. Glow
strength is emissive intensity × the colour's peak × (1 − sparsity) × base gain, and it does not
depend on how much of the layer there is. A layer whose final gain is 0 casts none either.

**Everything is a pure function of the frame:** the camera, the clusters and the parameters. Nothing
remembers the previous frame, so a seek lands on the lights that play reached (ADR-089).

**Three new controls**, all under Parameters → `scene` → **glow-pools**, and in the World panel under
Atmosphere → environment → Inspector → glow-pools:

| Path | Label | File key (`environment`) | Default |
|---|---|---|---|
| `scene/glow-pools/reach` | how far each glow pool spreads on the ground (m) | `ecologyPoolReach` | 6 |
| `scene/glow-pools/faintest` | faintest glow that casts a pool (dimmer plants cast none) | `ecologyPoolFaintest` | 0.1 |
| `scene/glow-pools/distance` | farthest glow pool from the camera (m) | `ecologyLightRange` (existing) | 120 |

The brightness stays `scene/ecologyLight` ("light cast by glowing plants and fungi"). A save writes
all three bases whenever `ecologyLight` > 0.

**Not ADR-946.** The brief allowed a second decision: fix `groundGlow` under authored ground programs
so that a ground term fed by the glow clusters could carry the pools. It is not needed. The light path
reads in GV3's 300 m wides (below), so the `groundGlow` defect stays as it was, still warned about at
load.

## Consequences

**GV3, measured** on a scratch copy of gv3-int's round r6 (`build/ecolight-gv3/`, travel beam off,
960×540). The metric is gv3-int's: the ground (the lower 55% of the frame), at each kick (+60 ms
fungi delay, sampled +50 ms) against 300 ms later. "Pools" is the same frame with the ecology lights
minus the same frame rendered with `scene/ecologyLight` 0. Renders are deterministic, so that
difference is exactly the ecology lights' light.

| Wide | Old engine, GV3 as is: ground kick/trough · pools' share at the kick | New, GV3 as is (range 120) | New, range 300 | New, range 300, faintest 6.5 (fungi only) | New, range 300, faintest 6.5, arc x3 |
|---|---|---|---|---|---|
| 41.1 | x1.039 · 0.0% | x1.057 · 4.5% | x1.061 · 6.0% | x1.064 · 5.1% (pools x2.05 on the kick) | **x1.082 · 10.9%** (pools x1.77) |
| 113.1 | x1.035 · 3.1% | x1.072 · 19.9% | x1.078 · 22.3% | x1.110 · 17.3% (x1.93) | **x1.149 · 34.7%** (x1.70) |
| 117.1 | x1.032 · 1.8% | x1.051 · 6.2% | x1.057 · 7.9% | x1.060 · 7.1% (x1.99) | **x1.077 · 13.7%** (x1.68) |

With the lights off, the ground's own kick/trough is x1.038, x1.034 and x1.026 (the elder, the
particles and the aurora). In the old engine the pools added nothing over that. The share of ground
pixels the pools raise by more than 0.05 at a kick rises from 0.1%, 3.1% and 3.2% (old) to 23.6%,
57.2% and 30.4% (fungi only, x3).

**Existing scenes whose look changes.** These are all the scenes with `ecologyLight` > 0: the
Glowmere family (`glowmere-valley-2`, `-2-song`, `-2-multicam`, `glowmere-stylized`,
`glowmere-atmospherics`, `_pre-defects`), the nine foot-IK and motion-matching labs that use its
world, and `terrain`, `moonrise`, `_tier1` and `_tier1big` (gain 0.006). In each:

- the ground glow moves from specks at the patches beside the camera to soft pools across the view;
- layers glowing below 0.1 (Glowmere's trees at 0.035, ferns and grass at 0.0615) no longer cast light;
- patches behind the camera no longer take lights.

A scene that wants the old reach of the faint layers sets `ecologyPoolFaintest` to 0. There is no
compatibility mode (ADR-441). No existing test pinned the old placement: the full CPU suite and the
emission and reactivity GPU families pass unchanged, with no test re-baselined.

**Open:** the pools' kick response is the lit layers' mix. Only the fungi pulse on the kick in GV3,
so pools from the shelf fungi, flowers and beacons dilute it (x1.3 against x2.0 fungi-only). A per-layer
pool gain would separate "how much this layer lights the ground" from its surface glow. It is not
built, because `faintest` already lets a film choose which layers cast pools.
