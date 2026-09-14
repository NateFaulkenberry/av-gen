# Phase 4 — the hero mushrooms

The generator, the search, the scoring, the diversity selection, and six winners serialised as
reproducible parameter sets. The user's addendum governs it; the `search::` framework published in
Phase 2 is the machinery.

**Six distinct hero designs, chosen from 200 candidates, at `examples/organisms/glowmere2-heroes.json`.**
The contact sheet is `build/mushroom-winners.png`.

---

## 1. What was built

| | |
|---|---|
| `scene::PrimitiveKind::Generated` + registry (ADR-175) | the gap the design note named, landed generically |
| `src/organism/mushroom.{hpp,cpp}` | the generator: 18-axis schema, four parts, banded scorer, feature vector |
| `tests/rendering/test_mushroom_contact_sheet.cpp` | raw candidates, looked at before any evaluator existed |
| `tests/rendering/test_mushroom_search.cpp` | the pipeline end to end, the winners' sheet, the record |
| `examples/organisms/glowmere2-heroes.json` | the canonical record: six parameter sets, no meshes |

## 2. The geometry

Four swept surfaces from one parameter vector: an upper cap, an underside, a stem, a ring of gill
blades. Each is a separate part, so each becomes its own named node with its own material — which is
what makes a cap that glows differently from its gills expressible, and both selectable in the editor.

Three ideas taken from outside:

- **The cap is a radius profile, not a primitive** (`docs/visual-cookbook/fungi.md`).
- **The identity lives in the rim tangent** (proc-shrooms, source read): the cap is two cubic Hermite
  profiles sharing a rim, and the free parameters are the *tangent angles* at centre and rim. That is
  why four numbers cover a parasol, a bell, a cone and a recurved chanterelle — control points can
  express those shapes but cannot be *asked* for twenty degrees more rim droop.
- **The profile is swept about a curved axis** (Desbenoit et al., EG 2004): a bent stem for free.

And one law, from `docs/visual-cookbook/bioluminescence.md`, governing the emissive design rather
than the geometry: **give the light a structure to come out of.** That is why the gills are geometry
and not a texture, why *which* structure emits is a searched parameter rather than a fixed choice,
and why the winners' sheet is shot from below.

**What the engine still cannot do, and no longer needs to.** `scene::makeTube` sweeps a circular
cross-section only, so no engine primitive can break a cap's radial symmetry — the defect visible in
the original elder, whose cap is a perfect ellipse. Rather than widen the shared sweep, which the
original Glowmere's own hero goes through, the modulation lives in the generator. The engine gap is
unchanged and is now nobody's blocker.

## 3. The five geometry defects the contact sheet found

Every one was found by *looking*, and the generator was fixed before any evaluator existed — the
discipline the coordinator asked for after Phase 3.

| defect | cause | fix |
|---|---|---|
| **24 of 24 rejected, "degenerate"** | gill blades tapered to zero height at both ends, making their end quads degenerate | a blade keeps an 18% lip at stem and rim, which is also what a real gill does |
| a second degeneracy | the lathe's first ring sat at r = 0, one degenerate triangle per spoke | the first ring starts just off the axis; the apex is closed by a fan |
| **half the gills rendered black** | blades were zero-thickness sheets, so their normal was a coin toss and double-siding did not save them | a blade is a thin wedge with two faces that each know which way they point |
| **ragged, sparkling cap rims** | upper and lower cap profiles ended at the same radius *and* the same height, so the two surfaces were coincident along the entire rim — z-fighting | the underside tucks inside: 96.5% of the radius, lifted by 14% of the thickness. A real cap has a lip |
| **the cap floating off its stem** | the tilt rotated about the world origin, swinging the whole cap off the stem top | tilt about the attachment point |

The validity gate was right every time and the mesh was wrong every time, which is the gate doing
exactly its job. **Two of those five were caught by a gate rejecting 100% of the population** — a
rejection histogram is the pipeline's most useful diagnostic, and this is why.

