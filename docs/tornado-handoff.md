# The Tornado world effect: what exists, what is proven, and what is left

**Read this first.** This document was **reconstructed from the committed record by an agent that
did not build the effect.** The agent who did is gone and cannot be asked. Everything here is
either read off a commit, an ADR, a scene file, the source, or a measurement taken while writing
it — or it is marked as an inference. Where the record does not say, this document says the record
does not say, rather than filling the gap.

Two consequences you should act on:

- **There is no reliable record of what was tried and abandoned.** The "Dead ends" section below
  lists only what an ADR or a commit message actually states. It is short. That is a fact about the
  record, not about the work: a great deal was certainly tried and is not written down anywhere.
- **Each section below is tagged *evidenced* or *inferred*, and cites what evidences it.** Where a
  single claim inside an evidenced section is a guess, it is marked inline. Treat anything marked
  inferred as a hypothesis you still have to test, even where it sounds confident.

**And one thing this document did not know when it was written.** While it was being written,
`agent/fog` rebuilt the shared volumetric foundation *underneath* this branch and landed it on
`main`. **Read §10 before you act on §5**, because it removes one of §5's blockers outright and
changes where two of its files live.

Point of entry for detail: **ADR-580** — the whole design and nine phases of findings; §1 to §10
are the original agent's, **§11 is this agent's** and says so at its head — and **ADR-581**, the
simulated-grid census. This document is meant to be actionable without opening either; it is not
meant to replace them.

---

## 1. The target, and the constraint that outranks everything else

*(evidenced: ADR-580 §1-§5 and §8.7; the owner's brief, whose section numbers ADR-580 quotes by
number throughout — brief §38/§39 are the structure-before-noise gate)*

A tornado is a **vertical** phenomenon read from the side. The effect it replaces — the Cosmic
Vortex — is a cyclone: eye, eye wall, spiral rainbands, all features of a horizontal plane you look
*down* at. ADR-580 §3 goes through `vortex.wgsl` term by term and concludes no parameter of one is
a parameter of the other. **The Tornado is not a renamed Vortex and could not have been.**

**THE HARD CONSTRAINT, and the first thing a new agent will violate:**

> **The effect must read as a tornado structurally BEFORE any noise is applied.** Noise is detail
> on top of correct structure, never a substitute for it.

This is not a style preference. It is enforceable and it has been enforced: ADR-580 §8.7 records
that with `cloudAmount` (the Detail control) at 0 the frame is **byte-identical to the analytic
field**, and the review sheet's panel A — structure only, no noise anywhere — reads as a tornado on
its own. If you change the field, that gate is what you re-run.

References the work was built against: **EmberGen** artistically, **SideFX** procedurally,
**GPU Gems** for rendering.

---

## 2. What is built and PROVEN

*(each row cites what proves it; "proven" here means a measurement or a test exists, not that it
looks good)*

