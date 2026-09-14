# Glowmere Valley 2 — research and design

Phase 1 deliverables 3 and part of 5. Written 2026-09-14 against the tree at `c232776`.

Every method below is reported in the form the brief's §2.3 demands: **what problem it solves, what
it consumes, what it produces, why it fits *this* engine, what it costs, when it runs, whether it
emits geometry or transforms, how it is seeded, and where it stops.** A method that was read and
rejected says so and says why, because a research document that only lists what was chosen is a
sales brochure.

Three findings govern everything that follows and are stated first.

> **1. Nothing here needs a new engine concept except one: radial asymmetry on a swept cap.**
> The terrain, the river, the habitat fields, the sampler, the instancing and the continuous camera
> are all expressible in types that exist — in several cases the type is not merely expressive
> enough, it is *already doing this job at a smaller scale*. The one genuinely missing primitive is
> a per-angle radius modulation on the sweep. `scene::makeTube` has a circular cross-section only,
> and no lathe is exported at all (one exists, hard-coded inside `makeBeveledCylinder`).
>
> **2. The papers' performance conclusions are from 2007–2015 hardware and do not bind us**, because
> our generation runs once at load, not per frame. Lagae & Dutré conclude that tile-based methods
> are "the only option" for real-time large distributions; that conclusion is about a 2007 CPU
> generating points every frame. We generate ~10⁵ points once. The tile machinery buys us nothing
> and costs a seam problem, so it is rejected — see §3.4.
>
> **3. Six hero mushrooms are a search problem, not a modelling problem**, and the search is the
> deliverable rather than the six. §4 is that architecture. The load-bearing decision there is that
> **no aesthetic score component may be monotone** — every one scores a band and falls off both
> sides — because a monotone score is an instruction to maximise, and maximised edge density,
> asymmetry and tessellation are three of the failure modes the brief asks us to avoid.
>
> **4. The scene's problem is not point count.** ADR-126 and ADR-151 both measured that Glowmere's
> cost is coverage, not instances: 45% of its triangles are sub-pixel and cover 2% of the frame, and
> deleting *everything* below 8 px of radius moves the scene pass by +1.8% — inside the noise, wrong
> sign. A vegetation system that "looks natural" by adding instances is optimising a quantity that
> was already measured not to matter. The wins available are composition and per-pixel cost.

---

## 1. Terrain and the river

### 1.1 The problem the brief actually states

Not "make nicer terrain". The brief's §4.1 asks for a river that **enters at one map edge, traverses
continuously, and exits at another**, with terrain that responds to it. The original has a stream
that begins and ends inside the map (§03-baseline.md §3). So the geographic axis is the deliverable,
and the terrain is downstream of it.

That ordering is the whole design. Every approach that generates terrain first and then finds a
river in it — hydraulic erosion, flow accumulation over a noise heightfield, priority-flood
depression filling — inverts it, and inverting it is what produces a river you cannot control.

### 1.2 Selected: a designed centerline, then distance-to-river terrain shaping

**Problem solved.** Guarantees the traversal property by construction rather than by search.

**Inputs.** A Catmull-Rom spline in the XZ plane whose first and last control points lie on opposite
map boundaries; a per-arclength width and depth profile; a valley cross-section function; a
large-scale elevation gradient along the flow direction; a noise field for local variation.

**Output.** A closed-form `height(x, z)` and, as by-products, three scalar fields that the vegetation
system needs anyway: signed distance to the centerline `d`, arclength position `s`, and **height
above river `HAR`** — the elevation of the ground minus the water surface elevation at the nearest
centerline point.

**The shaping function.**

```
s, d   = nearestOnCenterline(x, z)          // arclength and signed lateral distance
w(s)   = channel half-width                  // varies along the river
base(s)= source elevation + (mouth - source) * (s / totalLength)   // monotone downhill
cross(d) = valley cross-section, evaluated at |d| / w(s)
h(x,z) = base(s) + cross(d) * valleyDepth(s) + noise(x,z) * roughness(|d|)
```

with `cross` a smooth profile — flat channel bed inside `w`, a bank ramp out to some multiple of
`w`, then the valley wall, then flattening onto the plateau — and, decisively,
**`roughness(|d|) → 0` as `|d| → 0`**. The noise amplitude is a function of distance from the river.
That single term is what stops unconstrained noise destroying the large-scale structure, which is
the failure the brief's §4.2 names explicitly. Inside the channel the noise is off, so the riverbed
is coherent; on the valley walls it is at full amplitude, so the slopes are not ruled surfaces.

**What is actually missing from the existing system**, and it is only two things:

1. **Noise amplitude as a function of distance from the river.** `world::octaveSum`
   (`src/world/world_map.cpp:36`) applies its octaves uniformly and the `Feature` flatten is a
   separate blend afterwards. The `roughness(|d|)` term above — noise off in the channel, full on
   the walls — does not exist. This is the term that keeps the riverbed coherent.
2. **`HAR` as a queryable field.** `WorldMap::Sample` carries `height`, `normal`, `slope`,
   `waterSurface`, `submerged`, `moisture`, `altitude`. `height - waterSurface` is *almost* HAR
   already; what is missing is that `waterSurface` is only defined where there is water, so it has
   to become "the surface of the nearest course, extrapolated" to be a gradient over the whole map.
   `world::waterCourses()` and `WaterCourse::surfaceAt(p)` already supply exactly that.

Neither is a new subsystem. Both are terms in functions that exist.

**And the river must get much bigger.** `glowmere-run` is **7 m wide with a 2.4 m amplitude on a
640 × 640 m map** and does not reach either boundary. Whatever else Phase 2 does, the map extent,
the centerline length and the channel width all have to grow together, and `WorldMap::size` is a
fixed extent with no streaming, so the cost of growing it is real and must be measured (risk R5).

**Why it fits this engine.** `WorldMap::height` is already an analytic function queried pointwise
(it is what `ClearanceField` calls, `src/world/camera_clearance.cpp`). A closed-form shaper drops
into that contract with no new query path, no heightmap texture, no chunk streaming and no
regeneration story. The scatter placer, the terrain mesher, the camera clearance field and the
navigation mesh all get the new terrain for free by calling the same function they already call.

**Cost and when it runs.** The expensive part is `nearestOnCenterline`, which is the classic
closest-point-on-spline problem. Solved once at load by **baking the centerline to a uniform
arclength table** (the engine already has `spline::sampleByDistance` and `length()`), then answering
queries with a uniform grid of "nearest sample index" over the map — an `O(1)` lookup plus one local
refinement over three adjacent samples. At 2 m grid spacing over a 1 km map that is a 500×500
`uint16` table: **500 KB, built in single-digit milliseconds.** Per-query cost is then comparable to
the two octaves of noise already being evaluated. Runs at load. Produces no geometry; it produces a
function.

**Determinism.** The centerline is authored control points plus a seeded jitter; the noise is the
engine's existing seeded field. Nothing iterative, nothing order-dependent.

**Limitations, stated plainly.**
- No physical realism. Meanders are authored, not evolved, so the river will not have the
  characteristic asymmetric erosion of a real meander bend (cut bank steep, point bar shallow)
  unless that asymmetry is written into `cross` as a function of local curvature. It should be —
  see §1.4 — but it is a cosmetic term, not a simulation.
