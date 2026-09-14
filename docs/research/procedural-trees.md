# Procedural trees: audit, research and architecture proposal

Status: **sections 1 to 3 are the original proposal, kept as written.** Phases 4 to 10 are built;
§7 below records where the proposal was right, where it was wrong, and what the build found that no
amount of proposing would have. Decision ADRs are 176 through 180. They were written as 170, 174,
175, 176 and 177 while this ran alongside two other agents, and renumbered on merge: the Glowmere
Valley 2 work had taken 170-175, including its own ADR-174 about vegetation banding.

The proposal is deliberately not edited to match the outcome. A design document rewritten after the
fact to look prescient is worth nothing to the next person, and three of the most useful findings
here are places it was confidently wrong.

## 1. Repository audit: what exists, and what each part of this design sits on

| Concern | What already exists | What the tree does with it |
|---|---|---|
| Branch curves | `spatial::Spline` (`src/spatial/spline.hpp`), Catmull-Rom / Bezier / Hermite, arc-length table cached by `structuralHash`, and **rotation-minimising frames by double reflection (Wang et al. 2008)** at `spline.cpp:676` -- explicitly not Frenet, so no flip at an inflection | Reuse unchanged. This was the single largest thing I expected to have to build and it is done and tested |
| Tube meshes | `makeTube(curve, radius, taper, sides, segments, twist, caps)` (`src/scene/procedural.hpp:145`). Linear taper only, but `SplinePoint::scale` interpolates along the curve and multiplies in, so an arbitrary radius profile is authorable per control point | One tube per branch axis, with per-point `scale` carrying the pipe-model radius. That is the non-linear taper hook |
| Branching | **Does not exist, and ADR-043 says so deliberately**: "Branching in particular belongs a level up: a branch is a tube whose curve starts on another tube's curve, which is a generator concern rather than a primitive one" | This work is that level up. The ADR anticipated it |
| Determinism | `noise::hashIndex(seed, index, channel)` (`src/core/noise.hpp`), stateless, GPU-mirrored verbatim in `shaders/noise.wgsl`. ADR-023's contract: "no wall clock, no state" | Every draw in the generator names a channel constant rather than passing a literal, so two draws cannot silently correlate |
| Noise | `fbm3`, `fbm3Vec`, `curlNoise`, `voronoiF1`, `regionField`. Value noise, not Perlin; `fbm3` fixed at 3 octaves | `fbm3` shapes the crown's density field (the asymmetry knob); `fbm3Vec` is the growth-direction wander |
| 3-D nearest neighbours | **Does not exist.** `ObstacleField` is XZ-only and models everything as a vertical cylinder; `FilterDistance` is a single-pivot O(n) scan | `spatial::PointGrid` is new, built in the same build-once/query shape, laid out by a counting sort over an open-addressed table so no hash iteration order is ever observable |
| Mesh concatenation | **Does not exist.** No `mergeMesh` | A short loop in the geometry phase, offsetting indices |
| Vertex format | `scene::Vertex` is position/normal/uv, 32 bytes, `static_assert`ed in three places. **No tangent, no vertex colour** | Phase 5 decision: bark without a normal map, or widen the vertex. See open question 4 |
| Instancing | `spatial::InstanceRecord`, 96 bytes. Position/rotation/scale/random/color/emissive. **Exactly one free custom float**, `emissive.a`, bound by name via `ProceduralGeometry::extraLane` | Leaves. The one lane carries the cluster's animation phase; everything else is derived in the shader from `random` |
| Custom generated mesh | `SourceSpec::assetMesh` (a `shared_ptr<const MeshData>`) with `PrimitiveKind::Mesh`. **Trap:** `structuralHash` for that kind hashes the `asset` *string*, not the mesh contents, and `ProceduralRenderer` caches GPU uploads keyed on it | Every generated mesh gets a synthetic identity string carrying the parameter hash, e.g. `tree://trunk/<hash>` |
| Custom placements | `Distribution::scatterCloud` + `scatterHash` with `DistributionKind::Scatter`. Precedent: `src/world/ecology.cpp`, `src/world/city.cpp` | Foliage clusters and leaves |
| Terrain | `TerrainQuery::at(p)` / `groundPoint(p)` (`src/world/terrain_query.hpp`), pure and thread-safe | Roots, at scene-assembly time. The generator itself stays placement-independent |
| Audio | Five bands only -- `bass` 20-150, `lowMid` 150-400, `mid` 400-2k, `highMid` 2k-6k, `treble` 6k-16k. **No sub-bass and no band called "high"**, contrary to the brief's §21 wording. Plus `audio.rms/peak/onset/onsetStrength/spectralFlux/spectralCentroid/beat/beatPhase/tempo`, and `music.*` structure events | The brief's mapping is rewritten against the vocabulary that exists. See §6 |
| Modulation | `params::ModRoute` with a fixed processor chain: gain, offset, curve, clamp, threshold, asymmetric attack/decay smoothing, envelope, remap. Routes are JSON data | Every audio reaction is a route. `src/app/world_builder.cpp:117` states the house rule: a second reaction system "would not appear in the route list, the graph editor, or a saved project" |
| Springs | **No general spring-damper exists.** But `wind::PlantChain` (`src/core/plant_chain.hpp`) is a real 4-point spring-damper chain, calibrated so its analytic and integrated tiers agree at DC, already wired to `ProceduralGeometry::motion` as a **per-frame uniform** (deliberately not part of `structureVersion`) | This is the branch-motion system. See §6 |
| Baseline motion | `wind::WindParams` -- travelling gust fronts, regional variation, turbulence, per-plant flutter -- a pure function of position and time with **no audio input at all** | The tree moves with no audio playing because the wind field does not know whether anything is playing |
| Emissive ladder | `world::EmissionLadder` (`src/world/art_direction.hpp:41`), a validated ~200:1 range with a deliberate **13x gap** between "noticeable" (0.295) and "special" (3.94) | The life-force veins sit on the ladder rather than inventing a brightness |
| Post | AgX default, bloom intensity 0.18 / threshold 1.0 on Glowmere so only the ladder's top two rungs bloom at all. **Anamorphic is currently broken** (a tap comb on compact bright features; the fix was reverted at HEAD) | Bloom and halation yes; anamorphic off |
| Examples | `examples/index.json`. Pure data; nothing compiled in | One entry, one project, one scene file |

