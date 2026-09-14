# Glowmere Valley 2 — implementation plan, risks, and what the user must decide

Phase 1 deliverable 5. The brief supplies Phases 1–3 and was truncated after Phase 3; Phases 4–7
below are inferred from its §§7–11 and **must be confirmed before Phase 4 starts** (Q7).

---

## 1. The plan

Each phase names its acceptance criteria as things that can be *checked*, because the brief's
§12 asks for acceptance and because this repo's habit is that a claim without a measurement is not a
claim.

### Phase 1 — research and architecture *(this phase; complete)*

Delivered: `01-audit.md` (audit + reuse matrix), `02-research.md` (research, design, and the
candidate-search architecture), `03-baseline.md` (build, test and render baseline), this document,
and the ADRs listed in §4.

### Phase 2 — geography

**Do:** a new scene and project under a new name; a new `WorldMap` — not an edit of `defaultWorld()`
— with a river centerline whose endpoints lie on opposite map boundaries; the two missing terrain
terms (`roughness(|d|)` and a queryable `HAR`); curvature-aware banks; the existing water system
pointed at the new course; the Glowmere rig, palette, atmosphere and post values ported; a camera
placed to see the valley. **Do not** touch vegetation beyond what is needed to judge the landform.

**Acceptance:**
- the river enters within *x* m of one boundary and exits within *x* m of another, asserted in a test
  against the centerline rather than judged by eye;
- water surface elevation is monotone non-increasing along the course (a test);
- `HAR` is finite and continuous everywhere in bounds (a test);
- renders from ≥4 viewpoints, looked at, showing a readable valley;
- **the original Glowmere Valley still passes `03-baseline.md` §6** — every row.

**First task, before any of the above:** an uncontended frame-time baseline for the original
(`03-baseline.md` §5). Everything downstream needs a budget and there is not one.

### Phase 3 — vegetation

**Do:** extend `world::scatter` rather than replace it — add HAR to the habitat inputs, soft-shoulder
the slope/altitude bands, add per-species minimum spacing, add the single-pass FON acceptance test,
fix the `maxInstances` truncation. Port the 13 species and the emission ladder. Start with four
families, not thirteen.

**Acceptance:**
- `ρ` (relative radius, `02-research.md` §2.4) in `[0.65, 0.85]` per species, asserted;
- generation is bit-identical across two runs and across a rebuild (a test);
- riverbank species' HAR distribution is concentrated in the 0–2 m band and upland species' is not
  — a histogram assertion, not a look;
- open ground exists: some stated fraction of the map below a density threshold;
- CPU generation time recorded; visible instance count within the budget Phase 2 established.

### Phase 4 — hero mushrooms *(inferred — confirm)*

**Do:** the per-angle radius term on the sweep; extract `makeLathe`; the generator; then the
candidate search of `02-research.md` §4 — schema, Sobol sampler, validity gate, scorer, feature
vector, farthest-point selection, contact sheet. Run it. Look at the sheet. Choose six. Record why.

**Acceptance:** `02-research.md` §4 in full, plus the addendum's §19 list — and specifically the
coverage experiment of §4.4, whose result decides whether the sampler stands.

### Phase 5 — heroes and effects *(inferred — confirm)*

Wanderer promoted to a hero and staged deliberately; UFO staged for the four shots the brief names;
`monument-spire`, `far-arch` and `beacon-grove` excluded; the 23 routes ported with documented
ranges; **`glowmereTissue` or its successor made to reach more than 1.6% of the vegetation.**

### Phase 6 — the Auto-director *(inferred — confirm)*

The rename; the panel; the continuous path (one C¹ spline, arc-length parameterised, damped aim);
lateral clearance; Edited-sequence mode as today's behaviour. **Acceptance: a continuous offline
render with no cut, and position/velocity continuity asserted numerically at every former shot
boundary** — not judged by watching it.

### Phase 7 — integration *(inferred — confirm)*

`examples/index.json`; configuration documented; assets resolving; real-time, offline and
Auto-director all working; the original untouched.

---

## 2. Risk register

Ordered by expected cost, not by probability.

