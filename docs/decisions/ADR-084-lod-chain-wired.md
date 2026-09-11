# ADR-084: The LOD ladder is built by meshoptimizer, and it has to descend

Status: accepted
Date: 2026-09-11

## Context

ADR-078 built `assets::buildLodChain` over meshoptimizer, measured a calibration for vegetation, and
ended: *"Nothing is wired into `src/rendering/`; that is Phase 3's job and this is the geometry
waiting for it."* Phase 3 went looking for something else and found it by walking into this.

Glowmere's scene pass is 21.4 ms of a 23.1 ms GPU frame at 720×450. The pass is fragment-bound (the
depth prepass submits the identical geometry for 0.20 ms) and its fragment *count* is set by the
triangle count rather than the pixel count, because the ecology submits more triangles than the
frame has pixels and a triangle smaller than a quad still costs a quad. See
`docs/renderer-2-architecture.md` §2.

Removing one scatter layer at a time put 18.5 ms of the 18.5 ms that the whole ecology costs on
`canopy` alone, and 10.4 on `bushes` — while `pines`, a comparable tree at a comparable count, cost
2.3. The distinguishing fact was not density, or wind, or materials, or emissive. It was this, from
a diagnostic added for the purpose:

```
valley_canopy  lod1 = 1853 tris, 97% of the source   lod2 = 1378, 72%   lod3 = 1378, 72%
valley_bushes  lod1 =  862 tris, 96%                 lod2 =  703, 78%   lod3 =  703, 78%
valley_pines   lod1 = 1025 tris, 51%                 lod2 =  753, 37%   lod3 =  753, 37%
```

against a ladder that asks for 35% / 12% / 4%. The far levels of the two expensive layers were the
source mesh with a haircut, and every counter the frame prints agreed the LOD system was working:
`lod=2/30/124/0` says how many instances chose each level and nothing said how big the levels were.

The cause is that `scene::makeLodMesh` built imported-mesh levels with `scene::decimateMesh` — a
vertex clustering on a uniform grid (ADR-045). A grid has no way to *reach* a triangle count: it
snaps vertices into cells and keeps whatever triangles survive. On a Quaternius tree, whose branches
are a mass of separate shells, most of them survive.

## Decision

**`scene::makeLodMesh` builds LOD levels 1–3 of a mesh source with `assets::buildLodChain` and
`assets::vegetationLodSettings()`.** The chain is built from the *budgeted* mesh — the one
`makeSourceMesh` already returns — so **LOD0 is untouched** and the near field is unchanged. If the
chain cannot be built at all, the old clustering is still there as the fallback; a mesh the
simplifier refuses is better served by a rough reduction than by the source at every distance.

`vegetationLodSettings()` is used for every mesh source, not only for scatter. Its sloppy fallback
fires only on a level that stalled, and geometry that simplifies cleanly (the test's sphere) is
unaffected by arming it.

**`buildLodChain` now refuses to return a level larger than the level before it.** `validate()`
enforced that the *ratios* descend; nothing enforced it of the *results*, and the results did not:
on CommonTree_1 the sloppy simplifier reaches 7.6% at the 35% rung and then returns nothing at all
at the 12% and 4% rungs, leaving both of those stalled at 93%. The chain read 100 / 7.6 / 93 / 93 —
the far levels the expensive ones, which is the exact arrangement the ratio validator exists to
refuse, with a thirteen-fold pop between adjacent distance bands on top of it.

A level that comes back strictly larger than its predecessor (or that cannot be built at all) is
tried again *from that predecessor* rather than from the source. One step of compounding error is
the price, paid only by a level that had already failed, and the level above has usually had exactly
the topology the source stalled on removed from it. If the retry still cannot reduce, the
predecessor's mesh is reused.

Strictly larger, not "no smaller", and the retry reaches for nothing `settings` does not already
permit. Both are there to keep ADR-078's hero contract intact: a chain whose levels come back
*equal* is a chain that stalled, and a hero that will not simplify is supposed to say so and be
drawn rather than be swapped for a shape that merely occupies the same volume.

**`ensureLodMesh` logs what each level actually came back as**, at debug level, against the source it
was reduced from. The defect above was invisible for as long as it was because nothing reported it.

## Consequences

The ladders reach their targets and descend:

```
valley_canopy  lod1 = 145 (8%)   lod2 = 145 (8%)   lod3 = 14 (1%)
valley_bushes  lod1 =  80 (9%)   lod2 =  80 (9%)   lod3 = 28 (3%)
valley_pines   lod1 = 217 (11%)  lod2 =  49 (2%)   lod3 = 22 (1%)
```

Submitted camera triangles and the scene pass, min of 3 interleaved runs, `--tier realtime`,
headless, on a machine shared with other agents:

| world | canvas | tris before → after | scene before → after | frame before → after |
|---|---|---:|---:|---:|
| glowmere | 720×450 | 436,176 → 201,172 | 21.36 → **12.65** | 23.13 → 14.16 |
| glowmere | 1440×900 | 635,754 → 396,493 | 20.97 → **18.15** | 24.12 → 20.97 |
| glowmere | 2880×1166 | 1,014,963 → 694,975 | 40.57 → **37.62** | 46.01 → 42.73 |
| glowmere | 2466×1766 | 711,065 → 621,121 | 36.37 → **35.39** | 42.14 → 41.09 |
| glowmere-medium | 720×450 | 378,415 → 234,633 | 10.49 → 9.31 | 13.24 → 11.60 |
| glowmere-dense | 720×450 | 1,066,140 → 462,441 | 17.69 → **12.78** | 21.43 → 15.40 |
| glowmere-dense | 1440×900 | 1,270,103 → 824,852 | 23.53 → **19.92** | 29.43 → 24.84 |

Between a half and a quarter of the submitted geometry, everywhere, and it buys 41% of the scene
pass at 720×450 and 7% at the editor's 3.36 MP canvas. That spread is not a disappointment; it is
§2's finding restated. The saving is the geometry-floored part of the invocation count, and at
3.4 MP the pass is bound by screen coverage instead.

Visually: at 1440×900, 8.0% of pixels differ by more than 2/255, none of them in the foreground or
on a hero — the difference is the silhouettes of distant trees and mid-ground bushes, which is what
LOD levels are for. Composition, light and colour are unchanged.

**What this does not fix, stated plainly.** `meshopt_simplifySloppy` overshoots on these meshes: it
was asked for 35% of CommonTree_1 and returned 7.6%, so the LOD0→LOD1 step at 28 px of screen radius
is a thirteen-fold drop rather than a threefold one. It looks right in a still frame and the
per-instance spread of ADR-082 staggers when instances take it, but nobody has watched a moving
camera cross that threshold. Iterating the sloppy call towards its target is the obvious next move
and is not done here.