Two things the audit changed in this design rather than merely informing it. First, `makeTube`
already doing rotation-minimising frames removed the largest planned work item in Phase 5. Second,
`NodeKind::Terrain` turned out **not** to be the precedent for a C++-generated, artist-editable
node: its generator inputs are not exposed as parameters at all, it has no editor UI, and the only
way an artist changes one is the World Builder's "Generate World" button, which replaces the node
rather than editing it. `NodeKind::Procedural` is the precedent. That is what §4 is built on.

## 2. Research: how procedural trees are actually generated

### 2.1 The candidates

| Family | Mechanism | Silhouette control | Hierarchy produced | Failure mode | Verdict |
|---|---|---|---|---|---|
| **Recursive / fractal branching** (Honda 1971, "Weber & Penn 1995") | a branch spawns *k* children by fixed angle/ratio rules, recursed to depth *d* | explicit but crude: the envelope is whatever the recursion happens to sweep | perfect — the recursion *is* the hierarchy | self-similarity is visible; branches interpenetrate because nothing knows about anything else; reads as "mathematical fractal", which §6 of the brief explicitly forbids | rejected as the primary generator; **kept as the root generator** (§2.6) |
| **L-systems** (Prusinkiewicz & Lindenmayer) | string rewriting with a turtle; parametric and stochastic variants add randomness and context | poor without an *added* envelope mechanism (topiary pruning volumes were bolted on for exactly this reason) | perfect | same self-similarity problem; the artistic control surface is a grammar, which is the wrong knob for a parameter search — grammars do not interpolate, so §35's "perturb around a strong candidate" has no meaning | rejected |
| **Space colonization** (Runions, Lane & Prusinkiewicz 2007) | scatter attraction points in a crown volume; every node grows one segment toward the average direction of the points that select it; points die when a node comes within the kill distance | **excellent and direct** — the point cloud *is* the silhouette | emerges, and is a genuine graph | every node grows equally, so the result is uniformly dense and reads as coral/nerves/noodles. No trunk emerges unless one is hand-built. This is the exact failure §5 of the brief names | **adopted for the environment term only** |
| **Self-organising / BH resource model** (Pałubicki et al. 2009, SIGGRAPH) | space colonization (or shadow propagation) estimates the *light/space* Q available to each **bud**; Q flows basipetally; a resource *v* flows back acropetally and is split at every fork by an apical-control parameter λ; a bud with resource *v* makes ⌊v⌋ metamers; weak branches are shed | excellent (inherits the attraction cloud) | genuine, with a first-class notion of *axis order* (trunk = order 0) | more parameters, two passes per iteration | **adopted** |
| **Graph growth / DLA / road-network colonisation** | agents extend a graph into free space under constraints | good | weak (a graph, not an ordered hierarchy) | no notion of apical dominance at all | rejected |

### 2.2 Why plain space colonization cannot produce this tree

Runions' algorithm is a beautiful three-parameter system — influence radius *d_i*, kill distance *d_k*,
segment length *D* — and its defining property is that **every node is equal**. A node either has
attraction points in range or it does not. There is no quantity in the algorithm that says "this limb
matters more than that twig", so there is nothing to make a trunk out of, and no reason for one branch
to become thick and long while its sibling stays short. Every reference implementation compensates in
the same way: `jakobrichert/space-colonization` has a literal `Trunk Height: 6.0` parameter that grows a
straight stick upward *before* colonization begins, and `dsforza96/tree-gen` does the same. The trunk is
not generated; it is prepended.

That is the direct mechanical cause of the noodle look the brief describes in §5. Uniform node
competition over a uniform point cloud gives uniform branch statistics, which is coral.

Two further consequences matter for us:
- **Thickness is retrofitted.** Both implementations compute radius purely from the pipe model over the
  final graph (`dsforza96` uses exponent e = 2.05). That gives taper, but it cannot give the
  *buttressed, swelling, ancient* trunk §9 asks for, because the graph has no memory of anything.
- **Nothing is ever discarded.** Real crowns are sculpted as much by branches dying as by branches
  growing; the tall clean bole of a monumental tree is a shedding artefact.

### 2.3 What the self-organising model adds, precisely

