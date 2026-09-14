# The shared candidate-search interface

**Read this if you are writing a procedural generator whose output is chosen by search** — the
Glowmere Valley 2 hero mushrooms and the procedural Tree of Life both are, and there is one
framework for both.

- **Header:** `src/search/candidate_search.hpp`
- **Implementation:** `src/search/candidate_search.cpp` (the generic half is written and tested)
- **Tests:** `tests/unit/test_candidate_search.cpp` — `[search]`, 9 cases
- **Decisions:** ADR-172 (score bands), ADR-173 (the sampler), `02-research.md` §4

The generic half — sampler, validity gate, scoring harness, diversity selection, serialisation —
**is implemented now.** Write your generator against it; you do not need to wait for anything.

---

## 1. What you supply: four members

```cpp
concept CandidateGenerator = requires(const G& g, const Parameters& p, const Subject& s) {
    { g.schema()            } -> std::convertible_to<const GeneratorSchema&>;
    { g.build(p)            } -> std::same_as<Result<Subject>>;
    { g.features(s, p)      } -> std::same_as<FeatureVector>;
    { g.domainScores(s, p)  } -> std::same_as<std::vector<ScoreComponent>>;
};
```

Everything else is supplied for you: `sampleAt`, `meshHygiene`, `ScoreBand`, `Score::overall`,
`selectDiverse`, `defaultViews`, `candidateToJson`, `GeneratedSource`.

**No domain vocabulary belongs in this layer.** Nothing in the header names a cap, a stem, a trunk
or a leaf, and nothing should. If you need something from the shared layer that can only be
described in your domain's words, that is a signal the split is in the wrong place — say so rather
than widening the interface quietly.

---

## 2. The five things worth knowing before you write a generator

### 2.1 Your schema's order is the sample (ADR-173)

`ParameterSchema` is ordered, and the order is **not** alphabetical or arbitrary: a low-discrepancy
sequence's low-dimensional projections are much better than its high-dimensional ones, so **put your
most visually influential parameter at index 0** and the least at the end. Reordering a schema
silently degrades the sample without changing a single range, which is why `GeneratorSchema::hash()`
covers the order and why a reorder invalidates every stored candidate.

The sampler carries direction numbers for **21 axes**. A wider schema is refused at validation, with
a message saying so — a schema that wide has almost certainly not been pruned. The mushroom schema
is 18.

### 2.2 A score component is a band, never a maximum (ADR-172)

`ScoreBand` is four numbers — `lowEdge, lowPlateau, highPlateau, highEdge` — and falls off on **both**
sides. There is deliberately no way to express a monotone component: an infinite edge is refused by
`validate()`.

This is not stylistic. A selection stage that ranks by score *is* an optimiser over what the score
measures, so a monotone component is an instruction to maximise it — and maximised edge density,
asymmetry and tessellation are three of the failure modes this pipeline exists to avoid.

Penalties are a separate field and are correctly monotone: there is no such thing as too little of an
artifact.

**A selector that scores candidates independently cannot enforce a constraint on the selection.**
This is the one that will bite you, because it looks like a scoring problem and is not. Glowmere's
art direction reserves one warm colour for the hero — *"the hero is the only warm light in the world
... that is why the eye goes to it from anywhere in the frame"* — and the search happily returned six
excellent mushrooms of which four were warm. Nothing was wrong with any of them. "Is this the second
warm thing in the frame" is a property of the **set**, and a scorer that sees one candidate at a time
structurally cannot answer it.

So: a constraint on the chosen collection belongs in the **diversity stage**, where the set exists,
not in the scoring stage, where it does not. `selectDiverse` is where a rule like "at most one of
these may be warm" can be expressed. Anything you find yourself wanting to score *relative to the
other winners* is this mistake.

**A component with no variance across the population is not a criterion.** This is the sharpest
practical form of the rule and it was learned the expensive way: two of Glowmere Valley 2's nine
components scored *exactly* 0.00 for five of six winners, because their bands were guessed and the
real distribution sat entirely outside them. A component that returns the same value for every
candidate contributes a constant to every total — it changes no ranking, selects nothing, and looks
exactly like a working criterion in the code. **Before trusting a band, plot its raw values over the
population and check they span it.** If the scores cluster at 0 or at 1, the band is wrong, not the
candidates.

**Store components, never just a total.** When the pipeline proposes something ugly, the breakdown is
what says which band lied. That is the only mechanism by which the bands ever improve.

### 2.3 Prefer constraining the generator to penalising the scorer

