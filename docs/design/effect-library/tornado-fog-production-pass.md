# Tornado and Fog: a production-readiness art pass

Planned from the existing record. Nothing here re-measures what ADR-580/581 or ADR-560–579 already
measured; where a render or a probe is needed, the acceptance section says so instead of guessing
at a number. Primary sources: `docs/tornado-handoff.md` (a reconstruction, evidenced/inferred
throughout), ADR-580, ADR-581, ADR-560–579, ADR-702 (current effect architecture), ADR-441 (no
shims), ADR-421/574/576 (a control or capability with no reader/writer is a defect family).

New ADRs from this plan take numbers **704+** (703 is the Effect Library's Wave 1).

---

## 1. What's done

**Tornado** (`docs/tornado-handoff.md` §2; ADR-580 §5–§10, §11): a new analytic `Tornado` kind,
Burgers-Rott velocity + a curved axis + a four-part density field, no simulation state.
`src/core/tornado.{hpp,cpp}`, `shaders/tornado.wgsl`, `src/world/effects/kinds/tornado_effect.cpp`.
Reachable from the effects UI (ADR-702's `EffectsSection`, one card per owner's stack). Ships in the
Tree of Life day and night deliverables. CPU/GPU parity is a test
(`tests/rendering/test_tornado_parity_gpu.cpp`). Structure-before-noise is proven, not asserted:
`cloudAmount` (Detail) at 0 returns exactly 1.0 from an early-out, so the frame is byte-identical to
the analytic field (ADR-580 §8.7) — **this is the gate every field change has to re-pass.** Cost:
4.7–5.2x cheaper than the vortex it replaced, like-for-like, on one tree (ADR-580 §10.5, handoff
§2). Seven storms from one field (`examples/labs/tornado-showcase.scene.json` + `_tc-1..7`), six of
seven read well.

**Fog** (ADR-560–579): rebuilt from "a circular grey disc with an fBM stack on top and a hole in the
middle" (ADR-560, ADR-561) into an analytic elliptical/six-primitive field
(`shaders/fog.wgsl`, `src/world/fog_field.cpp`, ADR-563, ADR-566) with: an optical-depth density
unit that is correct at any bank size (ADR-564); a mean-preserving macro-detail multiply (ADR-565);
a height layer with a floor, a ceiling and a curve, shared by three readers and proven to be a
function and its own integral (ADR-567, ADR-568); drift + wind-direction coupling (ADR-571,
ADR-572); an off-by-default shared self-shadow march usable by any medium kind (ADR-570); ten
registered presets that reset every row so they are starting points, not continuations (ADR-579);
and `media`/`mediaDropped`/`shadowSteps` in the profiling line (ADR-578). The single-medium-slot bug
("if I turn on fog I can't see the vortex") is fixed structurally: `kMaxMedia` is 4, dispatched by
kind, with a per-slot ray interval that is a proven bound, not an estimate (ADR-562, ADR-566,
ADR-702 §"Resource isolation"). §46 C's local primitives (sphere, ellipsoid, box, capsule, cylinder,
bank) are **built and distinguishable at a glance** (ADR-566) — despite being listed as an "open
item" for this track's briefing, it is done; see §5.