Pałubicki et al. keep space colonization as the *environmental* term and add the internal signalling
that space colonization lacks. The parts we take, with the paper's own equations:

**Environment (space colonization variant).** Each bud has a spherical occupancy zone of radius ρ and a
conical perception volume of half-angle θ and distance *r*. Markers inside any bud's occupancy zone are
deleted; each surviving marker is assigned to the nearest bud whose perception cone contains it. A bud
with at least one assigned marker has Q = 1 and an optimal growth direction **V** = the normalised mean
direction to its markers; otherwise Q = 0. The paper's typical values: **θ ≈ 90°, ρ = 2 internode
lengths, r = 4 to 6 internode lengths.**

**Internal allocation (extended Borchert–Honda).** Q flows basipetally and accumulates in internodes. At
the base, `v_base = α·Q_base`. Flowing back out, the resource *v* arriving at a fork splits between the
continuing main axis and the lateral branch as

```
v_m = v · λ·Q_m / (λ·Q_m + (1−λ)·Q_l)
v_l = v · (1−λ)·Q_l / (λ·Q_m + (1−λ)·Q_l)
```

λ > 0.5 biases the main axis (**excurrent** — one dominant leader, conifer-like); λ < 0.5 biases laterals
(**decurrent** — spreading, oak-like). The paper's Fig. 7 sweeps λ = 0.46, 0.48, 0.50, 0.52, 0.54 at
α = 2 and gets a clean progression of architectures from that alone.

**Bud fate.** A bud receiving resource *v* produces `n = ⌊v⌋` metamers, each of length `l = v/n`. This
single rule is what makes the hierarchy *readable*: a vigorous bud makes a long, many-segment shoot in
one step; a starved bud makes one short segment or none. Branch length distribution is therefore an
*output* of competition, not a parameter — which is exactly the property §5 and §10 are asking for.

**Direction.** The orientation of each new metamer is a weighted sum of three vectors: the default
orientation (terminal bud = parent internode direction; lateral bud = phyllotaxis + branching angle),
the optimal growth direction **V** with weight ξ, and a tropism vector with weight η. The paper notes
that *a strong downward tropism combined with a strong tendency to grow toward light produces decurrent
forms with highly gnarled branches* — which is a precise recipe for the monumental ancient look, and is
the single most valuable sentence in the paper for our purposes.

**Shedding** (Takenaka 1994): compare the light a branch gathered against its size in internodes; below
a threshold the branch is a liability and is shed. The paper flags that this works badly with the
space-colonization environment because Q is binary there — noted, and §2.5 says what we do instead.

**Diameter**, basipetally: `d^n = d₁^n + d₂^n`, *n* usually between 2 and 3 (Macdonald 1983). The paper
adds one critical sentence: **"branch width is not decreased when leaves and branches are shed or
pruned. The model thus requires a memory of past leaves and branches."** That memory is where a thick,
old, disproportionately massive trunk comes from. A tree that only counts its *surviving* leaves has a
thin trunk and looks young.

### 2.4 The decision

**A self-organising model with a space-colonization environment, an apical control that decays over
simulated age, and the pipe model with shed memory.** In short: Runions for *where*, Pałubicki for
*how much*.

The reasoning, against the brief's requirement list:
- *Controllable global silhouette* — the attraction envelope is the silhouette, directly authored.
- *Coherent trunk, strong primary limbs, readable hierarchy* — λ and its decay over age. Apical control
  high for the first phase of growth builds the bole; removing it later lets the crown spread. The paper
  states this explicitly as the excurrent→decurrent progression of a real tree as it ages, and it is
  precisely the Tree-of-Life silhouette: massive single trunk, then a broad spreading crown.
- *Controlled asymmetry* — asymmetry comes free from the competition, and is *controllable* by biasing
  the attraction cloud rather than by adding a random jitter term.
- *Parameters that interpolate* — every parameter is a real number, so §35's perturbation search is
  meaningful. This is the property that kills L-systems for us regardless of their other merits.
- *Per-branch metadata and stable IDs* — the simulation already carries axis order, parent, vigor and
  accumulated light per node. The semantic graph §39 demands is a by-product, not extra work.

### 2.5 Where we deliberately depart from the paper

1. **Q is not binary.** The paper's space-colonization Q ∈ {0,1} makes shedding unusable and throws away
   the information the BH split needs — with binary Q, λ·Q_m/(λ·Q_m+(1−λ)·Q_l) collapses to λ or 1 or 0.
   We use **Q = the count of markers assigned to the bud, normalised by the perception volume's capacity**,
   clamped to [0,1]. This keeps the competition graded, makes the BH split informative at every fork, and
   makes Takenaka shedding work. Cost: none. Risk: a graded Q changes the dynamics away from what the
   paper validated, so the tuning numbers in §2.3 are starting points, not answers.
2. **No seasons, no prolepsis/syllepsis.** We are not animating growth, so the distinction that
   motivates half the paper's machinery is irrelevant. One shoot per bud per iteration.
3. **Roots are not grown by this simulation.** See §2.6.
4. **Foliage is not a leaf-per-metamer model.** See §2.7.

### 2.6 Roots

Roots are a separate problem with a different aesthetic goal: §8 wants spreading, varied, partly-buried
buttressing that *relates to the canopy* (§38), not a second crown. Two options were considered.