Anything your generator can be stopped from producing is one fewer thing a score has to defend
against. Glowmere's mushrooms draw colour from the scene's five authored palette roles rather than
from a free parameter, so "random rainbow coloration" is prevented by construction and needs no
saturation band at all.

### 2.4 The diversity trade is one explicit number

`selectDiverse(candidates, count, alpha)`. `alpha = 1` is top-N-by-score; `alpha = 0` is diversity
with no regard for quality. It is a parameter and not a constant because hiding it inside an
algorithm is how a pipeline ends up with N excellent near-identical individuals, or N diverse ugly
ones, with no knob to say which way it went wrong.

Selection is farthest-point over feature space, each axis normalised by its observed range, seeded
with the highest scorer. Not k-means: cluster *sizes* are unequal, so a dense region of near-identical
good candidates can own two clusters while a genuinely distinct sparse region owns none.

### 2.5 Six preview views, and the low one is not optional

`defaultViews()` returns front, three-quarter, side, rear three-quarter, **low**, above. The low angle
is part of the contract because a structure that the geometry above it encloses is invisible from
every camera at subject height — `docs/visual-cookbook/bioluminescence.md` records exactly that
happening here: gills were added once and could not be seen, because the glow dome covered them.

Render previews through the **existing** offline path — `app::Engine(EngineMode::Offline)` +
`SceneRenderer::renderToImage` + `assets::writePng`, which is what
`tests/rendering/test_representation_ceiling_perf.cpp` already does. Do not write a second capture
path.

---

## 3. The world-editor contract

A hero an artist cannot click, inspect, nudge, key, undo and save is a mesh blob wearing a procedural
label. Four requirements, each verified against the code rather than assumed.

### 3.1 Emit named nodes, not opaque geometry

`Subject::parts` is a list, and **each part becomes one named `scene::CompositionNode`**. That is what
makes it selectable: `ui::WorldSelection` (`src/ui/world_panel.hpp:38`) selects by kind — `Node`,
`Procedural`, `Field`, `Spline`, `Sdf`, `Particles`, `Material`, `Environment`, `Camera` — and derives
the inspector's parameter prefix from the name via `parameterPrefix()`. A generator that merges
everything into one mesh has made its own output uninspectable.

`SubjectPart::role` is the name suffix, so the editor shows `elder-2.cap`, not `part 3`.

### 3.2 The parameter is authoritative, never the mesh

This engine's governing rule is that `Scene` is a **per-frame derivation** rebuilt by
`Composition::applyParameters`; anything written straight into a `Scene` does not survive one update.
So a generated individual's morphology is a registered parameter vector, and
`search::parameterPaths(schema, "procedural/<name>/")` gives the paths — one per axis, under the
node's own prefix, keyable and modulatable like anything else.

**`GeneratedSource` is the unit.** `values` is authoritative — the generator builds from it, the editor
writes it — and `index` is **provenance**: which candidate these numbers started life as. For an
untouched hero `values == sampleAt(schema, index)` exactly, which is an assertion a test makes. The
moment an artist nudges a slider the two diverge, and that is correct.

### 3.3 Live regeneration is affordable, and the throttle already exists

Hash `values` into the source's `structuralHash` and an edited parameter rebuilds the mesh, which is
what makes a nudge visible. The cost is real — `src/scene/composition.hpp:503` records dragging
`procedural/elder-stem/hierarchy/depth` putting every following frame at **145–216 ms** — and the
mitigation is already built and already measured: `Composition::setInteractiveRebuildBudget` defers an
object whose last rebuild exceeded the budget until its inputs settle, and it is **deliberately zero
offline**, because a wall clock has no business deciding what a deterministic render contains.

**So the answer to "is full live regeneration too expensive" is no, and the line is already drawn by
existing code.** A generator should not invent a second throttle.

### 3.4 Round-trip and undo

`generatedSourceToJson` / `generatedSourceFromJson` are one pair, so a scene's stored block and a
candidate record cannot drift — `candidateToJson` emits the same block. A moved schema hash is
**refused**, not silently reinterpreted, because the values are positional.

For undo: `scene::cloneNodeSpec` is, in its own header's words, "the list a new authored field has to
be added to. If a duplicate ever comes back missing something, this is the function that forgot it."
Whoever lands the new source kind must add its fields there.

---

## 4. The one engine gap, named rather than worked around

**`scene::SourceSpec` cannot express a generated organism today.** `scene::PrimitiveKind` enumerates
`Box, Cylinder, Sphere, Torus, Point, Procedural, Tube, Mesh` and none of them is "built by a
registered generator from a parameter vector".