| thing | evidence |
|---|---|
| A new `Tornado` kind, analytic, no simulation state | `src/core/tornado.{hpp,cpp}`, `shaders/tornado.wgsl` (535 lines), `src/world/world_effects/effects/tornado_effect.cpp` (755 lines) |
| It is reachable from the **World Effects panel** | `effect_registry.cpp` registers `tornadoSchema()`; the panel draws one button per schema with a factory (`world_effects_panel.cpp`, "ADR-500: one button per declared kind"); `tornadoSchema` sets `factory`, `displayName`, `addLabel`, and `resolve.bucket = EffectBucket::Medium`. **Verified by reading both ends of the registry, not by a screenshot** — nothing here has looked at the running UI. |
| It ships in **content**, not only in a lab | `tree-of-life-floating-island.json` and `.scene.json` both author a `Cosmic Tornado`; the day project and the night project each render one (§4 below) |
| CPU and GPU evaluate the same field | `tests/rendering/test_tornado_parity_gpu.cpp` (587 lines); ADR-580 §8.5 |
| Structure reads without noise | ADR-580 §8.7: `cloudAmount 0` is byte-identical to the analytic field |
| Seven distinct storms from one field, no per-variant code | `examples/labs/tornado-showcase.scene.json` plus `_tc-1`..`_tc-7`, derived by `tools/make_tornado_showcase.py`; five of the seven read well |
| The detail term has mean exactly 1 | ADR-580 §8.7 — this is what keeps `density` a per-metre coefficient (ADR-389's family rule) |
| The ray interval is a provable bound, not an estimate | `shaders/volume.wgsl::mediumInterval`, tornado arm: cylinder radius is the Bezier convex hull of the three artist radii plus the axis excursion |

### The 4.7x cost claim, and whether it holds today

*(evidenced as to provenance; see the tag on the reproduction)*

`392c7e62` and ADR-580 §10.5 claim, in the hero shot, same camera, 32 steps, 1920x1080, same tree,
interleaved under `tools/gpu-lock.sh`, minima over repeats:

| arm | `volume.march` |
|---|---|
| the shipped Cosmic Vortex, recovered from `fd71b736` | 6.16 ms |
| the Cosmic Tornado that replaced it | 1.31 ms |
| no medium at all | the pass does not exist |

**Is it reproducible today? Yes — re-measured, and it comes back larger.** *(evidenced: ADR-580
§11.5, measured while writing this document)* Three interleaved rounds under `tools/gpu-lock.sh`,
same binary and shaders for both arms, 1920x1080, 32 steps, 60 frames, `--bench-json`:

| arm | `volume.march` (median of 3 rounds) | GPU frame p50 |
|---|---|---|
| the Cosmic Vortex, recovered whole from `fd71b736` | **5.83 ms** | 14.6 ms |
| the Cosmic Tornado that replaced it | **1.11 ms** | 9.6 ms |

**5.2x**, against the committed claim of 4.7x, with frame medians inside the ranges §10.5 quoted —
which is the control saying this is the same measurement rather than a different one. The ratio
moved up for two reasons, both benign: §10.5 quoted a minimum over repeats and this quotes a median
of medians, and the tornado arm measured here is the **skirtless** one (§6), which is less non-zero
density and therefore less coverage.

Two caveats that matter more than the number:

- **Neither arm predicts the merged tree.** This branch is **15 commits behind `main`**, and
  `agent/fog` has changed `shaders/volume.wgsl` since. Both arms ran here, which is what makes them
  comparable *to each other*.
- **Nothing is claimed about combinations.** ADR-580 §10.5 records that per-slot cost is **not
  additive** — dense media in one frame subsidise each other through the transmittance early-out
  while thin separated ones do not. A number taken with fog also on is not comparable to either arm.

---

## 3. What is built and NOT proven

*(the separation matters: everything here works in the sense that it runs, and nothing here has a
measurement or a test behind it)*

- **The hero shot's art direction.** ADR-580 §10.4 records the frame as **unfinished rather than
  shipped**: too bright, reads as glowing gas rather than smoke, and the wall cloud is cut off by
  the top of frame — which loses the thin-column-under-a-broad-cloud proportion §8.1 identified as
  the thing that makes a tornado read as a tornado. Those two are untouched.
- **The Wedge variant** is the weakest of the seven and ADR-580's closing list believes the
  weakness is *intrinsic*: a wedge is as wide as it is tall by definition, so it has no shoulder between column
  and cloud, and the shoulder is what sells the others. It ships weak rather than tuned, on purpose.
- **The camera sweep.** *(evidenced by the review sheet `5-camera-sweep.png` and its notes — **not**
  by any ADR; ADR-580 does not contain this finding.)* The effect holds at close, side, far,
  three-quarter and below, and **fails from directly above**, where the wall cloud hides the funnel
  and it reads as a mushroom. That angle was put in the sheet on purpose rather than quietly
  omitted.
- **Suction vortices** exist and are, in the previous agent's own words, "weaker than I wanted"
  (commit `20631c4d`, ADR-580 §8.6, which gives a geometric reason).
- **Nothing is claimed about combinations.** No shot with a tornado and a fog bank together has
  been measured or judged.

---

## 4. Wiring: what a scene actually loads

*(evidenced: `src/app/engine.cpp` around the `atmosphericEffects` reader; verified by rendering)*

This is the part most likely to waste your day, so it is stated as mechanics rather than as advice.

- **A project's `atmosphericEffects` block REPLACES the scene's list** (ADR-264 / ADR-387 §19).
  There is exactly one exception, and it is a *migration* arm: if the project declares no effect of
  kind `Vortex`, the reader copies one `Vortex` — and only a `Vortex` — out of the scene's list.
- So the two Tree of Life halves are **both live, for different deliverables**:

  | file | what it is | what it draws |
  |---|---|---|
  | `tree-of-life-floating-island.json` | the day project, authors its own list | `[comet, tornado]` — its own |
  | `tree-of-life-floating-island.scene.json` | the composition | the tornado, for anything that does not override |
  | `tree-of-life-floating-island-night.json` | the night project, **no `atmosphericEffects` at all**, names the scene above | the **scene's** tornado |

  **Deleting either copy breaks a shipped shot.** Verified by rendering both at `--range 6:6`:
  day `c57c8bcee871b688`, night `4ca76bbcc98637fa`, column present in both.
- `kMaxMedia` is **4** (`atmospherics.hpp`), raised from 1 by ADR-562. A fifth medium increments
  `AtmosphericCounts::dropped`.
- **`dropped` still reaches no user interface.** ADR-580 §8.8 turned that from a code census into a
  blank deliverable and it is still blank. The panel warns only when `ahead >= kMaxMedia`, computed
  from the authored list. **If an effect you add simply does not appear, this is the first thing to
  suspect and nothing will tell you.**
- The Vortex is gone from **every deliverable** and survives only in twelve `_`-prefixed evidence
  arms under `examples/treeisland/`, which ADR-580 §10.3 protected on purpose: deleting them
  destroys the evidence for ADR-460, ADR-461 and ADR-560.
  **Census it by parsing, never by grepping.** Counted today by walking every `examples/**/*.json`:
  12 files author an atmospheric Vortex and all 12 are those evidence arms. `git grep '"kind":
  "vortex"'` over `examples/` returns **18** — the extra six (`constellation`, `infinite`,
  `library`, `machine`, `reassembly`, `stress`) are a `spatial::FieldKind::Vortex` driving
  *particles*, an unrelated subsystem that happens to share the word, with no atmospheric effect
  anywhere in them. A grep-driven removal would delete particle motion from six scenes; ADR-580 §9
  and ADR-581 §1 are two more instances of the same collision.

**On ADR-500's "an effect is one file and four lines":** the Tornado does not meet it and says so
in its own source. `tornado_effect.cpp`'s header comment: "ADR-500 is explicit that a kind reaching
the picture through an integrator the engine already has is one file, and a kind needing a NEW one
needs WGSL, a GPU struct and a renderer change. **This is the second kind.**" The march gained a
third density term, `VolumeUniforms` gained twelve `vec4`s, and `core/tornado.{hpp,cpp}` and
`shaders/tornado.wgsl` are new. What the registry still buys — the panel, the JSON, the modulation
targets, the timeline keys, the save entries — is everything above the march, and that part *is*
one file. Read the rule as satisfied in the layer it governs, not violated.

---

## 5. What is LEFT, ranked

### 1. Ground interaction — a debris cloud where the funnel meets something. *The highest-value work on this effect.*

*(the need is evidenced; the design is yours)*

The funnel's own bottom is a **flat horizontal cut**, not a shape. `core/tornado.cpp::evaluate`:

```
if (h > 1.08f || h < -0.02f) { return s; }                            // a hard plane
const float foot = smoothstepf(reach - footSoft, reach + footSoft, h);  // footSoft = 0.04
```

At `touchdown = 1`, `reach` is 0, so that is a smoothstep from `-footSoft` to `+footSoft` — 0.08
of the height, 112 m of a 1400 m column — sitting on a hard plane. Seen at the hero camera's 8.8
degree depression it is an ellipse. The debris skirt used to sit on top of it
and hide it; the skirt has now been dropped (§6), so the cut is what is left.

**Dimming will not fix it and neither will the skirt.** What fixes it is structure in front of it:
a debris cloud, dust, particulate — the term that says the column is interacting with something.
The **owner's brief §25** wanted dust, debris and wisps as a secondary layer — quoted in
`tornado_effect.cpp`'s header — and **none is implemented**; there are deliberately no controls for
them, on the ADR-421 rule that a control doing nothing is worse than an absent one.

### 2. The Tall Column defect — `_tc-4` renders with its middle missing

*(reproduced today; the diagnosis below is evidenced by five renders and is **not** the previous
agent's — the record says only "not yet diagnosed")*

Full write-up in **ADR-580 §11.3**, including hashes. In brief:

```
tools/gpu-lock.sh ./build/release/src/avgen --headless \
  --composition examples/labs/_tc-4-tall-column.scene.json \
  --render <out> --format png --range 6:6 --fps 30 --size 1280x720
```

Ruled out: screen-space effects (identical at 2560x1440), noise (identical with Detail at 0), and
under-sampling (512 steps, per the previous agent).

**Localised to the axis displacement.** `_tc-4` is the only one of the seven presets with a
non-zero `lean` (120 m on a 2400 m column) and it also carries `wobbleAmount` 70. Zeroing both
restores the whole column; zeroing `lean` alone restores about half of it.

*Inferred, untested*: this may be a **contrast floor rather than a hole** — the displaced funnel
presents a shorter chord to each ray, the column at these values is already barely above the
background, and spreading the same mass over more screen area drops it under. If so, nothing is
being skipped and no step count or bound will help. **The measurement that settles it** is a CPU
probe of `tornado::evaluate` down the displaced axis: a hole in the CPU field means the field, no
hole means the march. `tests/rendering/test_tornado_parity_gpu.cpp` already has the harness.

Note the consequence if the field turns out to be at fault: this is a failure of the **analytic
structure**, which is exactly what §1's constraint is about.

### 3. Self-shadowing — ~~blocked~~ **unblocked; read §10 before planning this**

*(evidenced: ADR-580 §7, Phase 5, and the review sheet's "WHAT IS NOT DONE" — then superseded)*

The tornado is lit but casts no shadow into itself. It needs a **shared light march in
`shaders/volume.wgsl`** that does not exist; the fog agent owns that file. Nothing is budgeted for
it inside this effect. This is a large part of why the hero column reads as glowing gas.

**That paragraph was true when it was written and is not true now.** ADR-570 built the shared
march, kind-dispatched so this effect is carried by construction, and it is off by default. See
§10. This is no longer the third-ranked project it is ranked as here; try turning
`volumeShadowSteps` up and re-rank it against what you see.

### 4. The hero's remaining art direction

Too bright, and the wall cloud cut off by the top of frame. Both are tuning against the owner's eye
and neither is blocked on anything.

### 5. Step redistribution

*(evidenced: ADR-580 §8.3 and its closing list)* The per-slot ray interval **skips** samples
outside a medium but does not **place** them inside it, so a thin column is still limited by the
global step count. ADR-580 §8.3's rope table and `agent/fog`'s 7% are the two arguments for doing
it. A separate change.

### 6. Particles

Dust, debris, embers. Not implemented, no controls, on purpose (see §5.1).

### 7. The grid tier — do not start this without reading ADR-581 §4 first

The Tornado is deliberately analytic and **does not use** the engine's GPU fluid solver. ADR-581
measured why that matters: at `kCatchUpSteps = 240`, `--range 6:6` and `--range 30:30` render
**byte-identically**, because both skip to a state that was never simulated. It does not violate
ADR-360 (two renders of one range agree) and it does break `--range t:t`, which is the idiom this
whole repository looks at single frames with. **If the grid tier is built, one of ADR-581 §4's four
options has to be chosen first.**

---

## 6. Decisions already taken — do not reopen these

| decision | who | why |
|---|---|---|
| **Drop the debris skirt in the Tree of Life.** `skirtDensity` 0 in both halves, and the `audio.bass` route retargeted to `coreDensity` so the skirt does not return on every beat. | **The agent coordinating this handoff** — *not* the owner and *not* the agent who built the effect. ADR-580 §10.4 offered three options and deliberately chose none. | The skirt asserts "this column is tearing up the ground" and the Tree of Life is an island in open space. The rejected alternative, "give it something to touch", adds a cloud deck — a set-dressing change, out of scope for a rendering task. **It is only half the fix**: see §5.1. |
| The Tornado **replaces** the Vortex rather than sitting beside it; no compatibility alias. | ADR-441, applied by ADR-580 §10.3. | Cut effects come out and are stripped from scenes while the project is in heavy development. |
| **Vorticity confinement is absent from the analytic tier on purpose.** | ADR-580, "Do NOT 'fix' this later". | Fedkiw/Stam/Jensen's term puts back energy that *numerical diffusion* removed from a grid. An analytic field is not integrated and has no numerical diffusion, so the term would compute a force added to a velocity field nothing integrates — ADR-421's dead control. The `Vorticity` sliders that exist drive the curl-noise amplitude and the suction vortices, which are real. **The trigger for implementing confinement is the grid tier existing**, not a reviewer noticing the word is missing from the shader. |
| The silhouette does **not** live in a simulated grid. | ADR-580 §4, measured by ADR-581. | It has to be correct at any `t` with no history, for an artistic reason and a determinism reason that happen to agree. |
| The Wedge ships weak rather than tuned, and stays in the showcase. | ADR-580's closing list. | An honest weak case beats one flattered by a camera chosen for it. |
| `edgeWidth` was removed as an artist control and folded into a constant. | ADR-580 §10.1. | Lane 15 became the medium kind tag; sixty-one floats had to become sixty. One control removed deliberately beats a packer that silently overwrites the tag selecting its own density function. |
| Review pixels are not committed. | `.gitignore`'s own paragraph; enforced here in ADR-580 §11.6. | The 1.8 MB hero frame committed in `392c7e62` has been removed and the ignore rule, previously anchored at the repository root, is now also written unanchored. |

---

## 7. Dead ends

**The committed record contains three, and no more.** This is the section where a reconstructed
document would be most tempting to embellish, so it is deliberately confined to things an ADR or a
commit message states in so many words.

1. **Provisional medium plumbing, replaced rather than kept.** *(ADR-580 §10.1)* This branch built
   its own parallel uniform block and per-effect frame slot before ADR-562's medium slots landed,
   "described from the start as a move rather than a rewrite". The rebase took the foundation's
   side for every file it owned.
2. **This branch's own fix for the unread `MediumSlot::kind` tag was reverted** in favour of
   `agent/fog`'s central fix. *(ADR-580 §10.1)* Two fixes for one defect is worse than one.
3. **A framing rule based on height alone**, for the seven-variant showcase.
   *(`tools/make_tornado_showcase.py` module docstring)* It made the wedge subtend 71.7 degrees of
   arc in a 40 degree frame and rendered two variants as a featureless grey wall — which reads off
   the contact sheet as two variants failing and is actually one rule failing on anything wider
   than it is tall. Replaced by framing on `max(height, width)`.

**And one expensive detour, recorded as a lesson rather than as a dead end** *(ADR-580 §10.2)*: six
renders were spent tuning brightness, steps and scattering while the real fault was that the medium
kind tag never reached the shader, so a tornado's lanes were being read by `vortexShapeAt` — a
height read as a radius, a taper read as extinction per metre. The two moves that would have saved
five of those renders are worth stealing:

> When a knob does nothing, **compare hashes across configurations that should differ wildly.**
> Identity across unrelated inputs says the input is not reaching the computation at all.

> The first question about an unresponsive control is not "what value" but **"is this code
> running"** — disabling the effect entirely should be the *first* move, not the seventh.

**What the record does NOT contain**, and what this document therefore cannot tell you: which field
formulations were tried and rejected, which parameter ranges were walked, what the wedge was tuned
against before it was declared intrinsically weak, or why the Tall Column was left undiagnosed
rather than chased. If you need those, they are gone.

---

## 8. Orientation: the files, in the order worth opening

| file | what it is |
|---|---|
| `docs/decisions/ADR-580-…` | the whole design and nine phases of findings; §1-§10 are the original agent's, §11 is a second agent's |
| `docs/decisions/ADR-581-…` | the simulated-grid census; read before any grid work |
| `src/core/tornado.hpp` | the artist-facing field, one commented struct; the best single read |
| `src/core/tornado.cpp` | the CPU evaluation — `evaluate()` is the whole density field in 100 lines |
| `shaders/tornado.wgsl` | the GPU transliteration; the parity test keeps the two in step |
| `shaders/volume.wgsl` | the shared march, `mediumShape` / `mediumInterval` / `mediumBoundOf` / `mediumEmissionAt` / `mediumSelfShadow`. **Was owned by the fog agent; that branch has landed** (§10) |
| `src/world/medium_bound.cpp` | ADR-566's CPU twin of `mediumBoundOf`. Change the tornado's extent here **and** in the shader; `tests/unit/test_medium_bound.cpp` fails when only one moves |
| `src/world/world_effects/effects/tornado_effect.cpp` | the registration: 57 panel rows, JSON, modulation targets, presets |
| `examples/labs/tornado-modes-{a,b,c,d}-*.scene.json` | the structure-before-noise ladder; **this is the gate in §1** |
| `examples/labs/tornado-showcase.scene.json` + `_tc-1`..`_tc-7` | the seven variants; regenerate the arms with `tools/make_tornado_showcase.py` after editing the showcase |
| `~/Desktop/avgen-tornado-review/` | the five frames the owner has already seen, with the previous agent's own notes on what is wrong in each. Not in the repository, and the best five minutes you can spend |

## 9. Testing, briefly

Full rules in `docs/testing.md`. The three that catch the most:

- GPU work goes through `tools/gpu-lock.sh`. Several agents share one device; a result taken
  without the lock is not evidence.
- **Read the exit code the binary returned, never a pipeline's** — `cmd | tail` reports `tail`'s
  status, and `grep` is delighted to find nothing.
- A clean Catch2 run prints `All tests passed` and **no** `^test cases:` line; a crash prints no
  verdict line at all. Report the **skip count** too: a suite that skips half its cases looks
  identical to a clean one in every other field. The unit suite carries one
  **failed-as-expected** case by design — reconcile against the summary header, not against the
  word FAILED.

---

## 10. The `agent/fog` merge: the foundation moved under this branch

*(evidenced: this section was written by the agent that performed the merge, from the conflicts it
resolved. Commit and files cited inline.)*

**Read this before you read anything above it.** Sections 1-9 were written against
`agent/tornado` standing alone. While that document was being written, `agent/fog` rebuilt the
shared volumetric foundation **underneath** this branch and landed it on `main` — 33 commits,
ADR-563 to ADR-579. `agent/tornado` was then 33 commits behind and did not merge. This section
records what changed and, in particular, **one design collision that both branches answered
independently and that had to be settled at the merge.**

### What `agent/fog` moved

- `shaders/volume.wgsl` is no longer only the march. The per-slot bound was factored out of
  `mediumInterval` into **`mediumBoundOf`**, and it now has a **CPU twin**, `world::mediumBound`
  in `src/world/medium_bound.cpp`, with `tests/unit/test_medium_bound.cpp` asserting the bound
  actually contains the field's support (ADR-566). The tornado's bound arm was inlined in
  `mediumInterval` on this branch; **the merge moved it into `mediumBoundOf` and transliterated it
  into the C++ twin.** If you change the tornado's extent, you now change it in two places and a
  test will tell you when you have changed only one.
- `VolumeUniforms` gained `heightFog` (ADR-568) and `selfShadow` (ADR-570). The `sizeof`
  assertion in `src/rendering/volume_renderer.hpp` was `8 + 1` here and `10 + 1` there; the
  merged struct was recounted from its members rather than picked between.
- **§5.3's blocker is gone, and this is the single most actionable thing in this section.**
  "Self-shadowing — blocked; it needs a shared light march in `shaders/volume.wgsl` that does not
  exist; the fog agent owns that file" was true when §5.3 was written. **ADR-570 built exactly
  that**, `mediumSelfShadow` in `shaders/volume.wgsl`, and its own comment says it was written with
  this branch in mind: *"`agent/tornado` needs the same term and must not write a second one. This
  marches whatever each slot's kind says its density is."* It dispatches through `mediumShape` and
  clips to the same `mediumInterval` the primary march does, so the Tornado kind is carried by
  construction. `volumeShadowSteps` defaults to 0, which returns 1.0 from the first branch and
  leaves every existing frame bit-identical — so **nothing about the tornado's look has changed
  until someone turns it on.** §5.3 ranked this third and called it "a large part of why the hero
  column reads as glowing gas". It is now a slider, not a project. Turn it on and look — the
  slider is `scene/volumeShadowSteps`, capped at 16, and the panel still labels it **"Fog
  shadow steps"**, which is the only thing about it that is fog-specific.

  **One defect the merge itself created here, found and fixed while resolving it.** ADR-570 wrote
  `let extinctionPerShape = mediaLane(s, 1u).w;` — correct on `agent/fog`, where the only two kinds
  shared a lane layout. A tornado does not: lane 1 is its radius curve. That read would have handed
  the shadow march `radiusMidControl`, a number in the tens, as an extinction in the hundredths, and
  a tornado would have swallowed every light crossing it. This branch had already built the
  kind-aware accessor for precisely this (`mediumDensityCoeff`, whose comment names the same
  hazard); the merge routes ADR-570's new call site through it. **Neither branch could have caught
  this alone** — it needs the new shared reader and the new kind in one tree, which is what a merge
  is. It is also why the first thing to do with a shared reader after a merge is grep for
  `mediaLane(s, <n>u)` and ask which kinds it is true of.

### The collision: `lean` versus `pack(flow)`

Both branches independently answered **"how does a medium respond to the wind it subscribes to?"**,
and neither knew about the other:

| | what it built | where |
|---|---|---|
| `agent/fog` | changed the signature **every** kind implements: `pack(effect, envelope, const MediumFlowInput& flow, slot)` | ADR-572 §17 |
| `agent/tornado` | added a **separate per-kind hook beside** `pack`: `lean(effect&, downwind, influence)`, called by `buildAtmosphericFrame` on a copy of the effect | ADR-580 §68 |

Keeping both would have left **two channels for one question** with nothing downstream able to
disagree about which was authoritative — the defect family this repository has shipped repeatedly
(§7's own lesson, and ADR-576). ADR-441 forbids half-converted hooks while the engine is in heavy
development.

**How it was settled.** `pack(..., flow, ...)` is the single channel. **The `lean` function
pointer was deleted from `EffectResolve`**, and so was the lean stage in `buildAtmosphericFrame`.
Each kind's wind response is now **the body of its own packer**, which was being handed the flow
anyway.

**ADR-580 §68's insight is not lost — it is the thing that survived.** The argument that a kind's
answer to the wind is *per kind and the frame builder must not know it* was correct, and it won:
the frame builder no longer knows. What it did not need was a registry slot of its own.

- a cosmic vortex is a disc whose shape the march's coefficients were tuned against, so it answers
  by **moving** — `vortex_effect.cpp`'s packer, a tenth of the radius per unit influence;
- a fog bank is the same placed medium with a different authoring surface, so it answers the same
  way — `volumetric_fog_effect.cpp`'s packer. (That is separate from `driftWind`, which steers the
  structure *inside* the bank.)
- a tornado's axis is already a **curve** rather than a line, so it answers by **bending**, which
  is what a storm column visibly does and is free because the lean term is evaluated per sample
  whatever its value. That paragraph is now the comment on `packMedium` in `tornado_effect.cpp`.

The shared derivation — unit downwind direction, speed clamped to 1, times influence — is
**`world::flowLean`** in `atmospherics.hpp`, beside `flowAmplitude` and `flowOffset`, so "how much"
is answered once and only "in what units, for this shape" is per kind.

**What made the hook removable rather than merely redundant, and the condition that would reopen
this:** `buildAtmosphericFrame` called `lean` on a *local copy* of the effect whose only reader was
`packMediumSlot`, so nothing between the mutation and the pack could observe it. **A kind that
needs the effect mutated before some *other* stage reads it cannot be expressed as a packer** — and
that kind does not get a second hook here without an ADR saying why first.

Two consequences you can rely on:

- `effect_conformance`'s **`flow-reaches`** check is unchanged and still fires. It compares
  **packed frames**, not hooks, so it never knew which mechanism produced them — it was what
  caught the Tornado ignoring its own subscription in the first place, and it will catch it again.
- The bent column is still inside its own bound: `world::mediumBound`'s tornado arm adds
  `length(lane[7].xy)` to the cylinder radius, and the bend reaches `lane[7]` through the packer.

### Two smaller resolutions worth knowing

- `docs/testing.md` is now **"Thirty-two ways a green suite has lied"**. This branch's two new
  entries were renumbered to **31** and **32** rather than renumbering `main`'s 3-30, because other
  files cite those numbers (`tools/gpu-lock.sh` cites entry 25).
- The panel-guard row this branch added for `detailAmount` was dropped, as its own comment invited
  ("drop it if it conflicts"): `main`'s ADR-579 §36 fixed the same defect by declaring
  `.sec("Detail")` on the row explicitly.

### What the merge was proven against

All from the `av-gen-tornado` worktree, after `cmake --preset release` (**reconfigure, always** —
the test glob is configure-time and this merge brought seven new test files in; `docs/testing.md`
entry 1), and with the build's own exit code read separately from the suite's:

| | result |
|---|---|
| build | exit 0, **0 errors** |
| CPU suite `./build/release/tests/avgen_tests` | **exit 0** — 2848 cases, 2843 passed, **4 skipped**, 1 failed as expected |
| the one `FAILED` | `test_character_lab_slopes.cpp:187`, tagged `[!shouldfail]`, `with expansion: 7.168504715f < 1.0f`. Pre-existing and by design (ADR-260); it reconciles against the header's "1 failed as expected" and is not this merge's |
| the 4 skips | 2 × `AVGEN_SAMPLE_ASSETS is not set`, 2 × `ffmpeg is installed; the search falls back to it`. Environment, not GPU — **no case skipped for a context that would not create** |
| `[tornado]` under `tools/gpu-lock.sh` | **binary exit 0**, `All tests passed (2868 assertions in 5 test cases)`. All five **ran**; none skipped. This is the run that matters: a wind mechanism that silently lost would still render a perfectly plausible tornado |
| `[fog],[vortex],[volume]` under the lock | **binary exit 0**, 125374 assertions in 37 cases, 0 skipped — the other side of the shared foundation, unbroken |

The CPU suite was run **twice**: once after the build, and again after the `volume.wgsl` edit above,
because `ShaderLibrary` reads `.wgsl` from the source tree at run time and several CPU cases read
those files (`docs/testing.md` entry 25).