**Run the same simulation downward** with an inverted attraction volume. Rejected: it produces a
mirrored crown, which is the "roots growing upward" failure §5 names, only upside down. It also makes
the terrain-following requirement awkward, because the simulation has no notion of a surface.

**Chosen: a small number of authored root axes, grown as guided splines.** Each root is seeded at a
buttress point on the trunk base, given a direction derived from the *canopy's* mass distribution (§38 —
a heavy limb to the north-east gets a counterbalancing root), and marched outward with the same
tropism/noise direction blend, following the terrain surface and sinking below it. This is recursive
branching (§2.1) used where its weaknesses do not bite: at depth 2–3 with under a dozen axes, nobody
perceives self-similarity, and the control it gives is exactly the control §8 asks for.

### 2.7 Foliage

Leaf-per-metamer is wrong for this scene at this scale. §12 asks for *clusters* with a centre, radius,
orientation, density, colour and their own animation phase — i.e. a mid-level object between the
skeleton and the leaf, which is also the right granularity for hierarchical animation (§20) and for
instancing (§13).

So: terminal branch tips (order ≥ some threshold, tip radius below a threshold) become **foliage cluster
attachment points**. Each cluster is an oriented ellipsoid carrying its own seed; leaves are instanced
inside it. The cluster is the unit of animation and of audio response; the leaf is the unit of geometry.

### 2.8 Skeleton → mesh

The skeleton is a graph of nodes with positions and radii. Turning it into tubes needs a frame
(a normal and binormal) at each node, and the frame must not spin, or the radial ring seam corkscrews
around the branch and the shading shows it.

- **Frenet frames** are unusable: the normal flips through any inflection point and is undefined on a
  straight run, which every trunk has.
- **Parallel transport / double reflection.** Wang et al. 2008 show the double-reflection method has
  4th-order global error against an exact rotation-minimising frame, versus 2nd order for Klok's
  projection method and Bloomenthal's rotation method. It is a handful of vector operations per segment
  and has no degenerate case. `dsforza96/tree-gen` uses parallel transport (Hanson & Ma) for exactly
  this reason. **Chosen: double reflection**, propagated from the root outward through the graph so a
  child branch inherits its parent's frame at the fork and junctions stay consistent.
- **Junctions.** True blended junctions (implicit surfaces, or a generalised cylinder union) are a large
  amount of work for a feature the brief asks for only as "visually coherent junctions". The cheap,
  effective technique is to (a) start the child tube *inside* the parent tube, at a point offset back
  along the parent axis, and (b) blend the child's first few rings toward the parent's surface. This
  hides the intersection without a CSG operation. Recorded as the plan; not validated at Phase 4.

## 3. Architecture proposal

### 3.1 The pipeline, and which existing system each stage sits on

```
TreeParams (JSON, authored)            -- the scene file's own parameter block
  -> generateTree                       src/scene/tree.cpp, new
     -> markers (crown envelope)        noise::hashIndex + noise::fbm3
     -> simulation                      spatial::PointGrid, new
     -> TreeGraph                       nodes, axes, foliage sites, roots, animation data
  -> rasteriseTree / measureTree        src/scene/tree_evaluator.cpp, new, CPU only
  -> searchCandidates                   src/scene/candidate_search.cpp, shared framework
  -> winning TreeParams                 written back into the scene file, unchanged in form
  -> tube + leaf meshes                 spatial::Spline + scene::makeTube, existing
  -> CompositionNodes                   NodeKind::Procedural via SourceSpec::assetMesh
  -> animation                          wind::VegetationMotion / PlantChain, existing
  -> audio                              params::ModRoute onto wind and emissive, existing
  -> render                             ProceduralRenderer, existing, unchanged
```

Nothing new is introduced below the `CompositionNode` line. No second renderer, no second audio
path, no new asset pipeline, no new parameter system.

### 3.2 The tree graph

`TreeGraph` carries what the brief's §39 asks for and nothing that can be re-derived: per node an
id, parent, axis, order, tier, position, direction, length, radius, parent radius, arc length from
the base and along its own axis, a phase, an animation weight and an audio response weight. Axes are
maximal runs of nodes sharing an axis id, so a whole limb is addressable without walking the graph.

Two invariants are load-bearing rather than incidental, and `validateTopology` checks both. **Parents
always precede children in the array**, which makes the basipetal and acropetal sweeps single linear
passes with no recursion and no sort, and is *also* a sufficient proof of acyclicity: an edge can
only point from a lower index to a higher one, so no cycle can close. And **a child's radius never
exceeds its parent's**, which is the pipe model's own invariant.

The animation weight is derived physically rather than authored per tier. A cantilever's compliance
goes as length over the second moment of area, so "far from the base and thin" is the physical
statement of "moves a lot". Measured means over the four tiers come out ordered without anyone
authoring four amplitude constants, which is what the hierarchical-motion requirement needs.

### 3.3 Geometry (phase 5 proposal, not built)

One `spatial::Spline` per axis, control points at the axis's nodes, per-point `scale` carrying the
pipe-model radius, then `makeTube`. Radial resolution by tier: trunk 16 sides, primary 12, secondary
8, tertiary 5. Frames propagate from the parent axis at the fork so junctions stay consistent.