Making a searched individual a first-class, editable, round-tripping, undoable scene object needs
**one new `PrimitiveKind`** whose spec is `search::GeneratedSource`, plus:

- its arm in `SourceSpec::validate` and `SourceSpec::structuralHash` (the hash is what makes an edit
  rebuild the mesh);
- its JSON, both directions;
- its fields in `scene::cloneNodeSpec`;
- a small registry mapping `generatorName` to a builder, so the composition can resolve one.

**This is generic on purpose.** A tree and a mushroom differ in their generator's name and schema, not
in how a scene stores them. **Whichever project lands this first owns it; the other must not add a
second kind.** If that is Tree of Life, say so and I will build Glowmere Valley 2's mushrooms against
it rather than duplicating it.

Until it lands, a generator can still be written and searched — `build()` returns meshes and the
pipeline runs — it simply cannot yet be *edited in place*. That is a real limitation and it is the
reason this gap is written down here rather than discovered in Phase 4.

---

## 5. What is selectable, deliberately

| category | selectable in the viewport? | why |
|---|---|---|
| hero mushrooms | **yes**, each part individually | they are named nodes; they are what an artist stages and tunes |
| the Wanderer, the Visitor UFO | **yes** | already named nodes today |
| terrain, water | **yes**, as one node each | the existing behaviour |
| **scatter vegetation** | **no**, not per stem | 100k+ instances share one `ProceduralGeometry` and one draw; per-instance picking would need a per-instance id through the cull chain. The *layer* is selectable and its parameters are editable, which is the level an artist actually works at |
| candidate previews | **not scene objects at all** | they live in the search tool's offline renders, not in the scene |

The vegetation row is a deliberate exception, not an oversight. Picking resolves through the
identifier render target, whose index field is 14 bits — `kPickIndexBits` in `src/scene/scene_types.hpp`,
a ceiling of 16,383 nameable entities (`docs/renderer-limits.md:228`, where `packPickId` saturates
rather than wrapping). Glowmere alone places over 114,000 instances, so per-stem picking is not a
thing that was skipped; it is a thing the identifier encoding cannot express.

---

## 5a. Measurement conventions

These are not about candidate search. They are here because this is where this project's measurement
conventions ended up being written down, and both were bought with a wrong conclusion.

**Every frame time reads with a ±3 ms band unless it says it was interleaved.** Measured on this
machine: three invocations of one perf binary over one byte-identical scene gave 10.945 / 11.272 /
13.697 ms with the triangle count constant at 252,996. Every run held the GPU lock and passed
`pgrep avgen` on both sides. A cross-invocation comparison below about 3 ms is not evidence, and
quoting an absolute frame time to three significant figures implies a precision that does not exist.
The protocol that does work is ADR-150's and it is not optional: **arms interleaved inside one
process**, medians over a steady window, three runs.

**A causally impossible result is a free diagnostic.** A hero-cost A/B run as two invocations reported
that *hiding six mushrooms made the frame slower*. Hiding geometry cannot do that, so the arm indicted
the method instantly and at no cost — where a merely surprising result would have been argued with.

The transferable half is the converse: **an arm whose sign is known in advance is worth running
deliberately, as a test of the method rather than of the scene.** Delete something and the frame must
not get slower; add something and it must not get faster. If it does, stop measuring and fix the
harness.

And the corollary, which is what actually saves time: **when an arm's sign is known, the delta is
often not the question.** Adding vegetation can only cost, so the useful measurement is not the A/B
but whether the absolute still fits the budget — one interleaved run instead of two scenes and a
harness to alternate them.

---

## 6. Status

| part | state |
|---|---|
| `ParameterSchema`, `GeneratorSchema`, hashing | implemented, tested |
| Sobol sampler (`sampleAt`) | implemented, tested — including the stratification claim |
| `ScoreBand`, `ScoreComponent`, `Score::overall` | implemented, tested — including the "more is not better" negative control |
| `meshHygiene` | implemented, tested |
| `selectDiverse` | implemented, tested |
| `GeneratedSource` + JSON + `parameterPaths` | implemented, tested |
| `defaultViews` | implemented, tested |
| contact-sheet rendering | **not written** — needs the GPU layer; specified as data (`PreviewView`, `PreviewLighting`) |
| the new `PrimitiveKind` | **not written** — §4; first project to need it owns it |
| any generator | **not written** — mushroom generator is Glowmere Valley 2 Phase 4 |
