# The LOD / Geometry Lab

Lab #4 of the Engineering Lab Suite (ADR-261, `docs/engineering-labs.md`). Registered in
`src/labs/lab.cpp`; fixture `examples/labs/lod-geometry-lab.scene.json`; cases
`examples/labs/lod/cases.json`, reachable as `avgen --lab-case lod:<n>`.

**The question**: *which representation was chosen, does it look like the object, and does it reach
the frame?*

**What it owns**: the scatter ladder's level selection, the size measure it selects on, which rungs
are recorded as draws at all, and the vertex path each rung's mesh takes.

**What it does not own**: whether the object was culled at all. That is the Visibility Lab. The two
meet inside `shaders/cull.wgsl`, which makes both decisions in one function, and the boundary
between them is now a line of code rather than a convention — `radius` is the Visibility Lab's and
`lodRadius` is this one's.

---

## 1. The ladders that are actually live

Three things in this engine are called LOD. Only two of them run.

| Ladder | Where it decides | What it selects on |
|---|---|---|
| **Scatter instances** | `shaders/cull.wgsl` `cs_cull_classify`, CPU reference `rendering::cullLodLevel` | projected radius in pixels (or distance, if `lodByScreenSize` is off) |
| **Terrain chunks** | `world::chunkLod` (`src/world/terrain.cpp`) | distance, rescaled to a reference lens so a threshold tuned at 50 mm holds at 24 mm |
| **Entities** | `rendering::RepresentationSelector` / `ImportanceEvaluator` | — **not live**: `scene_renderer.cpp` calls neither |

The thresholds every scatter layer in Glowmere uses are set in one place,
`src/scene/composition.cpp` (the `Terrain` case): **28 / 11 / 4 px of projected radius**, four
levels, `lodByScreenSize` on. They are the same three numbers for all thirteen layers — a tree, a
fern, a pebble.

Around the ladder, in the same shader and in the order the shader applies them: the frustum test,
`maxDistance`, `minScreenRadius` with ADR-082's asymmetric hysteresis, ADR-038's depth-band thinning
(which fires even with culling off), and only then the rung. The first three are rejections and
belong to the Visibility Lab; the rung is this lab's.

Two stability terms sit on top of the rung, both ADR-082 and both legitimate: `lodSpread` (default
0.12) gives every instance its own slightly offset threshold so a band of the world does not change
mesh on one frame, and `lodHysteresis` (default 0, opt-in) is a dead zone that reads the previous
frame. Neither is a margin bolted on to make something pass; read ADR-082 before touching either.

---

## 2. What the thresholds mean, and what happened to them

`28` is not a dimensionless number. It is compared against `radius / distance × projScale`, so it
only means a size once you say **which radius** — and the cull pass forms its sphere about the
*instance record position*, which for a scatter is the point on the ground the thing was planted at.

The Visibility Lab corrected that radius: a sphere centred on the record has to reach the furthest
corner of the source **from the source's own origin** (`rendering::sourceCullRadius`), or a tree's
canopy sits outside the sphere used to decide whether the tree is on screen. That fix is right and
this lab kept it.

It also changed the ladder's input, unevenly. Measured over Glowmere's thirteen scatter layers
(`avgen_tests "[.analysis][lod]"`), the ratio between the origin-centred radius and the tight one is

| least | most |
|---|---|
| `ferns` 1.08× | `grass` 1.74×, `pines` 1.75× |

— a factor that is a fact about where the artist put the origin and says nothing about how big the
thing looks. The consequence, in the only terms that answer the question: **the same authored
threshold of 28 px fired when an object's drawn radius was 11.1–18.7 px under the corrected radius
and 14.8–22.6 px under the one the thresholds were authored against.** Objects were holding their
full mesh roughly a quarter longer, per layer, for reasons unrelated to their size.

So the thresholds were not re-derived. **The ladder was given back the quantity it was authored
against**, as a separate uniform (`CullParams::thresholds.w`, from `CachedMesh::radius`), while the
rejection tests keep the conservative one. Two numbers, two jobs — the same shape of fix as
`sourceCullRadius` itself, one step further down.

What that costs and buys is measured in `tests/rendering/test_lod_gpu.cpp`, "What the first
threshold costs and what it buys": on CommonTree_1, rung 1 keeps 93–96% of the object's lit pixels
and its outline is within 1–3 px of rung 0's at every drawn radius from 8 px to 40 px. The ladder
has headroom above where it fires under either convention. **28 / 11 / 4 were left alone.**

A caution that cost a wrong conclusion on the way. ADR-085's quality statement — "8.0% of pixels
differ by more than 2/255" — is a *frame* measure dominated by background. At the object level the
same measure reads 92–98% at every size, because a third of the triangles is a different arrangement
of leaves and nearly every pixel of the object moves. It cannot tell a visible swap from an
invisible one on foliage. What a viewer sees change is the object's **mass and outline**, and those
are what the instrument reports.