Junction blending is the honest open risk. True blended junctions are a large amount of work for
something the brief asks for only as "visually coherent junctions". The cheap technique -- start the
child tube inside the parent and blend its first rings toward the parent's surface -- is the plan,
and it is not validated.

Foliage: each cluster is an oriented ellipsoid; leaves are instanced cards inside it. **Leaf cards
must come from an imported glTF asset**, because a procedural node's `material` JSON block does not
parse `alphaMode`, `alphaCutoff` or textures at all -- verified, not assumed. The renderer's
mip-aware alpha-coverage preservation then stops the crown eroding into crawling specks at distance.

### 3.4 Editor compatibility

Every claim below was checked against the code rather than assumed.

**Granularity: one `CompositionNode` per tier, not one per tree and not one per limb.** Five nodes --
`tree.trunk`, `tree.primary`, `tree.secondary`, `tree.tertiary`, `tree.foliage` -- plus one for the
roots. The reasoning is that the *picking* granularity forces it: picking resolves a 14-bit index in
one of three spaces to a node, and the procedural renderer writes the **object** index, never the
instance index, so a click on any of a hundred thousand instanced leaves selects the layer. Sub-node
selection is not available at any price short of a new pick space. Given that, one node per tree
would make the whole tree a single unselectable blob, and one node per primary limb would put twenty
entries in the outliner that an artist cannot usefully edit independently -- because a limb is an
*output* of the simulation, not an input, so there is nothing to adjust on it. A node per tier is the
granularity at which the things an artist would actually change (tier radial resolution, tier
material, tier motion response, tier emissive gain) are real, separable properties.

**Selectable, deliberately:** the five tier nodes and the root node, by click or outliner.
**Not selectable, deliberately:** an individual branch, an individual foliage cluster, an individual
leaf. Stated here rather than discovered later.

**Parameters registered, not baked.** `TreeParams` becomes a registered parameter block under
`tree/<node>/…` the way `registerProceduralParameters` does it, with a `TreeParams::structuralHash`
and an `applyTreeParameters(params, rest, live)`. This matters for more than sliders: **undo has no
record type for a raw struct field.** A setting edited as a plain field with no registered parameter
is invisible to undo entirely. So "registered" is the difference between undoable and not.

**Regeneration cost, and where the line sits.** A showcase tree is 138 ms to generate and the mesh
build is on top. That is far above the editor's 2 ms interactive budget, so it must not run on a
slider drag. It does not have to be special-cased: `advanceRebuildDeferral` is already a free
function -- deliberately, so a second caller can use it -- with a 90 ms settle timer and a ceiling of
four times the last measured cost. A tree at 138 ms therefore regenerates 90 ms after the artist
lets go of the slider, and at most once every ~550 ms during a long drag. That is the right
behaviour and it needs no new machinery. A `TreeProducts` cache keyed on the parameter hash, in the
shape of `TerrainProducts`, stops an unrelated edit elsewhere in the scene from regenerating the tree
at all.

**Round-trip and undo.** `TreeParams::toJson`/`fromJson` exist and are tested by regenerating an
identical tree from the serialised form, not by comparing fields. Undo holds a departing node whole
rather than serialising it, so a new kind is undoable provided `addNode` accepts it back.

**One finding to pass on, unrelated to this work.** `cloneNodeSpec` -- whose own header comment says
"the list of fields here is the list a new authored field has to be added to. If a duplicate ever
comes back missing something, this is the function that forgot it" -- is already missing five
authored fields: `waterFlow`, `materialAuthored`, `city`, `cityLibrary` and `floats`. Duplicating a
City node today produces a city at default settings. Separately, `src/ui/control_panel.cpp:1546`
passes a count of 6 to an `ImGui::Combo` over a 9-entry array, so `field`, `spline` and `sdf` are
unreachable from the add-node menu.

### 3.5 Candidate generation, scoring and selection

Covered by the shared framework and its commit message; the short version is fourteen trapezoidal
bands, every one bounded at both ends because a ranking stage is an optimiser, measured by
rasterising the candidate through the showcase camera because that camera is the authority. The
search is scrambled Halton for the initial sweep, then hill climbing around a diversity-filtered
elite pool drawn from everything seen so far.

**The winner's serialised form is the scene's authored parameter block**, not a side-file. One
description of a generated object: readable by the generator, writable by the editor, round-tripping
through the project file.

### 3.6 Animation and audio (phase 8/9 proposal, not built)

Reuse `wind::VegetationMotion` per tier node, which is a driven damped harmonic oscillator evaluated
analytically and is already a per-frame uniform. Glowmere's elder already demonstrates the exact
articulation wanted -- crown at `mass 120, stiffness 18, tipAmplitude 0.01` barely moving, filaments
at `mass 0.12, stiffness 0.6, tipAmplitude 0.12` floppy -- so the four tiers become four
`VegetationMotion` settings and the hierarchy is free.

**Audio drives the wind, not the tree.** The only audio-to-vegetation link that exists anywhere in
the codebase today is `music.build -> scene/windSpeed` (+0.18 on a rest of 0.74) in Glowmere, and
that is the right architecture: route audio to the field the tree already answers. It gives inertia,
damping and phase variation for free because the oscillator provides them, and it is structurally
incapable of looking like a branch wired to a VU meter. Onsets raise a `wind::Disturbance`, which is
a ready-made local impulse with its own spatial and temporal falloff.