One hypothesis was wrong and is worth recording: the ragged rims were first attributed to angular
aliasing, since the waviness harmonic runs at twice the lobe count and 44 spokes gave 2.4 samples per
cycle at nine lobes. The sampling *was* undersampled — it is now `max(48, lobes × 10)`, which is
ADR-159's rule in another domain — and fixing it changed nothing, because the cause was the
coincident surfaces. The fix was kept anyway: it was a real defect, just not that one.

## 4. The search

200 candidates from a scrambled Sobol sequence over 18 axes ordered by visual influence (ADR-173).

```
valid 165 of 200
  floating-cap    33  (16.5%)
  no-overhang      2  (1.0%)
```

Sixteen per cent rejected on one rule is at the edge of what a healthy schema should produce — it
says `capTiltDeg`'s upper range and `capThickness`'s lower range interact badly, and it is the sort
of thing the histogram exists to surface. It is recorded rather than tuned away, because the
alternative — widening the plausibility rule until the number looks better — is how a gate stops
meaning anything.

### The scoring, and the calibration it needed

Nine banded components (ADR-172): proportion, cap solidity, silhouette fill, multi-view consistency,
outline coherence, organicity, gill pitch, stem curvature, pixels-per-triangle. **No component is
monotone**; each scores the distance from a band and falls off on both sides.

Silhouette is measured on the mesh rather than on a render — vertices projected onto a 64×64 raster
at four azimuths, giving a fill factor and a boundary ratio. It is an approximation and is named as
one: a concavity hidden behind the shape is invisible to it, which for a mushroom seen from the side
is the underside, and the underside is scored separately by `gillPitch`.

**Two of nine bands were wrong on the first run and the component breakdown is what said so.**
`outline` scored exactly 0.00 for five of six winners and `pixelsPerTriangle` scored 0.01–0.19 — both
were guessed ranges that the real distribution sat entirely outside, so both components were
contributing a constant and neither was selecting anything. Re-banded from the measured population
(outline 0.44–0.65, pixels-per-triangle 8–32), the winners' scores moved from 0.73–0.79 to 0.88–0.99
with real variation between components.

That is exactly the failure ADR-172 predicted, and the reason it requires components to be stored
separately rather than folded into a total. The lower edge of `pixelsPerTriangle` is the only number
in the set that is not a preference: it is the 2×2-quad knee from `scene/mesh_metrics.hpp`.

### Diversity

Farthest-point selection over a 13-axis normalised feature vector, with the quality/diversity trade
explicit at `alpha = 0.45`. The six winners are recognisably six *designs*: a broad folded parasol
with warm amber gills, a sleek cyan-rimmed flat cap, a domed cap on a thick leaning stem, a bell with
deep gills, a wide flat cap on a heavy stem, and a low funnel with a bulbous stem base.

## 5. The record

`examples/organisms/glowmere2-heroes.json` carries per hero: generator, version, **schema hash**,
candidate index, all 18 parameters, the 13-axis feature vector, the overall score and all nine
components, and the mesh metrics. **No mesh is stored** — `(generator, version, schemaHash, index)`
regenerates it exactly, and the hash makes a stale record announce itself rather than silently
regenerating a different mushroom under the same name.

## 6. What Phase 4 did not do

- **The six are not yet placed in Glowmere Valley 2.** The generator is registered and the scene can
  name it (`PrimitiveKind::Generated`), but the elder is still the original squashed sphere and the
  habitat pockets of the brief's §7 are not staged. That is the first task of the next phase and it
  is a scene edit rather than new machinery.
- **No human override was exercised.** The six the pipeline proposed were the six accepted, after
  looking at the sheet. The mechanism for recording an override exists (`selectionNote`) and has not
  been needed yet — which is worth saying plainly, because a pipeline whose proposals are never
  overridden has either very good weights or a reviewer who is not looking.
- **The MAP-Elites coverage experiment (ADR-173) has not been run.** It needs the behaviour grid
  binned over the population, which is an hour of work against the data this phase already produces.
- **Materials are flat colours from the palette.** The brief's §7 asks for cap colour variation,
  emissive veins, subsurface approximation and wetness. What is there is the emission *structure*
  choice, which is the part that decides whether a mushroom reads as an organism.