- Tributaries need a second centerline and a blend, and the blend at a confluence is fiddly. Phase 2
  should ship one river.
- `nearestOnCenterline` is ambiguous where the river doubles back on itself within the grid cell
  spacing. Meander amplitude must stay above grid spacing, which is a constraint on the author, and
  the generator should assert it rather than produce a silently wrong field.

### 1.3 Rejected: hydraulic erosion

**What it is.** Droplet or grid-based simulation that carves channels into an existing heightfield
(Mei et al.'s pipe model; particle-based thermal/hydraulic hybrids).

**Why rejected, decisively.** It produces a drainage network, not *a* river, and it produces it
wherever the noise happened to put a minimum. The brief requires a specific traversal and a specific
cinematic corridor. Steering erosion to a prescribed path means constraining it until it is doing
what §1.2 does directly, at the cost of an iterative simulation, a heightmap representation (which
the engine does not currently use), a regeneration cost on every parameter change, and a determinism
story for the iteration order.

**What it would buy.** Genuinely better-looking slopes: gullies, ridgelines that branch, talus at
the foot of walls. That is real and it is not nothing.

**Revisit when** the valley's *walls* are the complaint rather than the river. Erosion can be run as
an offline pass over the walls only, with the channel corridor masked out, and baked. It does not
need to be in the runtime generator to be in the scene.

### 1.4 Adopted in part: curvature-aware banks

From the meandering-river literature generally rather than any one cited paper: on a bend, the
outside bank is undercut and steep, the inside is a shallow point bar. This is one term:

```
asym = signedCurvature(s) * bankAsymmetry
cross(d)  ->  cross(d * (1 + asym * sign(d)))
```

Cheap, no new data — the curvature comes from the same arclength table — and it is the difference
between a river that reads as carved and one that reads as extruded. Costs one derivative per
centerline sample, computed once.

### 1.5 The water surface

**Reuse the existing water system unchanged. The question of whether it can follow a curve is
settled: it already does.**

`world::buildChunkWater` (`src/world/terrain.hpp:193`) builds one water mesh per terrain chunk from
the same `ChunkField` the ground is built from, emitting a quad wherever any corner is under water
and sampling `WorldMap::waterSurface` for its height. **Because it samples the surface rather than a
constant, it follows a descending river course down a valley for free**, and the shoreline is where
the two surfaces cross rather than where a mesh boundary fell. Above it, `world::waterCourses()`
produces downstream-ordered `WaterCourse`s and `world::waterBodies()` produces `WaterBody`s carrying
a cumulative arc-length table, `pointAt(along01)`, `tangentAt`, `shearProfile(r)` and
`wettedHalfWidth(along01)`.

Two details worth knowing before touching it. The water vertex format is deliberately overloaded:
**`uv = (depth, channel)`** where channel is 1 on the centreline and 0 at the bank, and
**`normal.xz` is the downstream flow direction with `normal.y` the speed** as a fraction of the
body's fastest — the real normal is +Y everywhere, so the slot was reclaimed. And water is blended,
so it is excluded from the depth prepass and the linear depth it reads is the *bed*, which is what
the thickness, shoreline and depth-colour terms are made of.

So the river's water is a solved problem and Phase 2 should change none of it. The brief's §4.3
caution against geometric complexity is right and the measured evidence sharpens it: `docs/performance.md`
records that on this content **84% of the ecology's cost is rasterising the survivors**, not
submitting them. A denser water surface spends the one currency the frame has least of.

**Limitation:** water geometry is produced *only* by the terrain chunker. There is no standalone
"water mesh along an arbitrary spline", so a waterfall, a detached pool or an aqueduct would be new
work. A river in a valley is exactly the supported case.

---

## 2. Vegetation distribution

### 2.1 What the research collectively says, before any method

Three of the four vegetation papers make the same argument from different directions, and it is the
one worth internalising: **spacing is not distribution.** Poisson-disk sampling produces points that
are *evenly* spread, and evenly spread is not what a forest looks like. Zhang et al. state it
directly — random approaches including Poisson-disk "fail to reflect the specific biological
distribution features and patterns". Real vegetation is *clustered at one scale and spaced at
another*: stands and glades at tens of metres, minimum separation at metres.

So the architecture is not "pick a sampler". It is: **a density field decides where and how much, a
sampler decides the local arrangement, and a competition rule decides who survives.** The brief's
§5.2 Stages A–F already say this; the research confirms it is the right decomposition and supplies
the specific mechanisms for each stage.

### 2.2 Stage A — habitat fields, and the one number the ecology paper gives us

**Selected: height above river (HAR) as the primary habitat axis.**

**Problem solved.** Gives vegetation a reason to be where it is, from a quantity we are already
computing.

**Input.** `HAR = terrainHeight(x,z) - waterSurfaceElevation(s(x,z))`, a by-product of §1.2.

**Why HAR and not elevation or distance-to-river.** This is the actual contribution of Bair et al.
(*Ecosphere* 2021), and it is worth more than the rest of the ecology reading combined. They relate
ground **height above river** — explicitly as a *groundwater proxy* — to observed vegetation cover
types, and find that **an elevation difference of ≥ 0.5 m between adjacent ranked cover types is
what best defines the zone boundaries**. Not distance to the channel; height above it. A point 40 m
from the river on a flat floodplain is wetter than a point 10 m from it up a bank, and HAR captures
that where lateral distance does not.

**Output.** A continuous scalar, per query, no storage.

**The zone ladder**, adapted (the brief says adapt artistically, and this is a fantasy valley):

| zone | HAR band | character |
|---|---|---|
| channel / emergent margin | ≤ 0 | water; reeds and lilies at the edge |
| wet margin | 0 – 0.5 m | the capillary-fringe band; densest, brightest, most saturated |
| mesic riverbank | 0.5 – 2 m | the riparian corridor proper; broadleaf, groundcover |
| transitional | 2 – 8 m | mixed; where the woodland starts |
| xeric slope | 8 m+ | sparse, wind-shaped, silhouette species |

The 0.5 m figure is the paper's, used as the paper uses it: as the width of the band nearest the
channel, not as a spacing for the whole ladder.

**Cost.** One subtraction on top of a query we already make. Runs at generation.

**Other fields, and the smallest useful set.** The brief lists twelve candidate inputs and says
choose the smallest useful set. Four:

| field | source | what it decides |
|---|---|---|
| `HAR` | §1.2 | the zone ladder above |
| `slope` | `length(∇h)`, two extra height queries | nothing tall stands on a steep face; nothing at all above a cutoff |
| `macro` | one low-frequency noise octave | stands and glades (§2.3) |
| `clearance` | `world::ScatterClearance`, already exists | negative space, staged by hand |

Rejected as inputs: curvature (expensive — second derivatives — and the brief's readable composition
does not turn on it); a sun/exposure proxy (it is night, and the moon is a single fixed direction,
so exposure is a constant plus slope, which we already have); distance-to-hero (better handled as an
exclusion in Stage F than as a field, because it is a hard rule, not a gradient).

### 2.3 Stage B — macro-clustering

**Selected: one low-frequency noise octave, thresholded with a soft shoulder, per species group.**

**Why this and not the alternatives.** Voronoi cluster centres give patches with straight-ish edges
and a characteristic cell size that the eye finds after about thirty seconds. Explicit cluster-centre
lists are authorable but do not scale and are one more thing to seed. A single octave at a
wavelength of 40–120 m, remapped through a smoothstep, gives patches with organic boundaries, a
controllable coverage fraction (the threshold) and a controllable edge softness (the shoulder width)
— and it is one noise evaluation.

**The rule that makes it work:** the macro octave's wavelength must be **much larger than the
species' own spacing**, and the *only* noise in the pipeline at frequencies near the spacing is the
sampler's own randomness. The brief's §5.2 prohibition on "a high-frequency noise field as the only
placement rule" is right, and the reason is specific: noise thresholded near the spacing scale
produces a texture, and a texture placed in 3D reads as a pattern.

**Different species take different macro fields** — different seeds, different wavelengths,
different thresholds — so that a fern patch and a tree stand are not the same patch. Two species
that should associate share a field; two that should avoid each other share a field and take
opposite sides of the threshold. That is Stage E's association rule obtained for free, without a
neighbourhood query.

**Cost.** One noise sample per candidate. Runs at generation. Deterministic by construction.

### 2.4 Stage D — the sampler

**Selected: Bridson-style grid-accelerated dart throwing, with a per-sample variable radius.**

**Problem solved.** Minimum separation, with the radius varying by species and by local density.

**Inputs.** A domain, a radius function `r(x, z, species)`, a seeded RNG, an acceptance predicate
(the density field of Stage C).

**Output.** Point positions. Transforms, not geometry.

**Complexity.** `O(N)` — a background grid of cell size `r/√2` means each candidate checks a
constant number of neighbours. For variable radius the grid uses `r_min/√2` and a candidate checks a
neighbourhood sized by `r_max/r_min`, which is `O(1)` as long as that ratio is bounded. **It must be
bounded, and enforcing that is a real constraint**: a species whose radius varies 20× across the map
turns the sampler quadratic in the dense region. Cap the ratio at ~4 and split anything wider into
two species.

**Why this over everything Lagae & Dutré compare.** Their survey is thorough and its conclusions are
clear, and none of them point at us:

| method they compare | their verdict | why not us |
|---|---|---|
| dart throwing | ρ up to 0.75, good spectrum, **termination not guaranteed**, ">100,000 points very difficult" | the termination problem is real; Bridson fixes it with a k-attempt bound they did not have in 2007 |
| relaxation dart throwing, Lloyd's | good radius, slow, "non-interactive only" | an iterative relaxation is a determinism liability for no visual gain at our scale |
| Jones / Dunbar–Humphreys accelerated | "suited for interactive", 3.5 s for 16,384 points | that is 2007 silicon; and the scalloped-sector data structure is a lot of code |
| tiled blue noise | "**recommend not to use**" — low radius, bad spectrum | settled by the authors |
| edge- and corner-based Poisson tiles | corner-based is "the best tile-based approach" | see §3.4 — we don't need tiles |
| Ostromoukhov / Kopf hierarchical | spectra "below average", radius "surprisingly low"; "the real power is varying density" | the varying density is the attractive part, and we get it from the acceptance predicate instead |

Their governing conclusion — *"for real-time applications, and applications that require large
Poisson disk distributions, tile-based approaches are the only option"* — is **conditioned on
generating the distribution in real time.** We generate it once, at load, for a scene the engine
already spends 397–775 ms building (`src/scene/composition.cpp:4779`). A Bridson pass over 10⁵
candidates is single-digit milliseconds on an M-series core. The constraint the paper is written
against does not exist here.

**The quality target, stated as a number.** Lagae & Dutré's *relative radius* ρ is the right metric
and we should adopt it: with `r_max = √(A / (2√3 · N))` the maximum-packing radius for `N` points in
area `A`, ρ = r / r_max. They establish the good window as **0.65 ≤ ρ ≤ 0.85** — below 0.65 the
distribution is not visibly blue-noise, above 0.85 it starts to look like a lattice. That gives us a
unit test with a real acceptance criterion instead of "looks about right", and it gives each species
a principled way to pick its radius from its target density rather than by eye.

**Seeding.** One stream per species per chunk, derived by hashing `(worldSeed, speciesId, chunkId)`,
so adding a species does not move any other species' points and regenerating one region does not
require the whole map. This is the determinism property the brief's §5.5 asks for and it is cheap;
the alternative — one global stream — makes every point dependent on the order the species were
declared in.

**Limitations.** Bridson gives *uniform* blue noise inside the accepted region; all the structure
comes from the density field and the acceptance predicate. If those are flat, the result is flat and
even, which is exactly the failure the papers warn about. **The sampler is not the ecology.** It is
the last stage, and if it is doing the interesting work then Stages A–C are broken.

### 2.5 Stage E — competition, from the FON model

**Selected: a single-pass Field-of-Neighborhood acceptance test. Rejected: iterative FON growth.**

**What FON is.** Berger & Hildenbrandt's field-of-neighbourhood, as used by the
ecosystem-cover paper the brief cites: each plant carries a circular zone of influence of radius
`R = a · (stem radius)^b`, over which a scalar field decays **exponentially from 1 at the stem to
`F_min = 0.1` at `R`**. Two plants interact when their zones overlap. For a focal plant, the
neighbours' fields are **additively superposed**, and the sum becomes a correction factor on its
growth rate. Iterated, plants in crowded neighbourhoods grow less, and those that never reach a
minimum radius are eliminated — which is how the cover paper maximises canopy coverage.

**What we take, and what we leave.** We take the field and the superposition. We leave the
iteration.

```
accept(candidate) :
    P = Σ over neighbours n within R_n of  F_n(|candidate - n|)
    scale = clamp(1 - P / suppression, minScale, 1)
    if scale < minViableScale : reject
    else : place at `scale`
```

One pass, in placement order, largest species first. A candidate near a big neighbour is either
placed smaller or not placed at all. This gives, with no iteration:

- big plants not on top of one another (their fields are large, so they suppress each other hard);
- small plants tolerated near big ones (a small plant's `minViableScale` is reached at a higher
  suppression);
- mushroom and flower clusters permitted (set `suppression` high for a species and its own field
  barely suppresses its kin);
- thinner groundcover under large stems, automatically;
- the size variation of §5.3 coming out of the ecology rather than being sprinkled on afterwards.

**Cost.** One radius query into the same background grid the sampler already built. `O(1)` per
candidate, so `O(N)` overall — the same order as the sampler, and it can run *inside* the sampler's
accept step rather than as a second pass.

**Why not the iteration.** The iterative version's product is *canopy cover maximisation* — it is
solving a packing problem for a forestry question. Ours is a cinematic valley whose brief says in
§4.5 that negative space is a design feature and in §5.8 that filling the map is prohibited.
**Maximising cover is the opposite of the brief.** Beyond that, an iterative scheme makes every
plant's final state depend on every other plant's history, which is a determinism obligation and a
regeneration cost for a result we do not want.

**Limitation.** Placement order matters, so "largest first" is load-bearing: reverse it and small
plants claim the ground and nothing large can be placed. That must be a documented invariant and a
test, not a coincidence of the loop.

### 2.6 Rejected: neighbourhood-histogram texture synthesis

Zhang et al.'s *Sample-Based Vegetation Distribution Information Synthesis* (PLOS ONE 2015).

**What it does.** Treats a plant layout as a 2D vector pattern and synthesises a large one from a
small sample by texture synthesis. Each element gets a **neighbourhood histogram** — counts of
neighbours in 24 grid cells formed by 8 radial lines × 3 concentric shells. Synthesis grows outward
from a seed element, at each step matching the frontier element's histogram against all sample
histograms under a similarity-matrix-weighted Euclidean distance (σ = 5.0), then copying the best
match's neighbours into place. ~8,000 elements in 2–3 seconds.

**Why it is genuinely attractive.** It is the only method read that can reproduce a *real measured
forest's* spatial statistics, and it reproduces them implicitly — no explicit topology, no Delaunay,
so no error accumulation. If we had a photograph of a bioluminescent valley, this would be how to
match it.

**Why it is rejected.** Four reasons, in order of weight.

1. **It needs a sample, and there is no sample.** The input is field-surveyed tree positions or a
   hand-designed patch. For a fantasy valley, the sample would itself be authored — and authoring a
   patch and then synthesising more of it is a much more expensive way to get what §2.3's macro
   field gives directly.
2. **It reproduces *one* stationary pattern.** Our whole design is that the pattern must change with
   HAR, slope and zone. Making the synthesis non-stationary means multiple samples and a blend, and
   the paper does not address that.
3. **The authors name the matching limitation themselves**: the method "primarily captures local
   distribution features while neglecting overall statistical characteristics." Global structure is
   precisely what the brief's §4 is about.
4. **It is sequential frontier growth**, which is a poor fit for regenerating one chunk.

**The idea worth stealing.** The 24-cell radial histogram is a *good validation instrument* even
though it is a poor generator. Computing it over our output and over a reference gives a real,
numeric answer to "does this look naturally distributed" — see `04-plan.md`'s acceptance criteria.

### 2.7 Adopted conceptually: hierarchical tile-level generation

GPUOpen's work-graph article, read for its architecture rather than its API.

**The transferable shape** is its four-level pyramid — world → chunk → biome-specific tile → detailed
tile — and specifically two decisions inside it: **the biome is classified once per tile from the
dominant weight at the tile centre**, and **distant detail is represented by a different, cheaper
thing rather than by the same thing at lower density** (their 16×16 grass patch becomes one quad).

We take the classification: deciding a chunk's dominant zone once and then running that zone's rules
over the whole chunk turns a per-candidate branch into a per-chunk branch, and it is what makes
chunk-local regeneration possible at all. We do **not** take dense/sparse representation switching —
ADR-151 and ADR-153 measured that band on this exact content and found it worth +1.8%, i.e. nothing.

We also do not take work graphs, obviously, and we do not take GPU generation. ADR-029 exists and
GPU point processing is available, but the brief's §5.4 asks for generation and rendering to be
separated, and the CPU already generates this scene in well under a second.

### 2.8 Rejected: Wang tiles

Deng et al.'s *Silva* system generates variable-radius Poisson-disk distributions on Wang-tile sets
without global optimisation, and renders hundreds of square kilometres through multi-level instancing
and nested kd-trees.

**Rejected because the problem it solves is not ours.** Wang tiles exist so that an unbounded or
streaming world can be populated without storing it. Our map is a bounded, authored, cinematic set
of order 1 km, generated once. Tiles would buy us nothing and cost a real problem: **a tile set has
to be seam-free, and our distribution is non-stationary by design** (HAR, slope, zone), so every tile
would need per-zone variants and the corner-matching machinery multiplies with them.

**The one idea adopted** is the kd-tree/spatial-partition observation, which is just the background
grid the sampler needs anyway, reused for culling.

**Revisit when** the map needs to be much larger than the camera can see in one shot, or streamed.

---

## 3. Hero mushrooms

### 3.1 What already exists, and the one thing missing

`docs/visual-cookbook/fungi.md` records the engine's existing answer and it is a good one:

> **A cap is a radius profile, not a new primitive.** A tube swept along a curve whose control points
> each carry a `scale` is a lathe of arbitrary profile. `scale = cos(u·π/2)^0.55` is a dome; reverse
> it for a bowl; raise the exponent to flatten.

That is the same construction the two mushroom references arrive at independently:

- **Desbenoit et al.** (*Interactive Modeling of Mushrooms*, EG 2004) build the surface by rotating
  **one silhouette curve, split into three sub-curves for stem / gills / cap**, around a **2D spline
  axis `S`** rather than a straight line — at each silhouette sample `p_i` projected to a skeletal
  point `s_i`, the mesh vertices are `m_ij = R(θ_j)(p_i)` with `θ_j = 2πj/s` about the local tangent.
  So: a profile swept about a *curved* axis. That is the engine's tube-with-scaled-control-points,
  exactly. Then axial deformations (Lazarus et al.) for bend and twist, and local FFDs for
  imperfections.
- **proc-shrooms** (Blender, source read) parameterises the cap as **two cubic-Hermite profiles** —
  an upper and a lower hat sharing endpoints — with the free parameters being the *tangent angles at
  the centre and at the rim* (`aUpper0`, `aUpper1`, `aLower0`, `aLower1`), plus centre thickness
  `h0`, rim height `h1` and `radius`. The stem is a 3-point radius spline with a `bulgePosition` and
  `bulgeWidth`, swept by a Frenet-frame tube.

**The tangent-angle parameterisation is the best idea in either reference and should be taken
directly.** It is why proc-shrooms can produce a flat parasol, a conical ink cap, a bell and a
recurved chanterelle from the same four numbers: the *rim tangent* is what a mushroom's silhouette
identity actually lives in. Control points with scales can express those shapes but not
*parameterise* them — you cannot ask a points list for "20° more rim droop".

**The missing primitive.** proc-shrooms' `screw()` takes an optional **`rScale`: a spline over
[0, 2π] giving a radial scale at every angle**, applied as `verts[v].xy *= rScale(angle)`. A closed
Hermite loop of 8 random values in [0.9, 1.1] breaks the cap's circular rim. Our tube sweep has
**no per-angle term** — every ring is a circle. That is precisely the defect visible in the baseline
capture (`03-baseline.md` §3): the elder's cap is a perfect ellipse. The elder's current answer is
two displacement deformers on a sphere, which adds noise but cannot express *designed* asymmetry —
a cap that droops more on one side, a lobed rim, a torn edge.

**This is the one new engine capability the whole brief requires**, and it is small: an optional
periodic radius modulation on the sweep, evaluated per (angle, arclength). Two channels — a
low-harmonic term for designed lobes and asymmetry, and a noise term for breakup.

### 3.2 The selected generator

**A parameter set, a seed, and four swept surfaces.**

| part | construction | key parameters |
|---|---|---|
| stem | tube along a Catmull-Rom curve with a radius profile | height, base/mid/tip radius, bulge position and width, lean vector, curvature, per-angle flute |
| cap upper | tube along a short vertical curve, radius profile from a Hermite with tangent angles | radius, centre thickness, rim height, centre and rim tangent angles, rim lobes (count, depth), tilt |
| cap lower | the same profile, offset inward and down | underside depth, gill-attachment radius |
| gills | radial distribution of thin tubes between the two cap surfaces | count, thickness, taper, forking |

**Why gills are geometry and not a texture.** Desbenoit et al. texture the gills from a photograph,
and for a mushroom in the middle distance that is correct and cheap. The brief asks for close-up
cinematic subjects, and `docs/visual-cookbook/bioluminescence.md` records what was measured on this
exact problem: emission on a smooth dome "reads as a lamp"; the version that "reads as an organism"
is the one where the light comes out of sixty-four hanging filaments. The conclusion there is stated
as a law and it should govern the whole family:

> **Give the light a structure to come out of. A glowing surface reads as paint; a glowing structure
> reads as biology.**

The same document records the trap: gills were added once and were *invisible*, because the glow
dome enclosed them. Structure that the geometry hides is worth nothing. So the gill geometry and the
cap's underside have to be designed together, and the test is a render from below, not a parameter.

**Cost.** `docs/visual-cookbook/fungi.md` and the elder both demonstrate the budget: the elder is a
96×48 sphere plus a 32×40 tube plus 38 six-sided tubes. A hero mushroom at 32 radial × 24 rings for
each cap surface plus 48 gill blades is order 10⁴ triangles. Six of them is order 10⁵ — comparable to
the *entire current scene's* 273,819, which is too much if all six are on screen at once and fine if
they are staged so that at most two are. **Staging is the budget mechanism**, which is also what the
brief's §7 distribution section asks for.

**Determinism and identity.** A hero is `(generatorVersion, parameterSet, seed)`. The parameter sets
for the six designs are authored data, checked in, not random draws that happened to look good —
which is how you can come back in a month and get the same mushroom. proc-shrooms' `mutate` and
`procreate` (Gaussian perturbation of a random subset of parameters with a soft-bound clamp;
per-parameter crossover between two parents) and Desbenoit's silhouette morphing are both good
*authoring* tools for *finding* those six sets. They should not be in the runtime path.

**Limitations.** No subsurface scattering in the engine (`fungi.md` names this). Fungal flesh is
translucent and the cap edge should transmit; the approximation available is a rim term driven by
`N·V` and thickness, which is a material trick, not transmission. It will read acceptably at the cap
edge and will not read at all for a backlit whole cap.

---

## 4. The mushroom candidate search — generate many, score them, curate six

The brief's addendum is right that this is the part that decides whether the six heroes look
authored, and it is a Phase 1 architecture deliverable. This section is that architecture.

One framing first, because it determines the whole shape:

> **The generator is the search space, the scorer is not the art director — the scorer is a filter
> that removes the obviously bad so that a human looks at twenty candidates instead of two hundred.**
> Every automated aesthetic score in this section is a *rejection* instrument. None of them is
> permitted to promote a candidate to hero on its own. §4.9 makes that a rule with a mechanism
> rather than a hope.

### 4.1 The pipeline

```
ParameterSchema  ──▶ Sobol sample ──▶ build ──▶ validity gate ──▶ score ──▶ feature vector
                        (§4.3)        (§3.2)       (§4.4)        (§4.5)        (§4.7)
                                                      │
                                                      ├─ invalid: recorded with its reason, never silently dropped
                                                      ▼
                              diversity selection (§4.7) ──▶ contact sheet (§4.6) ──▶ human picks six (§4.9)
                                                                                            │
                                                                        canonical parameter sets (§4.10)
```

Every stage is a pure function of `(schema, generatorVersion, index)`. Nothing is stateful, nothing
is iterative, and **a candidate's identity is its Sobol index** — "candidate #184" is reproducible
forever from three numbers, without storing a mesh.

### 4.2 What should be generic, and the convergence with the Tree of Life

A parallel agent is building a procedural Tree of Life against a structurally identical requirement.
The two must not ship two candidate-search frameworks. The seam is clean, because **everything
except the organism is organism-independent**:

| generic — belongs in a shared `search::` module | mushroom-specific |
|---|---|
| `ParameterSchema` — named, bounded, typed parameter declarations with a stable order | the schema's *contents* (§4.3) |
| the Sobol sampler and the index→parameters mapping | — |
| **mesh validity gate** (§4.4) — NaN, degenerate triangles, normals, bounds, connectivity | the *plausibility* rules (stem supports cap, growth direction) |
| the **scoring harness**: named components, weights as data, every component stored | the component *implementations* that need domain semantics |
| silhouette, surface, artifact and **performance** scoring — all defined on a mesh and an image | cap/stem proportion, gill quality, underside legibility |
| feature-vector clustering and farthest-point selection | the feature vector's *entries* and their meanings |
| contact-sheet rendering through the existing offline path | the lighting preset used (§4.6) |
| the serialisation record (§4.10) | — |

The proposed interface is one concept with four members, so either organism plugs in:

```
concept CandidateGenerator = requires {
    schema()                      -> const ParameterSchema&;
    build(const Parameters&)      -> Result<Candidate>;      // mesh(es) + material params
    features(const Candidate&)    -> FeatureVector;          // the diversity axes
    domainScores(const Candidate&)-> std::vector<ScoreComponent>;   // may be empty
};
```

`search::run<Generator>(config)` owns sampling, the gate, the generic scores, clustering, the
contact sheet and serialisation. **I have not built this and must not build it in the other agent's
worktree.** The recommendation is that whichever phase lands second adopts the first one's
`search::` module, and that the first one to land keeps the four-member seam above even if it only
ever has one generator — a flagged open question (`04-plan.md` Q6).

The genuinely mushroom-specific parts are small: the schema, the builder, and roughly three of the
eight score components. That is the evidence that the split is in the right place.

### 4.3 The parameter space — small, ordered by influence, and that ordering is load-bearing

The addendum lists ~40 candidate parameters and warns against exposing hundreds of meaningless ones.
The proposal is **18**, in four groups, and they are listed in **descending order of visual
influence** for a reason given below.

| # | parameter | range | why it is in |
|---:|---|---|---|
| 1 | `capRadius / stemHeight` (aspect) | 0.25 – 1.6 | the single strongest silhouette determinant |
| 2 | `capRimTangentDeg` | −70 – +40 | **the identity parameter.** Parasol vs bell vs conical vs recurved chanterelle is the rim tangent, per proc-shrooms |
| 3 | `capCentreTangentDeg` | −40 – +40 | domed vs flat vs depressed centre |
| 4 | `capThickness` | 0.04 – 0.35 | of cap radius |
| 5 | `stemCurvature` | 0 – 0.45 | a straight stem reads as a cylinder (`fungi.md`) |
| 6 | `stemTaper` | 0.4 – 1.5 | <1 narrows upward, >1 flares |
| 7 | `stemBulgePosition` / 8 `stemBulgeWidth` | 0.15 – 0.85 / 0 – 0.35 | the volva/ring family |
| 9 | `capLobeCount` | 0, 3 – 9 | integer. 0 = circular rim |
| 10 | `capLobeDepth` | 0 – 0.30 | **the new primitive's parameter** (§3.1) |
| 11 | `capTiltDeg` | 0 – 18 | asymmetry that is *designed*, not noise |
| 12 | `capEdgeWaviness` | 0 – 0.20 | higher harmonic on the same per-angle term |
| 13 | `surfaceNoiseAmp` / 14 `surfaceNoiseScale` | 0 – 0.06 / 0.8 – 6 | the breakup term |
| 15 | `gillCount` | 24 – 96 | integer |
| 16 | `gillDepth` | 0.15 – 0.75 | of cap thickness |
| 17 | `emissionStructure` | enum: gills · rim · veins · filaments | *where* the light comes out (§3.2's law) |
| 18 | `emissionIntensity` | 0.8 – 6.9 | bounded by the scene's own ladder |

Colour is **not** a free parameter. It is drawn from `world::PaletteRoles` — the five authored
Glowmere colours — because a hero that invents its own hue breaks the reserve-accent rule that makes
the scene work (`01-audit.md` §1.13). "Random rainbow coloration", which the addendum asks us to
reject, is prevented by construction rather than penalised after the fact. That is the better place
to solve it.

**Growth stage and species identity are deliberately excluded** as parameters. Six hero designs are
six authored parameter sets; a "species" axis would be a seventh way of saying the same thing.

### 4.4 Sampling: a scrambled Sobol sequence. Not uniform random, and — provisionally — not MAP-Elites.

**Selected: Sobol low-discrepancy sampling over the unit hypercube, mapped through each parameter's
range.**

**Why low-discrepancy over uniform random.** Uniform random in 18 dimensions clumps and leaves
holes; the addendum's list of failure modes ("near-duplicates", "boring forms") is largely a
description of clumping. A low-discrepancy sequence is constructed to have bounded discrepancy at
every prefix length.

**Why Sobol over Latin hypercube, which is the obvious alternative.** Two decisive properties.
**Sobol is extensible:** any prefix of the sequence is well-distributed, so a 200-candidate run can
be grown to 400 by appending indices 200–399, and every earlier candidate keeps its identity. LHS
must commit to N up front and a different N is a different sample, so "candidate #184" would stop
meaning anything. And **Sobol is indexable**: point *i* is computable directly, so candidates
parallelise with no shared state and the record in §4.10 needs one integer.

**Why the parameter ordering in §4.3 matters.** Sobol's low-dimensional projections are much better
than its high-dimensional ones, and quality degrades with dimension index. Assigning the most
visually influential parameters to the lowest Sobol dimensions means the stratification is spent
where it buys the most. This is why §4.3's table is ordered rather than alphabetical, and it should
be a comment in the schema, because an innocent-looking reorder silently degrades the sample.

**Why not MAP-Elites — provisionally, with the measurement that would overturn it.** Quality-diversity
is genuinely the right *framing*: the goal is not the single best mushroom but many excellent ones in
different morphological regions, which is exactly what Mouret and Clune's illumination framing
describes. The literature is also clear that MAP-Elites beats random sampling on coverage. But the
mechanism of that advantage is conditional, and the condition is stated in the QD literature itself:
MAP-Elites wins because **mutating a member of a diverse population is more likely to fill a new cell
than sampling the genotype space is — "especially if cells are more likely to be filled by mutating a
nearby cell than by randomly sampling from the space of all possible genotypes."**

That condition is weak here, and deliberately so:

1. **The space is designed and bounded.** Every range in §4.3 was chosen so that its endpoints are
   both usable. There is no vast invalid region for a search to have to escape.
2. **The genotype→behaviour map is close to monotone.** Aspect ratio is nearly `capRadius/stemHeight`;
   asymmetry is nearly `capTiltDeg` plus `capLobeDepth`. When behaviour descriptors are near-monotone
   in parameters, a stratified sample *in parameter space is already a stratified sample in behaviour
   space*, which is the whole prize.
3. **Eighteen dimensions and a few hundred evaluations** is a small budget for an evolutionary loop
   and a comfortable one for a low-discrepancy sequence.
4. **MAP-Elites is stateful and order-dependent.** Its archive depends on evaluation order, which is a
   determinism obligation this repo takes seriously, for a benefit not yet shown to exist here.

**The falsifiable test, which must be run in Phase 4 before this decision is treated as settled:**
bin the valid candidates into the behaviour grid of §4.7 and report **cell coverage** — the fraction
of *reachable* cells occupied. If a 500-candidate Sobol run covers the reachable grid well, MAP-Elites
has nothing to add and the simple pipeline stands. **If coverage is poor and the empty cells are
reachable** — provably so, by hand-authoring one parameter set that lands in an empty cell — then
assumption 2 is false, the map is not near-monotone, and MAP-Elites earns its place. The upgrade is
then cheap, because the schema, the builder, the gate, the scorer and the feature vector are all
reused unchanged; only the sampler is replaced. **That is the reason to build the pipeline in this
order.**

### 4.5 The validity gate — cheap, total, and it records why

Runs before any scoring, because scoring a broken mesh is wasted work and a broken mesh can score
well. Two tiers.

**Tier 1, mesh hygiene — generic, and a hard reject:** any non-finite vertex or normal; any index out
of range; degenerate triangles above a small fraction of the total (`meshMetrics` already counts
triangles and sums area, and a zero-area triangle contributes zero area while still counting as a
triangle — so `meanTriangleArea()` falling off a cliff *is* the degeneracy signal); unnormalised or
discontinuous normals; bounds outside a sane box; triangle count above a ceiling; disconnected
components where one is expected.

**Tier 2, organic plausibility — mushroom-specific, and a soft reject:** the cap's centroid lies over
the stem's footprint; the cap's underside does not intersect the stem below the attachment; gills lie
strictly between the two cap surfaces; the thing has a growth direction (the stem's tangent at the
base is within some cone of vertical).

The addendum is right that these must not be tightened into realism. The rule adopted is: **Tier 2
rejects things that are *incoherent*, never things that are *strange*.** A cap lobed into six
drooping petals is strange and passes. A cap floating half a metre above a stem it never touches is
incoherent and fails.

**Every rejection is recorded with its reason and its parameter values.** A rejection histogram over
several hundred candidates is the single most useful diagnostic the pipeline produces: if 40% fail on
"cap does not overhang stem", the `capRadius/stemHeight` range in §4.3 is wrong, and that is a
schema bug being reported rather than a mystery.

### 4.6 Rendering candidates — through the existing offline path, not a second one

The engine already has everything needed and a second capture path would be a defect.
`app::Engine(EngineMode::Offline)` plus `SceneRenderer::renderToImage(scene, time, w, h)` plus
`assets::writePng` is exactly what `tests/rendering/test_representation_ceiling_perf.cpp` uses to
write the captures this phase's baseline was read from. The candidate tool is that, in a loop, with a
fixed camera.

**Six views per candidate**, as the addendum asks: front, three-quarter front, side, rear
three-quarter, low angle, slight top-down. The low angle matters most and is not optional —
`docs/visual-cookbook/bioluminescence.md` records that gills were once added and were *invisible*,
because the geometry above enclosed them, and a scene-height camera would never have caught it.

**Three lighting conditions**, and the third is the one that decides:
1. **Neutral** — a flat studio rig, for silhouette and proportion, where a mask is a mask.
2. **Glowmere moonlight** — `glowmere-valley.rig.json`'s exact rig, AgX, `chromaRetention 0.6`,
   bloom 0.18 at threshold 1.0, exposure −1.3 EV.
3. **Glowmere bioluminescent** — the same, with the surrounding ecology light field's contribution
   approximated by a small number of local emitters.

The addendum's warning that "some procedural materials look good under generic lighting and fail
under Glowmere's actual rendering conditions" has a specific mechanism here worth naming: **the bloom
threshold is 1.0 in exposed-linear units and `bloomEmissionWeight` is 0.75**, so a candidate whose
emission sits at 0.9 has no halo at all and a candidate at 3.0 has a large one. That cliff is
invisible under a neutral rig. It is the reason condition 3 exists and the reason §4.8 is a separate
pass rather than a weight.

**Silhouette extraction is free.** MRT target 4 is an R32Uint identifier buffer (low 16 bits object
id). A binary mask of the candidate is a threshold on that target — no chroma keying, no background
assumptions, no alpha compositing.

### 4.7 Scoring — every component is a band, never a monotone

**The architectural rule that prevents the failure mode the addendum warns about twice** ("do not
blindly optimise toward more edges", "do not automatically reward higher tessellation"):

> **No score component is monotone increasing. Every component scores the distance from a target
> band, and falls off on both sides.**

A monotone component is an instruction to maximise, and maximising edge density produces visual
noise, maximising asymmetry produces broken forms, maximising tessellation produces cost. A banded
component says "about this much", which is what an art direction actually is. Each component is
therefore `(lowEdge, lowPlateau, highPlateau, highEdge)` — a trapezoid — and all four numbers are
data, not code.

Eight components, each stored separately, per the addendum's §9:

| component | measured on | what the band expresses |
|---|---|---|
| **silhouette** | the binary mask, all six views | isoperimetric quotient `4πA / P²` — the classic compactness measure. A circle is 1.0; a spiky form approaches 0. The band sits well below 1 (a disc on a stick is a bad silhouette) and well above the noise floor. Plus **multi-view variance** of the quotient: a hero must stay interesting as the camera moves, so *low* variance scores well and a candidate that is a slab from one angle fails |
| **proportion** | the mesh | cap width ÷ stem height, cap thickness ÷ cap radius, centre-of-mass height ÷ total height. The addendum's "ball on a stick" and "umbrella on a pole" are two specific bands to sit outside of, and they are expressible as exactly these ratios |
| **organicity** | the mesh | the ratio of deformation energy across three scales (large-scale tilt and lobes, medium-scale waviness, small-scale noise). The band demands **all three present and none dominant** — which is precisely the addendum's "combines large-scale shape variation, medium-scale deformation and small-scale surface detail rather than noisy displacement everywhere" |
| **structure** | the mesh | gill count and depth legible at the intended distance; underside not occluded (measured from the low-angle view's mask, not assumed) |
| **surface** | the mesh | normal continuity across edges; faceting measured as the distribution of dihedral angles, banded so that *some* hard edges score well — a cap rim wants one |
| **material** | the Glowmere-lit renders | cap/stem colour separation in OKLab; emission confined to a structure rather than spread over a face; saturation inside the palette's own range |
| **cinematic** | the six views | how many of the six produce a readable composition — a distinct cap profile, visible stem curvature, an interesting underside. Scored per view and summed, so "excellent from one angle" cannot win |
| **performance** | `scene::meshMetrics` | **not triangle count.** `docs/renderer-upgrade/01-audit-and-baseline.md` §4.5 measured that this renderer's fragment cost tracks *triangle size*, not pixel count: at identical coverage, 2.1 M triangles cost 4.9× what 2,048 did. So the right quantity is **projected pixels per triangle at the intended hero distance**, which `MeshMetrics::meanTriangleArea()` and `boundsRadius` give directly. A candidate below the 2×2-quad knee is paying the quad-overdraw penalty and is penalised for it, however pretty it is |

Penalties (artifact, degeneracy, complexity) are subtracted and are **not** banded — a penalty is
correctly monotone, because there is no such thing as too little of an artifact.

Weights are data in the pipeline's config file and are explicitly expected to be wrong at first. The
addendum's instruction to store components rather than the total is right and is worth restating as
the reason: **when the pipeline picks a bad candidate, the component breakdown says which score lied**,
and that is the only way the weights ever improve.

### 4.8 Diversity selection — farthest-point over a normalised feature vector

**Selected: score → keep the top ~40% → farthest-point traversal in feature space. Rejected: top-six
by score; rejected: k-means then best-per-cluster.**

The feature vector is **13 dimensions**, each normalised to its observed range across the survivors
so that no axis dominates by unit: `capWidth`, `capHeight`, `capThickness`, `stemHeight`, `stemWidth`,
`stemCurvature`, `capAsymmetry`, `capTilt`, `edgeWaviness`, `gillDensity`, `emissionIntensity`,
`overallAspectRatio`, `silhouetteComplexity`.

**Why farthest-point and not k-means.** k-means into six clusters and then the best of each is the
obvious approach and it has a specific failure: cluster *sizes* are unequal, so a dense region of
near-identical good candidates can own two clusters while a sparse, genuinely distinct region owns
none. Farthest-point traversal — seed with the highest scorer, then repeatedly add the survivor
maximising `min` distance to everything already chosen — maximises the minimum separation directly,
which is the property actually wanted. It is also deterministic given a seed, and `O(n·k)`.

**The quality/diversity trade is one number and it is explicit:** each candidate's selection value is
`α · normalisedScore + (1−α) · normalisedMinDistance`. `α = 1` is top-six-by-score; `α = 0` is
diversity with no regard for quality. The value is config, it gets tuned by looking, and the tuning
is recorded. Hiding this trade inside an algorithm is how a pipeline ends up with six excellent
near-identical mushrooms or six diverse ugly ones.

**The archetype check.** The addendum's §12 names six morphological identities — broad and low, tall
and elegant, highly asymmetric, clustered, heavily folded, delicate and hanging. These are not the
selection mechanism, but they are the **acceptance test on the selection**: after the six are chosen,
each must be nameable as one of those, and two that get the same name mean the selection failed
however good the distance numbers look.

### 4.9 The human stays in the loop, and the mechanism is the contact sheet

The addendum's §16 is the most important paragraph in it and it needs a mechanism, not an intention.

The pipeline's terminal output is **a contact sheet**: a grid of thumbnails, each labelled with its
Sobol index, its overall score and its eight components, rendered under Glowmere conditions, ordered
by selection value. The final six are marked. **It is a proposal, not a result.**

The rule: **a candidate becomes a hero when a person has looked at it, and the reason is written
down.** Where the human overrides the ranking — and they will — the override is recorded in the
canonical record (§4.10) as a note: what was promoted, what it displaced, and why. Over a few
iterations those notes are the specification for fixing the weights, which is the only mechanism by
which an aesthetic scorer ever gets better.

No dedicated ImGui panel is proposed for Phase 4. A contact sheet PNG and a JSON table delivers
everything above; the addendum's §14 explicitly permits an offline tool instead of an editor, and the
editor is not where the value is.

### 4.10 The canonical record

Per selected hero, checked in as data:

```
generatorVersion · schemaHash · globalSeed · sobolIndex · parameters{18}
materialParameters · featureVector{13} · scores{overall + 8 components}
meshMetrics{triangles, surfaceArea, meanTriangleArea, boundsRadius}
lightingConditionsRendered · selectionNote (why a human chose it)
```

**No mesh is stored.** The mushroom is the parameters, and `(generatorVersion, schemaHash,
sobolIndex)` regenerates it exactly. `schemaHash` is what makes that honest: change a parameter's
range and the hash moves, so a stale record announces itself instead of silently regenerating a
different mushroom under the same name. This is the same discipline
`scene::MeshMetricsCache`'s `(identity, meshVersion)` key exists to enforce, and the reason is
identical — `docs/renderer-forensics-report.md` found nine defects of the shape "a derived copy with
a lifetime longer than the thing it was derived from".

### 4.11 What this section does not answer

- **The weights.** They are declared to be data and declared to be wrong. Tuning them needs
  candidates on a screen, which is Phase 4.
- **Whether 200 or 500 candidates is right.** Depends on generation time, unmeasured. The Sobol
  choice makes this cheap to defer: start at 200 and append.
- **Whether the `search::` module lands here or in the Tree of Life.** `04-plan.md` Q6.
- **Whether image-level metrics beyond the silhouette mask earn their place.** `tools/image_stats.py`
  already reads a PNG with no dependencies and reports luminance statistics, so the cost of trying is
  low — but the addendum's caution applies and nothing here depends on the answer.

---

## 5. The Auto-director and the continuous shot

### 5.1 The finding that reshapes this section

The existing director **already has a `continuous` flag, it already defaults to `true`, and it is
invisible to every user** — `DirectionBrief::continuous`, `src/app/cinematic.hpp:258`, applied at
`src/app/cinematic.cpp:1138-1153`. What it does is pin each shot's start position to the previous
shot's end position and bow a straight move into an arc.

So the camera's *position* is already continuous. What is not continuous is its **velocity**: every
shot re-eases from zero (`ease(0) = 0`), so the camera stops dead at every section boundary and
accelerates away again. And there is **no arc-length parameterisation** anywhere — `Shot::pathLength`
and `peakSpeed` measure a path but nothing reparameterises by it — so speed also varies with the bow
within a shot.

That changes the problem from "build a continuous camera system" to **"the path is already C⁰; make
it C¹ and make its speed mean something."** That is a much smaller and much more likely-to-succeed
piece of work, and it is the single most useful thing found in this whole phase.

### 5.2 The architecture

**Keep the bake.** ADR-075's decision that directing is a bake to timeline keys is what makes a
directed camera scrubbable, offline-renderable and identical every time, and it is why the brief's
§9 offline-consistency requirement is *already satisfied* — `RenderJob` drives the identical code
path as the live loop (`src/app/render_job.cpp:228-250` → `Engine::update` → `timeline_.apply`).
A continuous shot that is baked to the same seven tracks inherits all of that for free. Any design
that evaluates a camera per frame would have to re-earn it.

**Change what is baked, in three steps.**

1. **One path, not N paths.** Collect each shot's *intent* — subject, stand-off, approach azimuth,
   elevation, dwell — into a list of waypoints, then fit **one C¹ Catmull-Rom through all of them**
   rather than solving each shot's geometry independently. The hero-orbit segments become arcs
   inserted into that spline (an orbit is three waypoints on a circle about the subject), so
   "approach, arc around, leave" is a property of the curve rather than a transition between two
   curves. This is what the brief's §9 means by not interpolating between independent shots'
   starting positions.
2. **Arc-length parameterisation.** The engine already has it — `spatial::spline::sampleByDistance`
   and `length()` at `src/spatial/spline.hpp:83,86`. Sample the fitted path by *distance* and drive
   distance with an eased speed profile, so the camera's speed is a designed quantity in m/s rather
   than an artifact of where the control points landed. Curvature-aware speed (slow into a tight
   arc) is then one multiplier on that profile.
3. **Damped aim.** The target track gets its own continuity treatment: instead of each shot's
   `targetAt`, run a critically-damped follow over the sequence of intended look points at bake time.
   There is no damping anywhere in the camera code today (searched; zero hits), and a hero handoff
   currently swings the aim inside a fixed window — good enough for a cut, visibly mechanical for a
   continuous take.

**Edited-sequence mode is then `continuous = false`**, which is the behaviour that exists today plus
an honest cut at each boundary. Both modes are the same bake with a different path fitter.

### 5.3 Clearance is the real risk, not the curve

`world::ClearanceField` lifts baked keys clear of terrain, statistical canopy and hero spheres —
**and the correction is purely vertical and only ever raises** (`src/world/camera_clearance.cpp`).
For a cut-based film that is fine: a shot that would have clipped gets flown over.

For a continuous take through a valley it is a design problem. A path that dollies to a mushroom at
ground level, arcs around it and leaves must be corrected *laterally*, not by being lifted over the
thing it is supposed to be circling. And a vertical-only correction applied to a smooth spline
reintroduces exactly the C¹ discontinuities step 1 removed — the existing smoothing pass exists for
that reason and it also only raises.

**This is where the Auto-director work will actually go**, and `04-plan.md` carries it as risk R1.

### 5.4 The rename

`docs/glowmere-valley-2/01-audit.md` §7 carries the full inventory. The two facts that shape it:
the user-facing name today is **"Direct to Music"**, not "Camera Director" — so the brief's rename
list is partly a *new* naming rather than a substitution — and there are three unrelated things in
this codebase called a director (`app::WorldDirector`, `seq::Director`, `entity::Authority::Director`,
plus an "AI Director" help topic) which must **not** be swept up. `tests/unit/test_help.cpp:461`
asserts the menu path literally and will fail on the rename, by design.

---

## 6. What this document does not answer

- **Whether the existing water body can follow a curve.** Not established. It is the gate on §1.5.
- **Whether a 3.6% triangle growth since ADR-151 came from content or from counting.** `03-baseline.md` §7.
- **What Glowmere Valley 2's frame budget is**, because no uncontended frame time for Glowmere
  Valley exists yet (`03-baseline.md` §5).
- **Whether radial asymmetry on the sweep is 40 lines or 400.** Estimated from reading proc-shrooms'
  `screw()`, not from reading our sweep code. If it is 400, §3.1's central claim weakens.
- **Whether Sobol sampling actually covers the behaviour grid** (§4.4). This is the one decision in
  the document with a named experiment attached rather than an argument, and it is deliberately
  falsifiable: if coverage is poor and the empty cells are reachable, MAP-Elites is correct and this
  document is wrong.
- **The scoring weights**, which are declared to be data and declared to be wrong (§4.7).