The brief's §21 mapping is rewritten against the five bands that exist:

| Source | Destination | Why |
|---|---|---|
| `audio.bass` (20-150) | a world macro driving `scene/windSpeed` and trunk/primary emissive gain, attack 800 ms decay 3000 ms | the slow breathing; nothing this slow can read as dancing |
| `audio.lowMid` (150-400) | secondary tier `motion/gustResponse` | intermediate-scale movement |
| `audio.mid` (400-2k) | foliage emissive intensity and hue drift | colour, not position |
| `audio.highMid` (2k-6k) | tertiary/leaf flutter amplitude | the fastest visible motion |
| `audio.spectralFlux` | leaf flutter and particle rate | energy, not pitch |
| `audio.onset` | a `wind::Disturbance` at a hashed point in the crown | localised bursts, peak-hold envelope |
| `music.drop` / `music.break` | the life-force pulse and `scene/windSpeed` | form, over seconds |

Route depths follow the house discipline documented in `docs/glowmere-audio.md`: every event route
between 4% and 24% of its target's rest value, and `op: add` throughout so silence is exactly the
authored pose.

## 4. Phase 4: what the prototype demonstrates

Built, and measured over six seeds at the shipping parameters: 5,200 to 6,100 nodes, 15 to 18
primary axes, ~200 secondary, ~1,300 tertiary, ~1,400 foliage sites, 600 to 1,400 nodes self-pruned,
height 22.7 to 23.4 against an envelope topping out at 19.5, bole 7.3 to 7.8 against a crown base at
7.0, trunk base radius 2.3 to 2.5, in 138 ms.

Determinism is proven by generating twice and comparing every node and a content hash, and by the
complement -- a different seed must differ, which is what catches a generator that has stopped
reading its seed. Hierarchy is validated structurally. Candidate scores are deterministic across
runs. The evaluator separates a reasonable tree (0.814) from a pole (0.622) and a bush (0.509). A
72-candidate search over three generations takes 10.3 s, rejects nothing, and returns 23 distinct
candidates after diversity filtering.

## 5. What is not built, and what is not known

Not built: geometry, foliage, the scene, animation, audio, the contact sheet, the editor node kind.
Phases 5 to 12.

Not known, and not roundable:

- **Whether the score corresponds to what a person finds beautiful.** It separates the three cases
  it was shown. That is a sanity check, not a validation. The contact sheet in Phase 6 is the
  instrument that would settle it, and until then the weights are arguments.
- **Whether `openness` is banded correctly.** Every candidate in the search reads 0.06 to 0.13
  against an ideal floor of 0.18, so the component is discriminating but the whole population sits
  on the "too dense" side. Either the band is wrong or the canopy genuinely is too dense. Cannot
  tell without looking at a render.
- **`depthSpread` now scores full marks for every candidate.** It is doing its job as a guard rather
  than as a ranker -- it correctly reports that nothing in this design space is flattened -- but a
  component that never discriminates is also a component carrying 9% of the weight for nothing.
- **Junction quality.** Unvalidated.
- **Whether the tree reads at 138 ms x N in a live editor.** The deferral maths says yes; nothing
  has been run in the editor.

## 6. Sources

- Runions, A., Lane, B. and Prusinkiewicz, P., "Modeling Trees with a Space Colonization Algorithm",
  Eurographics Workshop on Natural Phenomena 2007.
  http://algorithmicbotany.org/papers/colonization.egwnp2007.large.pdf (accessed 2026-09-14).
  *Learned:* the three-parameter formulation (influence radius, kill distance, segment length) and
  that every node is treated equally, which is the mechanical cause of the uniform-density look.
  *Confidence:* high.
- Pałubicki, W., Horel, K., Longay, S., Runions, A., Lane, B., Měch, R. and Prusinkiewicz, P.,
  "Self-organizing tree models for image synthesis", ACM TOG 28(3) (SIGGRAPH 2009).
  https://algorithmicbotany.org/papers/selforg.sig2009.small.pdf (accessed 2026-09-14).
  *Learned, quoted from the paper itself rather than a summary:* the extended Borchert-Honda split
  `v_m = v·λQ_m / (λQ_m + (1−λ)Q_l)`; `n = ⌊v⌋` metamers of length `v/n`; λ sweeping 0.46 to 0.54 at
  α = 2 producing a clean progression of architectures; the space-colonization environment's typical
  values θ ≈ 90°, ρ = 2 internode lengths, r = 4 to 6 internode lengths; direction as a weighted sum
  of default, optimal (weight ξ) and tropism (weight η); Takenaka shedding and the paper's own note
  that it works badly with a binary Q; the pipe model `d^n = d₁^n + d₂^n` with n usually 2 to 3; and
  the sentence the whole trunk design rests on, that **branch width is not decreased when branches
  are shed, so the model requires a memory of past leaves and branches**. Also the observation that
  a strong downward tropism combined with a strong pull toward light gives decurrent forms with
  gnarled branches, which is the recipe for the monumental look. *Confidence:* high; extracted from
  the PDF's own text.