| # | risk | why it is real | severity | mitigation / first check |
|---|---|---|---|---|
| **R1** | **Camera clearance is vertical-only and only ever raises** | `src/world/camera_clearance.cpp`. Fine for cuts; a continuous take that dollies to a mushroom and arcs around it will be *lifted over* the subject. And a vertical correction on a smooth spline reintroduces the C¹ breaks the whole design removes | **High** | Prototype lateral clearance in Phase 6 *before* the path fitter is finished. This is where the Auto-director work will actually go |
| **R2** | **The scene's cost is coverage, and a bigger map has more of it** | ADR-126/151: 45% of triangles are sub-pixel and cover 2% of the frame; the ecology is 12.4 of 22.3 ms and **84% of that is rasterising survivors**. A larger map does not add cost through instances, it adds it through *visible* ones | **High** | Budget in visible instances and covered pixels, never in placed instances. Re-run `[.perf][representation]` at each phase |
| **R3** | **There is no frame-time baseline** | `03-baseline.md` §5 — the one taken was contended by an open `avgen` window | **High** | Phase 2's first task, on a quiet machine. `pgrep avgen` before any timing |
| **R4** | **Which scene is "Glowmere Valley"** | Two files carry the name; the index's literal *Glowmere Valley* is the one with no heroes, no wanderer and no UFO | **High** | **Q1.** Blocks Phase 2's framing, not its code |
| **R5** | **`WorldMap` is a fixed extent with no streaming** | Every chunk at every LOD is built up front. Growing 640 m to a map a river can traverse multiplies chunk count by the square of the ratio | Medium | Measure load time and memory at the candidate extent in Phase 2, before committing to it. `viewDistance 520` and `chunkSize 40` must be re-derived, not inherited |
| **R6** | **Per-instance data has exactly one free lane** | `InstanceRecord` is 96 bytes, `static_assert`ed; `emissive.a` is the only free float | Medium | Decide in Phase 3 what that lane carries. Adding a lane changes the struct, the shaders and every consumer |
| **R7** | **`kMaxGpuSplines = 16`, and Glowmere already exceeds it** | It logs "22 splines; only the first 16 are available on the GPU" *today*. A longer river plus mushroom profile curves will make it worse | Medium | Count splines in Phase 2. May force the river centerline to be CPU-side only, which is fine — but it must be a decision, not a surprise |
| **R8** | **The rig's name is a route target** | `music.downbeat → lightrig/GlowmereValley/elder-practical/intensity`. Renaming the rig silently unbinds it — unresolved routes stay enabled but inert by design | Medium | A test that asserts every route in the GV2 project resolves after load. Cheap, and it would have caught this class of bug already |
| **R9** | **Two agents build one candidate-search framework** | The Tree of Life brief is structurally identical (`02-research.md` §4.2) | Medium | **Q6.** Keep the four-member generator seam whichever lands first; do not build in the other worktree |
| **R10** | **The rename breaks a test on purpose** | `tests/unit/test_help.cpp:461` asserts `menuPath == "Camera > Direct to Music"`; `docs/help/features.json` carries the feature ids | Low | Expected, not a risk to avoid. Three unrelated "director" symbols must **not** be renamed: `app::WorldDirector`, `seq::Director`, `entity::Authority::Director` |
| **R11** | **`ecology.cpp`'s `maxInstances` truncates from −Z** | Inner-loop `break`. Invisible at 640 m; a hard edge on a bigger map | Low | Fix in Phase 3; it is a small change |
| **R12** | **The scoring weights will be wrong** | Every aesthetic scorer's weights are wrong initially | Low | Designed for: components stored separately, weights as data, human override recorded (`02-research.md` §4.7, §4.9) |
| **R13** | **Scope.** The brief is seven phases of work | Phase 1 alone consumed a full session | Low, certain | Phase boundaries are the mitigation. Each phase leaves the original working and the tree green |

**Retired by Phase 1's findings:** *"the water system may not follow a curve"* — it does
(`01-audit.md` §1.3). *"terrain may need a new generator"* — it does not; feature-stamping is already
the selected architecture. *"impostors may be needed for a denser map"* — ADR-151 and ADR-153 measured
this content and refused them; **do not re-propose them without a new measurement on new content.**

---

## 3. Open questions for the user — Phase 2 should not start until Q1–Q3 are answered

**Q1. Which scene is the parent?** `glowmere-stylized.scene.json` (16 nodes, 5 heroes, wanderer,
UFO, water dressing — listed as *Glowmere Valley - Painterly*) or `terrain.json` (the 38k-instance
generated world, listed as *Glowmere Valley*)? **This phase assumed the former.** If the answer is
the latter, the audit's reuse matrix changes substantially — there is no wanderer and no UFO to
carry over.

**Q2. How big is the map?** The brief wants a river traversing it and cinematic distance; the engine
wants a fixed extent built up front. 640 m today. 1 km? 2 km? This sets the load-time and memory
budget and is the single most consequential number in Phase 2.

**Q3. What is the performance target?** Glowmere's own documented figure is a 31.2 ms wall median at
1440×900 and `docs/TODO-glowmere-handoff.md` records 23.7 FPS at 2880×1800 as a parked defect. Is
GV2 expected to be *faster* than Glowmere, the same, or is richness worth being slower? Everything
in Phases 3–5 trades against this answer and nothing in the brief states it.

**Q4. Does Glowmere Valley 2 replace Glowmere Valley in the showcase?** The brief says do not destroy
the original and also says GV2 is *the* showcase. Both can be listed; the question is which one a new
user opens first, and it is an editorial decision, not a technical one.

**Q5. Six hero mushrooms — six *designs*, or six *individuals*?** The brief's A–F read as six
species. Six species of one genus need a shared visual language that six individuals do not. It
changes the schema's ranges and the diversity selection's target.

**Q6. Where does the candidate-search framework live?** Shared `search::` module, or two independent
implementations? Sharing is cheaper and slower to land, because it needs the two agents to agree on
the seam. **A decision is needed before either Phase 4 starts**, not after.

**Q7. Confirm Phases 4–7.** The brief was truncated after Phase 3. The phases above are inferred
from §§7–11 and are my reading, not the user's instruction.

**Q8. Is the Wanderer's `interest` subject `"elder"` a live bug?** It names no node and no hero
(`01-audit.md` §1.10). This phase could not settle it by reading. If it is a bug, it is a bug in the
*original* and fixing it changes the original's behaviour — which the brief forbids doing silently.