**Shared foundation** (ADR-562, ADR-566, ADR-570, ADR-702): both effects march through one
`shaders/volume.wgsl`, dispatched on `MediumSlot::kind`, with a CPU-twinned bound
(`src/world/medium_bound.cpp`, `tests/unit/test_medium_bound.cpp`) and one self-shadow function. Four
media coexist and are tested coexisting, including Tornado + Aurora together
(`tests/rendering/test_effect_stack_gpu.cpp`, ADR-702's acceptance case).

---

## 2. What's left

### Tornado, ranked (`docs/tornado-handoff.md` §5, re-ranked per its own §10)

| # | item | files | acceptance |
|---|---|---|---|
| 1 | **Ground interaction / debris cloud.** The funnel's own base is a hard horizontal plane (`core/tornado.cpp::evaluate`, the `h > 1.08 \|\| h < -0.02` cut); dropping the skirt (handoff §6) left "a column that stops" instead of "a disc of light", but the cut is still there. Highest-value remaining work. | `src/core/tornado.{hpp,cpp}`, `shaders/tornado.wgsl`, `src/world/effects/kinds/tornado_effect.cpp`, `src/world/medium_bound.cpp` (bound grows with the new term) | Before/after review renders of the hero project at the foot, matched exposure; owner's eye judges whether the cut reads as touchdown rather than a stub (ADR-441 — no test substitutes for this). Guard: `tests/rendering/test_tornado_parity_gpu.cpp` extended to cover the new term (ADR-565 §4's rule: a probe built from one feature's extent is blind to a feature outside it), `test_medium_bound.cpp` re-passes with the widened bound. |
| 2 | **Tall Column `_tc-4` missing-middle defect.** Reproduced (ADR-580 §11.3): not screen-space, not noise, not under-sampling; localised to `lean`+`wobbleAmount`. Untested hypothesis: a contrast floor, not a hole. | `src/core/tornado.cpp` (`evaluate`), `tests/rendering/test_tornado_parity_gpu.cpp` (already has the CPU-probe harness) | A CPU probe of `tornado::evaluate` down the displaced axis at `h≈0.4` settles field-vs-march. If the field has a hole: fix under the §1 structure-before-noise gate, re-render `_tc-4` and diff against `eadf0dd2a8549880`. If it's a contrast floor: no code change, only a documented finding (the axis-displaced funnel is genuinely close to invisible at those values) plus, optionally, a floor/gain tweak reviewed by eye. |
| 3 | **Self-shadowing — unblocked.** ADR-570 built the shared light march; `volumeShadowSteps` defaults to 0 so nothing has moved yet. Ranked 3rd in the original record on the assumption it was blocked; it is not. | `shaders/volume.wgsl` (`mediumSelfShadow`, already dispatches on kind), scene/project `volumeShadowSteps` | Turn it on (2–4 steps) against the hero project, review render, re-rank against what the owner sees — the hero currently reads as "glowing gas" (§10.4) and this is the named cause. Guard: `tests/rendering/test_fog_self_shadow_gpu.cpp`'s left/right asymmetry case already covers the shared function; no new test needed unless the tornado's own lane read needs a tornado-specific case (ADR-570's kind-aware `mediumDensityCoeff` already exists for this — handoff §10). Cost is linear in volumetric-lit lights (ADR-570): budget accordingly, and note `volumetricStrength` defaults to 1.0 for every light. |
| 4 | **Hero's remaining art direction.** Too bright, wall cloud cut off by top of frame. Pure tuning, blocked on nothing. | project/scene tornado parameters only | Before/after hero renders at matched camera; owner's eye. Do this *after* items 1 and 3 land, so it's tuned against the final structure and lighting once, not twice. |
| 5 | **Step redistribution.** Per-slot interval skips samples outside a medium but doesn't place them inside it; a thin column is still capped by the global step count (ADR-580 §8.3's rope table: 3 disconnected ellipsoids at 64 steps, continuous at 256). **This is the same work as fog §31 — do it once, in the shared march.** | `shaders/volume.wgsl`, `src/world/medium_bound.cpp` | See §4 (Ordering) — shared item. Acceptance: rope-preset tornado renders continuous at the *shipped* step count; fog primitive arms lose their 32-step speckle (ADR-566 §Consequences names this exact artefact). Guard: the Detail=0 byte-identity gate (tornado) and the fog "uniform-in-angle at zero detail, but built structure varies" case (`test_fog_bank.cpp`) must both still pass — redistribution must not become a second density change. |
| 6 | **Particles** (dust, debris, embers). Not implemented, no controls, deliberately (ADR-421: a control over nothing is worse than no control). Depends on item 1 existing as a term worth feeding particles from. | new — no file yet | Backlog. No acceptance criteria until scoped. |
| 7 | **Grid tier.** Blocked on an owner decision (ADR-581 §4: raise `kCatchUpSteps`, add `--grid-warmup`, refuse `--range t:t` on a grid scene, or make grid state a pure function of time). Do not start without that choice. | n/a until decided | n/a |

### Fog, open items

| item | state | files | what's left |
|---|---|---|---|
| **§31 adaptive sampling / step redistribution** | not built (ADR-562 §4's own comment, ADR-577) | `shaders/volume.wgsl` | **Identical to tornado item 5. One piece of work, done once, in the shared march.** See §4. |
| **§25 colour** (scattering/absorption/height/distance colour) | assessed, not built (ADR-575). Three colours through depth exist (`colorDeep/Mid/Accent`); height and distance colour do not. | `src/world/fog_field.cpp`, `shaders/fog.wgsl`, `src/world/effects/kinds/volumetric_fog_effect.cpp` | Additive, two more terms, lane budget needs checking (`grep -o 'mediaLane(s, [0-9]*u)' shaders/volume.wgsl` per ADR-571/575's audit method — do this before writing rows, not after). Not blocking; §25's own brief text warns against colour doing structure's job, so this is backlog, not urgent. |
| **§16 flow controls** | **verified today**: `src/world/effects/kinds/volumetric_fog_effect.cpp` registers `driftSpeed`, `driftVertical`, `driftWind` (ADR-571/572 — movement and wind-direction coupling). It registers **no** row named Turbulence, Curl, Swirl, Dissipation, Expansion or Contraction (`grep` for each of the six names returns nothing in that file). So §16 is half done: the bank moves and can follow the wind's direction; it cannot turbulently deform, expand, contract or dissipate. | same file, `shaders/fog.wgsl`, `src/world/fog_field.cpp` | Lane 8 is free (ADR-571's audit: `mediaLane(s, 8u)` has no reader). Curl specifically wants a flow sampled *per position*, not once per medium (ADR-572's revisit note) — a different shape of change from the other five, which are likely scalar multipliers on the existing field. Not blocking; schedule after step redistribution so the field these controls modulate isn't about to change shape again. |
| **§46 C local banks** | **done**, not open. ADR-563 deferred it past §23/§24; ADR-564 (optical depth) and the reorder made it judgeable by eye; ADR-566 then built and shipped it — six primitives, `FieldType::Choice`, a checked bound, a picture (`tools/make_fog_primitive_arms.py`). No action needed; listed here only because the brief for this track named it as open and the record says otherwise. |
| **March whole-frame perturbation** (ADR-702, Consequences) | recorded, not fixed | `shaders/volume.wgsl` / the composite pass | **Investigation item only — do not fix or even fully diagnose it as part of this pass.** Enabling the march for any placed medium, including one behind the camera, shifts up to ~70% of the frame by ≤7/255 per channel (mean 1.4). Open a tracking ADR (704+) that states the measurement (already in ADR-702) and assigns an owner in the volumetric area; run it *before* the step-redistribution change lands (item below), so whatever caused it isn't conflated with a second shared-march edit landing on top. |

---

## 3. The three owner rulings

### Ruling 1 — Terrain-aware fog (ADR-575 §18): DO IT

Bake a terrain height texture once at rebuild (the terrain is static — `Composition::TerrainProducts`
already exists as exactly this kind of rebuild-cache, keyed on a hash of the terrain-affecting
inputs, `src/scene/composition.hpp:428`). Add the baked texture as a new product on that struct, bind
it to the volume pass, and add a `fogGroundFollow` control so the height layer's floor tracks
terrain height instead of a world-space plane ("fog sits in valleys").

- **Extension point**: `volumeDensityField` already binds a named scalar field into
  `volumeDensityAt` (ADR-568's own revisit note names this as the thing to try before a new
  binding). Try it before adding a second GPU binding path.
- **Scope**: this closes 3 of ADR-575 §18's five named routes (world height already works; scene
  depth already works; terrain heightmap and, by extension, ground-hugging fog would now work). Do
  **not** also implement `FieldKind::SdfDistance` here — that is ruling 3, and it is a removal.
- **Other consumers noted, not migrated**: ADR-575 names water and grass as other systems asking the
  same CPU `TerrainQuery::heightAt` question that a baked texture would answer for free. Do not plan
  their migration in this track; note it in the new ADR's Consequences for whoever owns those
  systems.
- **Owner**: whoever owns `Composition`/terrain rebuild, since `TerrainProducts` is shared state, not
  fog's. Coordinate rather than land unilaterally.
- **Acceptance**: before/after renders of a fog bank over sloped terrain with `fogGroundFollow` at 0
  and 1; a guard test analogous to ADR-567/568's — the terrain-following profile must still be
  integrable by the surface pass if it is meant to feed `fogHeightIntegral` (check this constraint
  before implementing; if the baked texture only feeds the march, the surface-pass integral question
  doesn't arise and the height layer's shared-reader discipline (ADR-567, ADR-568) still applies to
  what does change).

### Ruling 2 — Horizon Density (ADR-569 §7): UNIFY (option 2)

Make the surface fog pass (`applyFog`, `shaders/common.wgsl`, exp-squared, `Environment::fogDensity`)
derive its density from the volumetric one (`fs_march`, `shaders/volume.wgsl`, Beer–Lambert,
`Environment::volumeDensity × volumeAbsorption`), so there is one law and one density, per the
brief's §5/§6. This is the most expensive item in this plan and the riskiest: ADR-569 measured that
the two laws agree at exactly one crossover distance (median 79.3 m across shipped scenes) and
diverge sharply beyond it — at 400 m in Glowmere's own flagship scene the two disagree by **48x**
(0.008 vs 0.383 transmittance), and Glowmere's depth separation is built on that divergence.

**Sequencing, exactly as directed:**

1. **Its own branch**, cut from `agent/entity-effects`, not built alongside the rest of this plan.
2. **Census the affected scenes before touching code.** Re-run `tools/fog_law_census.py` (it already
   exists and already answers this — do not hand-roll a new grep, ADR-569's own history is two
   people getting different answers from ad hoc greps). As of this write-up on `agent/effect-library`
   it reports, over 105 tracked non-probe scenes: **36 with both laws active** (up from ADR-569's 30
   — more scenes have shipped since), 25 surface-law-only, 2 march-law-only
   (`grid-catchup-lab.scene.json`, `volumetric-atmosphere-lab.scene.json`), 42 with neither. The
   flagship affected set, confirmed by direct grep of `environment.fogDensity`/`volumeDensity`,
   is the Glowmere family at the modal 79.3 m crossover: `examples/world/glowmere-valley-2.scene.json`,
   `glowmere-valley-2-song.scene.json`, `glowmere-valley-2-multicam.scene.json`,
   `glowmere-atmospherics.scene.json`, `glowmere-stylized.scene.json`, and the four
   `examples/labs/motionmatch/glowmere-*.scene.json` crowd/demo scenes, plus (per ADR-569, re-verify
   with the script) the foot-IK labs under `examples/labs/footik/`. Re-run the script for the
   authoritative full 36 before starting — this list is illustrative, not exhaustive.
3. **Before/after renders of every affected scene.** Not a sample — ADR-569's own §46 warns a
   metric that aggregates over a frame conflates coverage with opacity (ADR-564's measurement trap);
   this has to be looked at, scene by scene.
4. **Re-tune where an intended look is lost.** Glowmere's depth separation currently depends on the
   48x; it must come back, by eye, before this is accepted. This is the single named non-negotiable
   in the owner's ruling.
5. **Then, and only then, implement §7's Horizon Density control** on the unified law — "extra
   extinction that grows with distance," ADR-569's own recommended option 3 vocabulary, now applied
   as the *actual* mechanism rather than as a third law next to the first two.
6. **A new ADR (704+) resolving ADR-569**, recording the before/after evidence and the re-tuned
   parameters, marked as superseding ADR-569's "finding, no behaviour change" status.

This is a repository-wide re-lighting change and should not be merged opportunistically alongside
the tornado/fog structural work in this plan; see §4.

### Ruling 3 — Remove `FieldKind::SdfDistance` (ADR-441, no shims)

ADR-576 chose to *refuse at load* rather than remove, reasoning the seam was real (`Scene::sdfs` /
`SdfRenderer` exist) and nothing shipped used it. The owner's ruling supersedes that: remove it
outright, repository-wide, per ADR-441's "no compatibility shims while in heavy development." Every
reference found today:

| file | what's there |
|---|---|
| `src/spatial/field.hpp:50` | the enum member (`SdfDistance,` — comment: "REFUSED at load until it is bound") |
| `src/spatial/field.hpp:158` | a comment on the `reference` field explaining `SdfDistance`'s use of it |
| `src/spatial/field.cpp:34` | the file-header comment describing the "0 on CPU, 0 on GPU" state |
| `src/spatial/field.cpp:211` | a `switch` arm in the scalar sampler |
| `src/spatial/field.cpp:506–507` | `fieldKindName`'s `"sdfDistance"` case |
| `src/spatial/field.cpp:548` | the exhaustive kind-iteration array (`test_fields.cpp`'s loop consumes this) |
| `src/spatial/field.cpp:836,848–849` | ADR-576's `validate()` refusal branch and its message |
| `shaders/fields.wgsl:405,408` | the GPU-side comment and the `return 0.0` fallback arm |
| `src/graph/builtin_nodes.cpp:1097` | the graph-node UI's name list (`"distance", "sdfDistance"`) |
| `tests/unit/test_fields.cpp:55,225,233,240,246,849` | the name round-trip, the refusal-message case (ADR-576's own test), and the exhaustive-kind case |
| `docs/procedural-graph.md:242` | the field-kind enumeration in the node reference |
| `docs/spatial-data.md:166,235` | the field-kind table and the `reference` field description |

Zero shipped scenes name it (`git grep -l sdfDistance -- examples` returns nothing, confirmed
unchanged since ADR-576), so there is no content migration. Delete the enum value, its two `switch`
arms, its name-table entry, its GPU fallback line, its graph-node UI entry, and replace
`test_fields.cpp`'s cases with whatever the exhaustive-kind loop needs to stay exhaustive over one
fewer kind (do not just delete the assertions — the loop's whole point, per ADR-576, is that it goes
red on a kind missing an implementation on either side; deleting `SdfDistance` from the enum makes
the loop shorter, not weaker). This is a small, mechanical, same-day change with no owner-decision
risk; schedule it early (§4).

---

## 4. Ordering

1. **Diagnose the ADR-702 march perturbation (investigation only, no fix).** Do this first and
   against the current march, before either shared-march edit below lands — otherwise its cause and
   the effect of step redistribution become impossible to tell apart later, the exact trap ADR-560/
   ADR-565 both document ("two changes to shared code in one window" makes attribution ambiguous).
2. **Remove `FieldKind::SdfDistance`** (ruling 3). Fully independent of everything else in this plan,
   cheap, no owner-decision risk. Do it early for hygiene; it can also run in parallel with (1).
3. **Shared march step redistribution** (tornado item 5 = fog §31), once, in `shaders/volume.wgsl`.
   Everything below that touches a narrow feature (the tornado's foot, small fog primitives) is
   affected by this, so it goes first among the structural work. Re-run the Detail=0 byte-identity
   gate and the fog uniform-in-angle-at-zero-detail case immediately after, per ADR-580 §1 and
   ADR-563's probe.
4. **Tornado Tall Column `_tc-4` diagnosis** (item 2). Quick, self-contained, no dependency on (3) —
   it's a field question, not a march question — so it can also run in parallel with (3) if capacity
   allows.
5. **Tornado ground interaction / debris cloud** (item 1). Highest value, benefits from the
   redistributed march for the thin foot band.
6. **Tornado self-shadowing, turned on and re-tuned** (item 3), after (5) so the lighting is tuned
   against the final structure once.
7. **Fog §16's remaining flow controls and §25 colour** (backlog-priority, not blocking anything).
   Schedule after (3) so the field they modulate has stopped changing shape.
8. **Ruling 1, terrain-aware fog.** Independent subsystem coordination (shared `TerrainProducts`);
   can start any time after (3), since it doesn't touch the parts of `volume.wgsl` that (3) changes.
9. **Tornado hero art direction tuning** (item 4). Pure tuning; do it last among the tornado items so
   it's tuned against the final structure (5) and lighting (6), not thrown away twice.
10. **Ruling 2, Horizon Density unification.** Its own branch, cut from `agent/entity-effects`, run
    largely in parallel with the above but **merged last**, after (3)'s `volume.wgsl` change has
    landed on `main` — rebasing a repo-wide re-lighting branch onto a moved shared-march file once is
    much cheaper than doing it twice.
11. **Tornado particles and grid tier** (items 6, 7). Backlog; (7) is blocked on an owner decision
    (ADR-581 §4) that is out of scope for this plan to make.

---

## 5. Decisions not to reopen, and dead ends

**Do not reopen** (`docs/tornado-handoff.md` §6, plus fog ADRs):

- The debris skirt was **dropped**, not restored, in the Tree of Life (`skirtDensity` 0, bass route
  retargeted to `coreDensity`). Item 1 above (ground interaction) is the real fix; do not re-add the
  skirt as a shortcut.
- The Tornado **replaces** the Vortex; no alias, no compatibility shim (ADR-441/ADR-580 §10.3). The
  Vortex kind stays in code (fog stores in `e.vortex`) but is gone from every deliverable except
  twelve `_`-prefixed evidence arms, which must not be deleted — they are cited evidence for three
  other ADRs.
- **Vorticity confinement stays absent from the tornado's analytic tier.** It has no numerical
  diffusion to compensate for; the trigger for implementing it is the grid tier existing, not a
  reviewer noticing the shader lacks the term (ADR-580, "Do NOT fix this later").
- The tornado's silhouette does **not** move into a simulated grid (ADR-580 §4, measured by
  ADR-581). It has to be correct at any `t` with no history.
- The Wedge variant ships weak and stays in the showcase; it is not a bug to chase, the shoulder it
  lacks is intrinsic to its proportions (ADR-580 §8.1, §8.8).
- `edgeWidth` stays removed as an artist control (folded into a constant, ADR-580 §10.1) — do not
  re-add it without also re-solving the lane-15 tag-collision it was removed to avoid.
- **`volumetricStrength` stays at 1.0 by default**, and **Preview's `volumeStepScale` stays 0.5**
  (ADR-579's two directed corrections) — both are disclosed trade-offs, not oversights. Do not
  "fix" either without re-measuring the specific claim ADR-577/579 attached to it.
- **Temporal reprojection / history buffers for the march stay out.** ADR-143 rejected it, ADR-460
  and ADR-577 both re-confirmed the reopening trigger is shut (measured animated grain is ~1e-4 of
  the medium's brightness at the shipped 32 steps — already below the mandatory bar, §29).
- **`fog.wgsl` declares no bindings.** Do not reach into `frame` from inside it for convenience (the
  rejected route in ADR-572) — that is what lets the CPU/GPU parity harness compile it standalone.
- **The flow sets direction, never speed** (ADR-572). Do not read a flow's magnitude into a fog
  control without first branching on `FlowSample::units` — this exact confusion has shipped as a bug
  four separate times in this codebase under different names.
- **`FieldType::Choice` serialises as a name, never an index** (ADR-566) — do not "simplify" a
  primitive-selector row back to a float index; a saved file must survive a reordered enum.

**Dead ends** (handoff §7; fog's own recorded detours):

1. Tornado's provisional medium plumbing (its own uniform block, pre-ADR-562) — replaced wholesale
   at the merge, not kept.
2. Tornado's own fix for the unread `MediumSlot::kind` tag — reverted in favour of the fog branch's
   central fix; two fixes for one defect is worse than one.
3. A tornado-showcase framing rule based on height alone — replaced by `max(height, width)` after it
   made the wedge subtend 71.7° of a 40° frame.
4. Fog: a per-slot cost ladder measured on four large *overlapping* media — the geometry was wrong
   in a way that biased the conclusion in a specific direction (against more slots), not just added
   noise; redone interleaved, on separated media, before any capacity decision (ADR-562 §8).
5. Fog: the first version of `test_fog_bank.cpp`'s density guard sampled the plateau, not the
   silhouette (ADR-563) — a probe that samples the flat interior of a field is indistinguishable from
   the feature not working.
6. Fog: "the march's own sampling grain" was misattributed to the pixel-jitter for months, in the
   generator scripts and in ADR-566 itself — it is undersampling, not jitter (`spatialRatio` measured
   at exactly 1.000, ADR-577). This is why item 5 in §2's table (step redistribution) is the actual
   fix for the §46 C arms' speckle, not a jitter tweak.

---

## 6. Reconstructed requirements — the two lost specs

Neither the 47-section tornado brief nor the 50-section fog brief exists in the repository. Both are
reconstructed here strictly from ADR quotations; a blank cell means no ADR quoted that section.

### Tornado (47-section owner brief, quoted by ADR-580)

| § | requirement (quoted/paraphrased) | citing ADR |
|---|---|---|
| 2 | "do NOT simply rename the existing class/UI from Vortex to Tornado while retaining the same implementation" | ADR-580 §3 |
| 13 | velocity field as `tangential*swirl + axis*lift + normalize(radial)*radial` — three independent knobs | ADR-580 §5 |
| 15 | "cloud mass is the priority"; core/eye with four requested looks (solid, hollow, turbulent, in between) | ADR-580 §5 |
| 16, 28 | asymmetry; a shape that is not a perfect cone; a bent guide curve | ADR-580 §5 |
| 17–19 | artist controls as height-curves/multipliers: Rotation Speed/Curve/Height Influence; Lift/Buoyancy/Updraft; Radial Flow/Compression/Expansion | ADR-580 §5 |
| 20 | vorticity confinement | ADR-580 §5, and "Do NOT fix this later" |
| 21 | (implicit) three independent detail-scale sliders must not collapse to one octave at triple amplitude | ADR-580 §8.7 |
| 24 | hybrid pipeline: analytic field → guide velocity → low-res sim/advection → macro turbulence → procedural detail → volume march | ADR-580 §4 |
| 25 | dust, debris and wisps as a secondary layer | ADR-580 §5 (quoted in `tornado_effect.cpp`'s header), handoff §5.1 |
| 27 | secondary suction vortices, 2–6, orbiting the parent axis | ADR-580 §5, §8.6 |
| 29 | forbid per-frame random noise; ask for advected noise | ADR-580 §5 |
| 32 | EmberGen's physical vocabulary inside a cosmic art direction | ADR-580 §10.4 |
| 33 | panel vocabulary: Shape / Cloud / Ground / Flow / Turbulence / Appearance / Motion; Particles and Performance sections | ADR-580 §8.9 |
| 35 | quality tiers: Draft / Preview / High / Cinematic | ADR-580 §5 |
| 38 | diagnostic modes; Mode 1 = analytic structure only | ADR-580 §4, §8.7 |
| 39 | four-panel structure-before-noise gate (A smooth tornado → D wispy detail); A/B/C must not be a meaningless blob with only D reading | ADR-580 §8.7 |
| 42 | dead-parameter removal work | ADR-580 §6 |
| 43 | 14 design Q&A (solver sufficiency, advection, curl noise, confinement cost, texture resolution, WebGPU compute, etc.) | ADR-580 §5 |
| 45 | the silhouette must be correct at any `t` with no history | ADR-580 §4 |
| 47 | a seven-variant showcase, one field, no per-variant code | ADR-580 §5, §8.8 |

### Fog (50-section brief, quoted by ADR-560–579)

| § | requirement (quoted/paraphrased) | citing ADR |
|---|---|---|
| 3 | diagnose the grain artefact: A density noise / B march undersampling / C stochastic sampling / D temporal instability / E shadow sampling / F high-frequency density detail / G combination | ADR-560 §2 |
| 4 | eight diagnostic modes (A constant density … H shadowing disabled); §4 D = noise disabled | ADR-560 §4 |
| 5 | "the effect should conceptually generate a scalar field `density(x,y,z,time)` … the volume renderer consumes this field" | ADR-569 |
| 6 | `density = baseDensity × heightFalloff × distanceFalloff` plus local volume contributions | ADR-569 |
| 7 | "a robust height-based density model with artist controls: Height, Height Falloff, Ground Density, Upper Density, Height Curve, Horizon Density … dense near ground → gradually thinner → clear atmosphere … not restricted to a simple linear gradient" | ADR-568 |
| 9 | six local placeable volume primitives (sphere, ellipsoid, box, capsule, cylinder, bank), placed/scaled/rotated, density/softness/falloff/height influence, several at once | ADR-566 |
| 13, 14 | "noise distorts large boundaries, it does not create the density" | ADR-565 |
| 15, 16 | movement: sample the field at `position − velocity×time`; Drift Direction control | ADR-571 |
| 17 | "optionally respond to a procedural flow field: directional wind, curl noise, world-space flow, vertical convection, terrain-following drift. Important: flow affects movement, not basic existence" | ADR-572 |
| 18 | "Investigate terrain-aware fog via world height, scene depth, terrain heightmap, depth buffer, scene collision/depth, signed-distance approximation … Do not make this depend on expensive per-frame CPU scene traversal; use GPU/depth-based approaches where practical" | ADR-575 |
| 19 | depth integration (fog stops at geometry; no bleed across silhouettes) | ADR-573 |
| 20, 22 | "responds convincingly to directional light … bright illuminated mist, dark moody fog, backlit fog"; light shafts from the same volumetric density, real integration over a 2D radial blur | ADR-570 |
| 23 | softness over detail | ADR-564 (title) |
| 24 | a density response curve / remapping | ADR-564, ADR-571 |
| 25 | fog colour, scattering colour, absorption colour, height colour, distance colour; "avoid making colour responsible for structure" | ADR-575 |
| 26 | emission: intensity, colour, height influence, density influence | ADR-575 |
| 27 | local (clustered) light response — "a major visual upgrade for Glowmere and Tree of Life" | ADR-573 |
| 29 | "Mandatory. No crawling noise, shimmering, temporal popping, density flicker, unstable shadows or animated grain. Investigate temporal reprojection, history buffers, blue-noise sampling, interleaved sampling, jitter stabilization, clamping, history rejection. Use the least expensive technique that produces stable results." | ADR-577 |
| 30 | ray-march quality tiers; "do not make High merely mean throw samples at it" | ADR-577 |
| 31 | adaptive sampling / step redistribution | ADR-562 §4 (own comment), ADR-577 |
| 32 | empty-space optimisation | ADR-562, ADR-566, ADR-577 |
| 36 | panel structure: sections, pages, a Quality group | ADR-579 |
| 37 | ten presets; "presets are starting points" | ADR-579 |
| 38 | a debug visualisation of the diagnostic modes | ADR-560 §4 |
| 39 | "Profile density generation, compute/advection, ray marching, shadow integration, temporal reprojection, memory, 3D texture updates. Capture GPU ms, CPU ms, memory, resolution, step count, active volume count. Do not guess where the cost is." | ADR-578 |
| 44 | Definition-of-Done bar 1: "noise disabled → still looks like fog" | ADR-560 |
| 46 | phase order: A diagnose, B clean analytical volume, C local banks, D height and distance | ADR-560, ADR-562, ADR-563 |
| 48 | "bad density × 64 samples = better-sampled bad density" | ADR-560 |
| 53 | the noise-vs-flat weight control (`cloudNoise`) | ADR-560 |

---

## 7. No-owner-decision items: a self-contained brief

Three items need no ruling and can start immediately: tornado items **1** (ground interaction /
debris), **2** (`_tc-4` diagnosis) and **3** (self-shadowing turn-on). One engineer, in this order
(2 and 3 can be done first if 1 is still in review):

**Read first**: `docs/tornado-handoff.md` in full (484 lines), then ADR-580 §1 (the structure-before-
noise constraint) and §8.7 (how the gate is proven), then ADR-570 (the shared self-shadow march) and
its "off by default" cost table. Do not start without having re-run the Detail=0 gate yourself once,
against the current `main`/`agent/effect-library` tip — the handoff's own §10 shows this exact gate
moved under the branch once already.

1. **`_tc-4` (half a day).** Run the reproduction command in `docs/tornado-handoff.md` §5 item 2.
   Add a CPU probe to `tests/rendering/test_tornado_parity_gpu.cpp` that samples `tornado::evaluate`
   down the displaced axis at `h ≈ 0.4` on the `_tc-4` parameters. Report which of the two named
   outcomes it is (field hole vs. contrast floor) before writing any fix. If it's a field hole, the
   fix must keep passing the Detail=0 byte-identity gate. If it's a contrast floor, no code change —
   write the finding into a short note and close the item.

2. **Self-shadowing (half a day).** Set `volumeShadowSteps` to 2–4 on the hero Tree of Life project,
   render, compare by eye against the current shipped hero (too bright / glowing-gas read, per
   ADR-580 §10.4). This is a parameter change plus a review render, not new code — ADR-570 already
   built and shipped the mechanism. If the cost is a concern, ADR-570's own numbers (2.2–2.6x per
   volumetric light, linear in light count) are the budget to check against before turning it on by
   default anywhere.

3. **Ground interaction / debris (the real work, 2–4 days).** Design and implement a density term
   that reads as debris/dust where the funnel's flat base meets its foot — see ADR-580 §5's "The
   condensation shell" for the vocabulary the rest of the field already uses (shell density, core
   falloff), and reuse it rather than inventing a fifth density term with its own rules. Constraints:
   (a) must still pass the Detail=0 gate — the new term is structure, evaluated in the analytic path,
   not noise; (b) must extend `world::mediumBound`'s tornado arm (and its CPU/GPU pair,
   `test_medium_bound.cpp`) if it changes the field's spatial extent; (c) no new artist controls
   unless they do something — ADR-421's rule, enforced everywhere else in this track. Acceptance is
   a before/after hero render reviewed by the owner; there is no automated pass/fail for "does this
   read as touchdown."

None of the three touches `shaders/volume.wgsl`'s shared march machinery, so none is blocked on, or
blocks, the step-redistribution work in §4 — though doing ground interaction after redistribution
(per the ordering) avoids tuning the new debris term against a march that's about to change how it
samples narrow features.
