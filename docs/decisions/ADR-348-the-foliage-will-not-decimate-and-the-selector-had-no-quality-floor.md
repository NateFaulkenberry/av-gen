# ADR-348: The foliage will not decimate, and the selector had no quality floor

Status: accepted
Date: 2026-09-19
Extends ADR-078 (LOD chain generation), ADR-122 (importance), ADR-123/124 (representation
selection), ADR-339 (the Tree of Life's five emissive layers).

*Numbered 348 because 344–347 were taken on `main` while this branch was in flight; the branch's
commit messages and source comments say ADR-344 and are one renumbering behind.*

## Context

Runtime LOD for imported assets, driven by the Tree of Life's 3,162,186 triangles across 43
primitives and five nodes.

Two thirds of the machinery already existed and was not joined up:

* `assets::buildLodChain` (ADR-078) builds a chain from a mesh — pure, deterministic, GPU-free. It
  was called from `src/scene/procedural.cpp` and nowhere else.
* `rendering::RepresentationSelector` (ADR-123/124) chooses a representation from an
  `ImportanceRecord`. **Nothing in the engine called it.** `scene_renderer.cpp` did not mention the
  type.
* `scene::Scene` had no LOD field at all, so an imported glTF entity had no chain to select from.

So chains were generated for procedural scatter and consumed nowhere, and imported meshes had
neither. Project memory recorded this as "entity LOD not live". This record is the wiring, plus the
two things the wiring turned out to need that nobody had anticipated.

## Decisions

### 1. The Tree of Life's foliage cannot be decimated, and the reason has a floor under it

`buildLodChain` returns **1.000 of the source at every rung** — 0.5, 0.2, 0.07, 0.02 — with
`reachedTarget` false at all four. This is the failure `mesh_lod.hpp`'s header warns about, at its
theoretical maximum, and the cause is not a stall:

> 1,046,400 triangles across 137,437 disconnected closed shells is **7.6 triangles a leaf**, and a
> preserving simplifier cannot remove an edge from an 8-triangle closed shell without changing its
> topology.

It is not stalling. It is finished before it starts. The other four layers reach their ratios: wood
and lumens hit 0.020 exactly, twigs 0.019, tracery 0.020 through the sloppy fallback.

### 2. Shell thinning: remove whole leaves when there is no edge left to collapse

`ShellThinning` in `src/assets/mesh_lod.hpp`. Shells are found by union-find over shared vertex
*positions* (a hard-shaded leaf carrying three vertices per corner is one shell, not several); which
survive is decided by a hash of each shell's quantised centroid, so the thin is spatially uniform
rather than regional and is stable under a re-export that moves a leaf by a float's last bit; a size
bias drops the smallest leaves first, which is the half of the brief's §4 a pure hash would miss;
and every surviving shell is then **grown about its own centre by r^-0.5**, capped at 3×, so the
canopy keeps its total leaf area rather than acquiring holes.

With it, the whole tree reaches the requested ladder exactly: **0.500 / 0.200 / 0.070 / 0.020**
against targets of 0.5 / 0.2 / 0.07 / 0.02.

### 3. The strategy is chosen from the geometry, once, and not from what the simplifier returned

This took a second pass and it is the part most likely to be got wrong again.

**The sloppy simplifier also reaches every ratio on the foliage** — 0.483 / 0.186 / 0.062 / 0.017.
A rule that fired thinning on an overshoot would therefore never fire at all. What it reaches them
by is quantising each 8-triangle leaf onto a grid coarse enough to collapse it, and the rendered
frame is a thinner, see-through canopy where a dense one was.

So `buildLodChain` decides once, before the level loop: a mesh whose **mean shell is at or under 24
triangles** and which has at least 64 shells gets thinning, and has the sloppy simplifier taken away
from it. Everything else is untouched. 24 separates the Tree of Life's layers by a factor of three
either side — foliage 7.6, tracery 35, twigs 97, lumens 125, wood 1,005 — and it is the only
threshold in this work that is a judgement rather than a consequence.

A third condition was added after a test failed: a level whose simplification **lost geometry** (the
`boundsTolerance` guard refused it) also counts as the simplifier having failed. Without it, a cloud
of small shells that the simplifier reaches the ratio on *by deleting the outermost shells* fell all
the way back to the source with thinning armed, having declined to thin a level that had already
been refused.

### 4. The selector had no quality floor, and on one large asset that is fatal

ADR-124's px/triangle rule is a **cost** rule with no fidelity term in it. Correct for a scatter — a
fern never reaches 500 px/triangle at any rung, so the ladder's own thresholds decide. Wrong for one
large asset: the Tree of Life's triangles project to a fifth of a pixel each at its hero camera, no
rung reaches the target at any size the shot will ever be, and "the coarsest rung still under the
target" is therefore always the last one.

Measured: every one of the 43 drawables went to rung 3 or 4 at a camera where the tree is 900 pixels
tall. The frame is a sparse scattering of huge leaf cards with sky through the canopy.

`RepresentationPolicy::maxScreenError` refuses a rung whose deviation projects to more than **8
pixels**, however cheap it would be. `LodLevel::error` already existed and already promised never to
understate; nothing had ever read it.

**Eight is rendered, not reasoned.** Every rung forced in turn at five camera distances,
1920×1080, tier high, each compared against LOD0 by eye and by MS-SSIM. At the hero camera rung 1 is
the same picture, rung 2 is visibly chunkier foliage, rungs 3 and 4 are a different tree; the
foliage layer's projected errors there are 5.8, 19, 67 and 108 px. Eight is the round number between
the rung that passes and the one that does not, and the medium-shot pair at the distance it puts
rung 2 (520 m) is indistinguishable.

A rung whose error is zero — which is every `LodRung` built anywhere else in this repository — is
admitted unconditionally, so the scatter path and ADR-124's existing tests are untouched.

### 5. LOD0 is the source mesh and is not copied

`scene::MeshLodChain` holds the rungs *beside* `scene.meshes`, keyed by the `MeshId` they hang from.
`scene.meshes[base]` stays the authored geometry, so the offline renderer, the path tracer, the
bounds cache and the mesh metrics keep reading the asset and cannot be handed a rung by accident.
The brief's §1 as a property of the data layout rather than a rule to remember.

The rungs are deliberately **not** appended to `scene.meshes`. That would have been less renderer
code and would have silently inflated every count that walks the mesh list — the world's triangle
total, the mesh-bounds cache, the memory report, the offline renderer's idea of what the scene
contains. A LOD that changes what the scene *is* is not a LOD.

### 6. Opt-in, per node, and off everywhere until a scene asks

A `"lod"` block on a gltf node turns it on and nothing else does. No scene in this repository has
one, so no scene in this repository draws differently. `lod.ratios`, `lod.thinning`,
`lod.hysteresis` and `lod.maxScreenError` are all authorable; a misspelt key is refused rather than
ignored.

Chains are built at flatten time and cached on (asset path, asset version, ladder). Necessary rather
than tidy: the five layers take 3.4 seconds of pure arithmetic and a flatten runs on every parameter
edit.

### 7. Offline is untouched

`RepresentationPolicy::forTier(Offline)` already forced the top representation. `--render-limits
unlimited` now does too, which is what that flag already means for every other ladder here. Both are
asserted in `tests/rendering/test_entity_lod_gpu.cpp`, with every other tier as the control — the
first time that promise has been checkable, because nothing consumed the policy before.

Note the tension this leaves, unresolved and stated: `DetailLimits::offlineDefault` deliberately
*keeps* the procedural LOD ladder in an offline render, with a measurement behind it (lifting the
rungs raised flickering area from 2.842% to 4.453%). Entity LOD does the opposite, because §5.9 and
the brief's §14 both say so. Whether a hero asset's ladder should follow the scatter's rule offline
is an open question and a separate decision.

## Consequences

**Performance.** GPU lock held, 1920×1080, tier high, three interleaved repeats per arm, **minima
over repeats** (ADR-170: never means, never one run), binary hashed before and after the pass and
unchanged, load average 2.84 at the start. The per-run spread is printed because it is wide and is
the reason minima are the only honest statistic here — `hero off` ran 18.09 / 15.20 / 18.35 across
its three repeats.

| view | radius px | LOD0 GPU | LOD GPU | saved | triangles submitted |
|---|---:|---:|---:|---:|---|
| closeup | 1678 | 30.08 ms | 22.87 ms | 7.21 (24.0%) | 51.4% of LOD0 |
| hero | 470 | 15.20 | 8.85 | 6.35 (**41.8%**) | 33.3% |
| medium | 196 | 8.91 | 4.85 | 4.06 (45.6%) | 13.9% |
| wide | 78 | 6.55 | 4.13 | 2.42 (36.9%) | 6.5% |
| small | 24 | 3.93 | 2.56 | 1.37 (34.9%) | 3.9% |

At the hero camera the scene pass goes 9.44 → 6.68 ms and the shadow pass 3.08 → 1.31, the second
because a camera-visible caster draws its shadow from the rung the camera chose.

**The tree is what costs, and the earlier finding to the contrary has an explanation.** The same
instrument on the shipping animated scene, minima over three interleaved repeats:

| arm | GPU | scene pass | draws | triangles |
|---|---:|---:|---:|---:|
| everything | 11.86 ms | 9.37 ms | 51 | 3,203,880 |
| the five tree nodes hidden | 6.49 | 1.51 | **8** | 41,694 |
| foliage and twigs hidden | 7.93 | 3.34 | 24 | 711,292 |
| tree, cosmos and shader all off | 4.13 | 1.70 | 3 | 32,710 |

The tree is 5.37 ms of 11.86, and **7.86 ms of the 9.37 ms scene pass — 84% of it**. Foliage and
twigs are 3.93 of that 5.37, which is the two layers §4 says to treat differently.

An earlier measurement recorded as "nine draws, 15.96 ms, 3.16M triangles is not what costs here"
does not reproduce: this scene is 51 draws in every run of every arm that has the tree in it. It is
**eight** draws with the five tree nodes hidden. The earlier number was almost certainly taken with
the tree absent or invisible, which would make its conclusion true of the frame it measured and
false of the scene it was about.

**The hero number is the cost of §13 and should be read as a result.** Without the quality floor the
hero shot submits 2.6% of the source instead of 33% and looks wrong: a sparse scattering of huge
leaf cards. The floor gives back most of the saving anyway — 41.8% at the hero camera — because
what it refuses is the bottom of the ladder and not the middle of it. It is authorable per node
(`lod.maxScreenError`) for the cases where the trade should go the other way.

**Popping: measured in frames, and better than the system deserves.** Thirteen rendered steps from
660 m to 420 m, LOD against no-LOD at the same cameras. The worst frame-to-frame pair is **1.01×**
as different as the same pair without a ladder — the rung changes are smaller than the camera's own
20 m step. The reason is worth recording because it is a property of the asset rather than of the
system: the tree is 43 separate parts, each with its own ladder and its own thresholds, so they
cross at different distances. Across those thirteen steps the rung histogram goes 5/5/14/9/10 to
5/10/12/11/5 and the submitted triangles go 341,384 to 530,371 — a 55% change in geometry, arriving
a part or two at a time. **A single-part asset would not get that cross-fade for free.**

**Nothing that did not ask for LOD changed.** `tree-hero-off` — the shipping scene with no `lod`
block — renders to sequence hash `9b4040cf331a37b2` after every change in this branch, the same
hash it produced before the quality floor, the UI line, the selector reset and the ViewContext
cleanup. The other arms rendered in the same pass all hash differently, so that is a stable frame
and not a hash of nothing.

**Tests**: 2,327 cases / 2,241,670 assertions green, up from the 2,307 / 2,239,146 baseline.

**A finding outside this work's scope, reported because it is larger than this work's result.** The
imported vertex buffer for these five layers is **1,277 MB where 142 MB is needed**. Every primitive
of a multi-material glTF references the same POSITION accessor, and `convertPrimitive` copies the
whole accessor per primitive because `MeshData` cannot name a slice of a buffer somebody else owns.
The foliage's 22 primitives each carry 929k or 2.2M vertices, of which each indexes a twenty-second:
34.5M vertices where 3.1M are distinct, 11× on that layer and 9.0× over the tree. This is not a
property of the tree — it is a property of every multi-material glTF this engine imports — and
`optimiseMesh`'s existing vertex-fetch pass already removes it. Nothing here changes it, because
changing LOD0 is exactly what the brief's §1 forbids and the right fix belongs to the importer.

## Rejected alternatives

**The sloppy simplifier on the foliage.** It reaches every ratio, which is what makes it dangerous:
it looks like success in a table. In frames it produces a thinner, see-through canopy, because
reaching 20% of an 8-triangle leaf means collapsing the leaf.

**Thinning everything shell-structured.** A hundred rocks in one part should be simplified, not
decimated to sixty rocks. `maxShellTriangles` is what separates the two cases.

**Appending rungs to `scene.meshes`.** Less renderer code; changes what the scene is.

**Trusting MS-SSIM to choose between thinning and the sloppy simplifier.** It prefers the sloppy
arm — 0.964 against 0.939 at the hero camera — because the sloppy result keeps leaf *positions* and
loses leaf *mass*, and a structural metric rewards the former. The frames say the opposite about
canopy density. This is the project's standing instruction meeting a case where it bites: the metric
and the picture disagree, and the picture decides. Both are reported rather than one being dropped.

## Revisit triggers

* An asset whose mean shell is near 24 triangles, where the strategy switch will be making a
  judgement rather than reading an obvious case.
* A second hero asset for `maxScreenError`. Eight pixels is one number over every asset, calibrated
  on one canopy; eight pixels of deviation on a face is a different face.
* The importer learning to share a vertex buffer between primitives, which would make the 9× vertex
  blowup — and a good part of the flatten cost — disappear without touching this system.
* `Representation::HlodProxy` or `Impostor` being built. The kind bands are set to −1 here precisely
  because they are declared and unbuilt; the day one exists, that suppression is wrong.