---

## 3. Defects this lab found

Each was reproduced in production, reproduced in the lab, fixed at the decision responsible, and has
a case and a regression test.

### 3.1 Every rung change drew nothing for one to three frames

`ProceduralRenderer` decided which LOD levels to record an indirect draw for from **how many
consecutive frames the level had been empty in the last completed cull readback** — and that
readback lags the drawn frame by one to three. The rule was a prediction of the present from the
past, and it was wrong at exactly one moment: the frame an instance arrives on a level nothing has
been on recently. The object was then drawn by nothing at all until the readback caught up.

Measured on CommonTree_1 at 460 m: the arrival frame drew **0 lit pixels**; the identical frame,
same camera, once the renderer had seen the level occupied, drew 89. No counter in the frame
reported it — the cull pass ran, and its numbers were right.

Fixed by `rendering::objectLevelRange`: the inclusive range of rungs any record of the object could
be on, from the same bounds `objectFullyCulled` already uses, widened by everything that can move
one instance's threshold (ADR-082's spread and dead zone, ADR-038's `detail`). A level outside the
range is **provably** empty. It is a proof, not a margin. Regression:
`tests/rendering/test_lod_gpu.cpp`, "A rung change draws the object on the frame it happens", with
the proof itself pinned in `tests/unit/test_lod_ladder.cpp` against `cullLodLevel`'s answer for
every record over ten cameras.

Cost, on Glowmere: 130 skipped indirect draws per frame became 60, so 70 more draws are recorded.
That is the price of drawing the transition.

### 3.2 Rungs 2 and 3 of every imported asset were flattened into the camera plane

ADR-029 made levels 2 and 3 camera-facing impostor quads, and the renderer set the billboard vertex
path for `level >= 2`. ADR-085 then gave a **Mesh** source a simplified mesh at every level — and
nothing told the renderer. From that point, rungs 2 and 3 of every scatter layer, every city piece
and every imported asset were drawn through the impostor path: their vertices taken as offsets in
the camera's basis, which skips the source transform and the deformer stack.

So a 14 m production tree drew at rung 2 as a **7.3 m flat shape centred on the foot of its trunk**.
Measured at 90 m: rung 2 occupied screen rows 362–419 where rung 0 occupies 304–424 — half the
height, sunk to the ground, at every distance past the second threshold. On Glowmere 127 instances
were on that rung.

Fixed by `scene::lodLevelIsImpostor`, which is the one place the distinction is written down;
`makeLodMesh` branches on it and the renderer asks it. Rung 2 now draws rows 309–418. Regression:
`tests/rendering/test_lod_gpu.cpp`, "The representation a rung draws occupies the object's own
screen region", plus both arms of the predicate in `tests/unit/test_lod_ladder.cpp`.

### 3.3 The ladder's size measure (§2 above)

Regression: `tests/rendering/test_lod_gpu.cpp`, "A pixel threshold means the same size for every
asset", which finds by bisection *on the rendered image* the distance at which the GPU stops
choosing rung 0 for each of two assets, and compares how big each was on screen when it happened.

---

## 4. Not fixed, and why

**The bottom rung loses the trunk of a tree, and under-reports it.** At 2.9% of its triangles
CommonTree_1 comes back from `assets::buildLodChain` with the bottom 31% of its bounding box gone —
the trunk — while the level reports a relative error of 0.065 and an absolute error of 0.469 source
units against a bounding-box move of 2.47. That is 5.3× smaller than the deviation it has, and it
contradicts `src/assets/mesh_lod.hpp`'s own claim that the error "never understates". A selector
choosing a level by projected error, which is what that field exists for, would choose this one far
too early.

Measured in `tests/unit/test_lod_ladder.cpp`, "What each rung of the chain draws, against the
source" (`[.analysis][lod]`). Not fixed here because the fix belongs in `buildLodChain` — the same
shape of guard ADR-085 already added for a level larger than its predecessor, applied to a level
whose bounding box moved further than its reported error — and changing the simplifier's calibration
changes every asset in the repository. Rung 3 carries 7 instances on the Glowmere baseline, so it is
recorded rather than rushed.

**Entity LOD is not live and was not wired up.** `RepresentationSelector` and `ImportanceEvaluator`
exist, are tested, and are called by nothing in `scene_renderer.cpp`. Switching them on is a change
to every frame, not a gap to fill.

**Animated LOD does not exist.** Nothing in the engine varies rig evaluation or skinning by a LOD
rung. `entity.cpp` has a rig ladder of its own — posed / rate-limited / culled, reported per render —
but it keys on the entity cull and on distance, not on any representation this lab selects. Joining
the two would mean giving entities a representation first, which is the item above.

---

## 5. The fixture