- Wang, W., Jüttler, B., Zheng, D. and Liu, Y., "Computation of rotation minimizing frames", ACM TOG
  27(1), 2008. https://www.microsoft.com/en-us/research/wp-content/uploads/2016/12/Computation-of-rotation-minimizing-frames.pdf
  (accessed 2026-09-14). *Learned:* the double-reflection method is 4th-order accurate against an
  exact RMF, versus 2nd order for Klok's projection and Bloomenthal's rotation. *Relevance:* the
  engine's `spatial::Spline` already implements exactly this, which is why no framing work is
  needed. *Confidence:* high.
- dsforza96/tree-gen, https://github.com/dsforza96/tree-gen (accessed 2026-09-14). *Learned:*
  parallel space colonization with Voro++ for proximity, pipe-model exponent e = 2.05, parallel
  transport framing after Hanson & Ma, and a trunk prepended before colonization begins.
  *Confidence:* medium; read from the repository description rather than the source.
- jakobrichert/space-colonization, https://github.com/jakobrichert/space-colonization (accessed
  2026-09-14). *Learned:* a working parameter set -- 2000 points, influence radius 8.0, kill distance
  1.5, segment length 0.8, ellipsoid crown -- spatial grid acceleration, stagnation detection, and
  again an explicit `Trunk Height: 6.0` grown before colonization. *Confidence:* medium, same reason.
- Caner Milko, "Procedural Tree Generation - TreeGen Part 1",
  https://caner-milko.github.io/posts/procedural-tree-generation/ (accessed 2026-09-14). *Learned:* a
  working implementation of the Pałubicki model, its node struct, and two practical failures worth
  avoiding -- id collision from `id_main = 2·id_parent` overflowing after 32 consecutive buds, and
  shadow-map calculation dominating runtime. *Confidence:* medium.
- Procedural World, "Space Colonization", http://procworld.blogspot.com/2011/02/space-colonization.html
  (accessed 2026-09-14). *Learned:* practitioner tuning advice (segment size controls intricacy
  versus straight trunks) and the observation that repeated runs tend to look alike. *Confidence:*
  low to medium; a blog post.

## 7. What the build found (phases 4 to 10)

### 7.1 Where the proposal held

- **The algorithm choice.** Space colonization for *where*, Borchert–Honda apical control for *how
  much*. Nothing in ten phases argued against it, and the excurrent→decurrent λ decay does produce
  the bole-under-spreading-crown outline it was chosen for.
- **`spatial::Spline`'s rotation-minimising frames and `makeTube`.** Reused unchanged. The largest
  planned item in the geometry phase was already done.
- **One `CompositionNode` per tier**, forced by picking resolving an object index and never an
  instance index. Confirmed against the code and unchanged since.
- **Audio drives the wind, not the tree.** Held, and it is now testable: a full-scale step on the bus
  moves the geometry by less than 0.002 rad of mean bend in the first frame.

### 7.2 Where it was wrong

- **`wind::VegetationMotion` was named as the animation system and cannot be used.** Six meshes each
  bent about their own base separate at every joint between them. The tree is skinned to a 239-joint
  branch skeleton instead. ADR-177.
- **Axis order was assumed to be the semantic tier.** It is not: a tree whose trunk forks early
  reported `primaryCount = 2` for a crown showing a dozen radial limbs, and the evaluator ranked the
  best-looking tree in the population eleventh of twelve. Tier is now substance relative to the
  trunk. ADR-178.
- **The crown envelope was a surface of revolution**, so no search inside it could produce an
  asymmetric crown at any population size. ADR-180, and the most generalisable finding here.
- **"Foliage clusters at terminal branch tips"** (§2.7) makes a shell with limbs poking through it.
  Clumps hang at the ends of *axes*, with a Poisson-disc spacing rule.

### 7.3 What only rendering found

Five times, a structural measurement was internally consistent and describing something other than
the picture. The standing conclusion is that the rendered loop belongs **in front of** the structural
one, not beside it.

| Symptom | What it actually was |
|---|---|
| Evaluator scored 0.951 on an unusable frame | Every structural metric in band; the image a blown-out white lollipop |
| `depthSpread` at its floor for all 72 candidates | r² weighting made it measure trunk thickness, and a trunk is a vertical line with no depth extent |
| `structureVisible` 0.53–0.71 against a band topping at 0.48, unreachable by any parameter | The raster clamps a disc to half a pixel, so 1,300 sub-pixel twigs claimed 1,300 whole pixels |
| Every candidate a rounded ball | The envelope could not express asymmetry (§7.2) |
| One tuned hero looked right; 8 of 12 candidates had limbs as bright wires | Veins in uv space do not scale with the branch |

And one suspicion that was **tested and refuted**: `silhouetteComplexity` was not blind to limb
structure. Measured over 24 candidates, the full silhouette's quotient varies *more* relatively
(sd/mean 0.651) than the branch-only one (0.342). Recorded in ADR-178 as a worked negative, because a
pattern with a name gets over-applied.

### 7.4 Measured, as built

Showcase parameters, one machine, no cross-session comparison:

- Generation ~140 ms per tree at ~5,800 nodes; mesh build on top; 100k–174k triangles per tree.
- A 48-candidate search with geometry and evaluation: ~13 s, nothing rejected.
- 239 joints, inside the 256-joint palette ceiling.
- **What the atmosphere costs: 0.64 ms and 0.57 ms**, at 960x540, minimum of 24 per arm, ABBA
  interleaved inside one process under the GPU lock. The arms are the volumetric pass plus the
  fourteen distant trees, against neither.

  The two invocations are the argument for the method as much as the result. The *absolute* numbers
  moved 1.8 ms between them (6.94 -> 5.10 ms with atmosphere) — the cross-invocation noise floor,
  and far larger than the effect. The *difference* moved 0.07 ms. An arm that always ran second
  would have paid for that drift, which is why the order alternates and each arm sees both
  positions equally.

  Wall clock around `renderToImage` includes its blocking readback, which is a constant in both arms
  and cancels in the difference. It is not a frame time and is not quoted as one.
- Total frame cost: **not quoted.** The absolute above is unreadable across sessions by its own
  evidence, and this scene has never been run through the real headless path with `--bench-json`.

### 7.5 The hero, and why it moved four times and then was chosen by a person

`scene::kHeroCandidate` is **4**, and it is a **human selection rather than the top-scoring
candidate**. The user reviewed the contact sheet, named #4 and #12 as contenders and asked for wider
trunks on both; #4 was taken after rendering both at the showcase camera at three trunk widths. The
score-ranked winner was #87.

Before that it had been 11, then 49, then 39, then 87 — each move forced rather than chosen, because
the bands were re-measured after the camera moved, after it moved again, and after the foliage
primitive changed. The hero is an artefact of the scoring; the scoring is an artefact of the camera
and of what the rasteriser can see.

**The override is the pipeline working, not failing.** §7.6 records that the evaluator cannot judge
canopy density as rendered, and density is precisely the axis the contending cells differed on. A
search honestly characterised as unable to rank an axis is not overruled when a person decides that
axis; `selectionNote` on the written record says all of this, so the next person to disagree knows
what they are disagreeing with.

**The trunk width went back to where it started, and that is a finding rather than indecision.**
`radiusScale` was 1.0, was cut to 0.55 because the pipe model with shed memory produced a bole that
read as a stump, and is 1.0 again. The earlier judgement was correct about a tree that no longer
exists: 23 m tall, a near-orthographic 44-degree lens at 37 m, and a solid-shell canopy, where a
thick trunk had nothing to carry and everything to compete with. At 30 m, a low 55-degree lens and an
open canopy, the unscaled model reads as a trunk carrying a crown — 7.3:1 height to diameter against
12.9:1 at 0.55. **The pipe model was faithful all along; what was wrong was the proportions around
it.**

One knock-on was corrected without being asked for, and is named because it was not the user's
choice: roots are sized *from* the trunk, so widening it widened them by the same factor and they
came out heavier than anything shown on the sheet the user judged. `rootRadiusScale` was reduced to
hold their absolute thickness where it was.

### 7.6 The evaluator cannot judge canopy density, and this is structural

The foliage primitive was replaced and **not one band's variance changed by a thousandth**. The
evaluator's rasteriser stamps each foliage cluster as a disc, so everything between the leaves is
erased before any metric sees it. A measurement that cannot notice the thing you changed is not
measuring it.

Feeding it the spray's true alpha coverage corrected the scale — six bands then moved by up to an
order of magnitude, and `silhouetteComplexity`'s raw range went from 3–41 to 55–249, because a mask
of sparse cut-out sprays has an enormous perimeter for its area where a mask of solid discs had very
little. But it cannot correct the *ranking*, because coverage is a property of the primitive and is
identical for every candidate.

The proof that the gap is real: `boxFill`, the axis that ought to separate "reads as volume" from
"reads as scattered leaves", reports the selected hero as the **densest** of the top six (0.433) when
by eye it is among the airiest. `openness` has been re-banded four times and settles back to scoring
~1.0 for almost the whole population each time; it is now demoted to a guard beside `depthSpread`,
on the evidence that the sparsest candidate on the sheet scored 0.42 and one of the fullest scored
0.43.

**The evaluator ranks crown architecture competently and cannot judge canopy density as rendered.**
Closing that means rasterising real foliage geometry instead of a proxy. The shared record's
`selectionNote` is the supported answer in the meantime: a human override is an outcome, not a
failure.

### 7.7 Still open

- The tree is a monumental, anchored, luminous fantasy tree with architectural variety across the
  population, a readable hierarchy, warm-against-cool colour and legible scale. It is **not yet
  awe-inspiring**, and §51 says not to rationalise that away. The two named remaining gaps:
  - ~~The canopy is a mass of small flakes.~~ **Done.** The card is now a seven-to-eleven-leaf spray
    cut out by a generated alpha mask, so a card can be large enough to read as mass without reading
    as a rectangle, and the silhouette edge is leaf-shaped. A quarter of the triangles of the flakes
    it replaced. What remains on this axis is that the *evaluator* cannot judge canopy density —
    see §7.7.
  - **No foreground.** There is no depth cue in the near field, only mist in the far one. Adding one
    is exactly the environment accumulation §26 warns against, so it is named rather than attempted.
- The evaluator has never been validated against a person's ranking. It agrees with one reader on one
  contact sheet, which is a sanity check.
- The editor path is unbuilt: the tree emits `scene::Scene` entities, not `CompositionNode`s, so it
  is not yet selectable or editable. `PrimitiveKind::Generated` exists on the Glowmere branch and is
  the intended route.
- Runtime cost is unmeasured (§7.4).