`examples/labs/lod-geometry-lab.scene.json`. Flat, unlit by atmosphere, fixed seed, camera 6 m up
and level looking down 640 m of flat ground — for the reason the Visibility Lab's fixture is flat:
anything that changes an object's appearance with distance makes "it changed" ambiguous, and a rung
change is exactly a change with distance.

**Its light changed on 2026-09-18, and this is what that moved.** The fixture always carried a
top-level `"lights"` array and nothing read it (ADR-278); the scene was lit by `defaultKeyLight()`
instead. It now obeys its own file. Measured, one frame at 1920x1080, t = 1/60 s, arm and control
rendered by the same binary with `--disable post` and the control being this same file with the
`"lights"` key removed:

| | default key (what it got) | the file's key (what it gets) |
|---|---|---|
| direction | (-0.353209, -0.883022, -0.309058) | (-0.349843, -0.719676, -0.599730) — **19.2 degrees** apart |
| intensity | 3.0 | 4.0 |
| temperature | 5600 K | 6500 K |
| frame mean | 0.0629 | 0.0633 |
| rms contrast | 0.0343 | 0.0340 |
| p99 | 0.1073 | 0.1076 |
| bright centroid y | 0.4938 | 0.4941 |

**1,000,000 of 2,073,600 pixels differ (48.2%), worst channel 158 of 255.** The frame means barely
move because this fixture is mostly unlit sky and ground; what moved is where the shadows fall.

**No LOD measurement in this document moved, and that is measured rather than assumed**: the
identifier AOV (`--aov id`, which instance drew in which pixel, and therefore which rung) is
**byte-identical** before and after, on both frames, while the colour frames differ. The lit-pixel
counts and threshold measurements in §2 and §3 come from `tests/rendering/test_lod_gpu.cpp`, which
builds its scene in code and never loads this fixture.


Six layers of production assets, chosen to separate hypotheses rather than to look like a world:

| layer | asset | why |
|---|---|---|
| `tree-on-its-foot` | CommonTree_1 @ 14 m | geometry standing on its origin; the two radius rules differ by 1.61× |
| `rock-on-its-centre` | Rock_Medium_1 @ 14 m | **the control**: the same height, near enough centred (1.30×) |
| `worst-offset` | TwistedTree_2 @ 15 m | the extreme of that ratio over the production set (1.75×) |
| `least-offset` | Fern_1 @ 1.4 m | the other extreme (1.08×), and the shape a sphere describes worst |
| `below-the-bar` | Grass_Common_Short @ 0.7 m | small enough that `minScreenRadius`, not the ladder, removes it — so the two decisions are distinguishable in one frame |
| `graded` | CommonTree_1, scale 0.35×–2.4× | one population across every rung at the same instant, which a dolly cannot produce |

10,610 instances; `573/510/53/0` across the rungs, identical on every frame.

**The lesson the fixture is built around**: the last bug in this area survived every existing test
because every fixture was a *centred box*, for which the right and wrong radius rules are
bit-identical. The bug was in the shape of the fixtures. Every pair above differs in the thing being
measured and agrees in everything else.

---

## 6. Making LOD state visible

`DebugViewOptions::lod` had a checkbox in the World panel since the option struct was written and
`debug_visualizer.cpp` read the field nowhere — ADR-225's defect exactly. It is wired now, to
`ProceduralRenderer::readLodLevels`, which is a blocking readback of `lodIndex`: the buffer
`cs_cull_classify` writes and the compaction reads, so it is the decision itself and not a second
account of it.

Green 0, yellow 1, orange 2, red 3, purple culled — four named colours rather than a ramp, because a
rung is one of four things and not a position on a scale. **Grey is "no decision available"**, which
is a different statement from rung 0 and is drawn as one: when the object's cull dispatches were not
encoded that frame (the whole object was provably rejected), the buffer still holds the last frame
that ran them, and an overlay that draws it is drawing history. `readLodLevels` returns `fresh` for
exactly that reason.

It is on by default in `labs::overlaysFor(LabId::Lod)`, with `points` and `bounds`.

---

## 7. Hazards, recorded

* `readVisibleIndices` and `readCullCounts` are **stale for a frame in which `objectFullyCulled`
  fired**, because that skips the cull dispatches as well as the draws. A tool that reads them then
  reports history. (Found by the Visibility Lab; `readLodLevels` reports `fresh` so this lab's
  overlay cannot repeat it.)
* The asynchronous stats readback lags the drawn frame by **one to three frames**. It is a readout,
  never an input — and §3.1 above is what it cost to use it as one.
* Verifying a ladder by re-deriving the expected rung from the inputs the shader uses agrees by
  construction and proves nothing. `tests/unit/test_triangle_size_analysis.cpp` and
  `test_representation_band_analysis.cpp` both transcribe `cullLodLevel` deliberately and say so;
  this lab's rung assertions are against `readCullCounts` and drawn pixels instead.
