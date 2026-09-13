# Phase G — certification

**What this is:** the evidence that says whether the renderer upgrade worked, and the instruments
that produce it. Written 2026-09-13 by the `cert` agent.

**What it is not:** a verdict on any single agent's change. Certification measures the renderer, not
the wave.

| | deliverable | where it lives | status |
|---|---|---|---|
| G1 | stress scenes at declared densities | `examples/**` `certification` blocks, `tools/certify.py` | built; five subjects match their declaration |
| G2 | scalability curves, cost against object and instance count | `tests/rendering/test_scalability_perf.cpp` | built; see §3 |
| G3 | frame pacing p50/p90/p95/p99 and the 1% low | `tools/certify.py --report` | built; see §2 |
| G4 | visual comparison on the canonical frames | `tools/certify.py --capture` | built |
| G5 | offline parity | `tests/rendering/test_phase_g_certification.cpp` | **two failures, both real** — ADR-146, ADR-147 |
| G6 | frame-state baselines green | `examples/qa/baselines/*.snapshot.json` | green, 29 assertions, unchanged |

---

## 0. The rule this phase is built around

ADR-131 is the reason every instrument here refuses before it reports. A single-run sweep found a
52% difference, was written up as a decisive minimum, and three repeats dissolved every interval into
every other. §1.1 of the audit is the same failure in the other direction: Glowmere's 1% spread was
applied to Constellation, whose own is 37%, and a headline had to be withdrawn.

So: **a curve without a noise floor is decoration**, and ADR-144 makes that structural rather than
advisory. Every sweep in this phase keeps its raw repeats, derives its floor from the arms' own
spread, runs a null arm against itself, and says "NOT A RESULT" when it cannot resolve what it was
asked. Several of the tables below say exactly that. That is the instrument working.

The counters are a different matter and are treated differently. A composition is a pure function of
`(recipe, library, seed)` and the culling decision is a pure function of that plus the camera and
the resolution, so a draw count or an instance count is **exact**. Those are checked against a
declaration and a difference is a finding with no noise floor to argue about (ADR-145).

---

## 1. G1 — the stress scenes, and what they declare

Five subjects, each carrying a `certification` block inside its own scene file. `tools/certify.py`
renders each and compares.

Four are the existing Glowmere density ladder, which was measured in
[renderer-2-benchmark-world.md](../renderer-2-benchmark-world.md) §6 and documented there; the block
turns that documentation into a check. The fifth is new:
`examples/stress/open-vista.recipe.json`, the Open Vista profile
[05-scope-contract.md](05-scope-contract.md) §45 schedules and the ladder does not cover — long
sightlines, mountainous relief over 1.4 km, thin cover, composition weight in the background band.

§45's other two named profiles, Character and the AV Gen Showcase, are **not** built and are not
claimed.

### The measured ladder

Revision `ccb5d5c`, 1280×800, `--tier realtime`, 180 frames at 30 Hz, three interleaved repeats
under `tools/gpu-lock.sh`, zero GPU errors on every run. Counters identical across all three repeats
for every subject. Timings are this session's and are not comparable with any other document.

| subject | draws | shadow draws | entities | visible inst. | culled inst. | submitted tris | logical tris | scene pass | GPU p50 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `glowmere-low` | 70 | 49 | 56 | 449 | 7,484 | 84,400 | 1,006,793 | 7.86 | 13.43 |
| `glowmere-medium` | 87 | 49 | 56 | 2,827 | 46,278 | 292,114 | 6,182,569 | 9.76 | 13.43 |
| `glowmere-dense` | 92 | 53 | 68 | 2,844 | 150,932 | 545,844 | 22,094,662 | 12.91 | 17.04 |
| `glowmere-extreme` | 291 | 42 | 256 | 1,145 | 171,452 | 1,002,666 | 28,769,011 | 14.61 | 19.66 |
| `open-vista` | 125 | 51 | 104 | 304 | 20,857 | 277,661 | 5,383,538 | 10.75 | 15.99 |

Milliseconds. `scene pass` and `GPU p50` are the median of the three repeats' medians.

### Three things this table says

**The world grows 28.6× and the scene pass grows 1.86×.** Logical triangles — what the world
contains, before LOD and culling — go from 1.01 M at `glowmere-low` to 28.8 M at `glowmere-extreme`.
The scene pass goes from 7.86 ms to 14.61 ms. Culled instances go from 7,484 to 171,452, a factor of
22.9, for that same 1.86×. **This is the upgrade's headline property, measured at the world level:
cost tracks what is drawn, not what exists.** It is one session's numbers and the confound is
obvious — the rungs differ in more than one variable at once — which is why §3 asks the same
question with one variable at a time.

**The ladder is no longer monotonic in visible instances.** `glowmere-medium` has 2,827,
`glowmere-dense` 2,844, `glowmere-extreme` **1,145**. The benchmark-world document anticipated the
mechanism at the top rung (§1: the composer's 120,000-instance ceiling for a foreground layer, and a
viewpoint that lands on different ground as the extent grows) but recorded 3,111 visible at
`extreme`. Whatever else is true, the rung labelled "extreme" is now the *lightest* of the three
upper rungs in scatter and the heaviest in entities — 256 against 68 — so it is a different kind of
load than its name suggests, not more of the same one.

`entities = 256` at `glowmere-extreme` is a suspicious number, because 256 is `kInitialObjects` and
was the old hard cap. **It is not the cap.** The submission loop logs "more than N visible entities;
extra entities skipped" when it truncates, and a full run of that scene logs nothing of the kind.
Checked rather than assumed, because a coincidence at exactly the old limit is worth two minutes.

**`glowmere-low` and `glowmere-medium` are indistinguishable at 13.43 ms.** The bottom of the ladder
does not resolve. This is consistent with benchmark-world §6, which found `glowmere-low` moving 27%
between two passes and concluded that only `dense` and `extreme` are rungs a regression should be
judged on. It remains true, and now for a second reason: whatever the light rungs are measuring, most
of it is not the geometry the preset varies.

---

## 2. G3 — frame pacing

From the same run. Each cell is the median of the three repeats' values for that statistic, with the
peak-to-peak spread of the repeats beside it. **The 1% low is the mean of the slowest 1% of frames
and is not p99** (ADR-113); both are reported because the gap between them is the shape of the tail.

| subject | min | p50 | p90 | p99 | 1% low | max |
|---|---:|---:|---:|---:|---:|---:|
| `glowmere-low` | 9.83 | 13.43 | — | 19.99 | 20.28 | — |
| `glowmere-medium` | 12.52 | 13.43 | — | 20.77 | 22.68 | — |
| `glowmere-dense` | 15.86 | 17.04 | 18.68 | 23.46 | 24.48 | 25.69 |
| `glowmere-extreme` | 16.78 | 19.66 | — | 26.28 | 26.97 | — |
| `open-vista` | 9.96 | 15.99 | — | 24.97 | 25.72 | — |

GPU milliseconds. The full per-subject tables, on both clocks, are what `tools/certify.py --report`
writes.

**Every subject has a tail worth about half its median again.** `glowmere-medium`'s 1% low is
22.68 ms against a 13.43 ms p50 — a factor of 1.69. `open-vista`'s is 2.6× its minimum. A frame
budget set from the median is met by these scenes and a frame budget set from the 1% low is not, and
the two answers differ by more than any optimisation in this upgrade has moved.

**The tail statistics are the noisy ones.** On `glowmere-dense` the p50 reproduced across repeats to
8.1% while p90 moved 30.5% and p95 27.6%. The wall clock is worse: its 1% low moved 100.5% and its
max 149.6% across three repeats of the same scene. So the tail is real and its *size* is not yet a
measurable quantity on a shared machine — which is a statement about the machine and about how many
repeats a tail needs, not a reason to quote the median instead.

---

## 3. G2 — the scalability curves

The ladder in §1 varies several things at once. These vary one.

The fixture is a ring of objects around the camera at a fixed radius, so every object is the same
size on screen and the only thing that changes is how many the frustum contains. 384 are held in a
±20° wedge in front of the camera at every arm; the rest are placed on the arc **behind** it. Five
repeats per arm, interleaved round-robin, a null arm in every table, 1280×800.

Both submission paths are measured, because they do not cull alike:

- **entities** — `SceneRenderer` performs no camera-frustum test of its own. `Entity::cameraCulled`
  is an *input*, written per frame by `scene::Composition` (`composition.cpp:1606`) and by terrain.
  A scene handed straight to the renderer submits everything in it. Both arms are measured: the flag
  left alone, and the flag written by the same `world::aabbVisible` the composition uses.
- **procedural instances** — culled on the GPU by `shaders/cull.wgsl`, but only when
  `LodSettings::cull` is set, which defaults to false. The same population with one boolean changed
  is a paired answer in one session.

### 3.1 What the counters say, which is the part that is settled

Deterministic, exact, no noise floor. Submitted triangles at each arm, all four curves:

| population | entities, no cull | entities, culled | instances, `cull=false` | instances, `cull=true` |
|---:|---:|---:|---:|---:|
| 384 | 4,608 | 4,608 | 4,608 | 4,608 |
| 640 | 7,680 | 4,608 | 7,680 | 4,608 |
| 1,408 | 16,896 | 4,608 | 16,896 | 4,608 |
| 4,480 | 53,760 | 4,608 | 53,760 | 4,608 |
| 8,576 / 16,768 | 102,912 | 4,608 | 201,216 | 4,608 |
| 65,920 | — | — | 791,040 | 4,608 |

**With culling, the work submitted is a function of what is visible and of nothing else.** 384
visible instances out of 65,920 submit exactly the same 4,608 triangles as 384 out of 384 — a 172×
change in what exists for a 0% change in what is drawn. The entity path does the same when the
composition's flag is applied, and does the opposite when it is not: 8,577 draw calls and 102,912
triangles for 384 visible objects.

The visibility sweep is the control, and it moves the other way as it must: one fixed population of
8,192 instances spread across a widening arc submits 98,304 triangles when the whole population is
in frame and 22,704 when only 1,892 of it is, with `visible + culled` summing to 8,192 at every arm.

**This is the upgrade's headline property, and on the counters it holds exactly.** The qualification
that matters is in the entity row: it holds *because something culls*, and on the entity path the
thing that culls is the composition, not the renderer. A caller that builds a `scene::Scene` and
hands it to `SceneRenderer` gets no frustum test at all. That is the documented design — the flag's
comment says so — and it is worth knowing that the renderer's scalability is a property of the
composition layer rather than of the renderer.

### 3.2 What the timings say, which is much less

**One step in five curves clears its floor.** Everything else is inside the noise, and the null arms
are inside the noise of themselves — up to 67%.

| curve | ends | floor | verdict |
|---|---:|---:|---|
| entities, no cull | −26.7% | 60.6% | not a result |
| entities, culled | +0.0% | 48.9% | not a result |
| instances, `cull=false` | −26.9% | 67.3% | not a result; **but** +71.8% at N=65,920 clears it |
| instances, `cull=true` | +0.0% | 72.0% | not a result |
| visibility, arc 70→360° | +3.6% | 86.6% | not a result |

The one resolvable step is the right one: the unculled instance curve's jump from 16,768 to 65,920
instances, where submitted triangles go from 201,216 to 791,040. Cost that tracks submitted work is
what the counters already said; the timing agrees where it can see.

There is one further comparison worth stating and worth qualifying. At N=65,920 the unculled arm's
scene pass is 7.995 ms and the culled arm's 3.539 ms — **+126%**, which clears both curves' floors.
It is a weaker result than that number looks, because the two arms are two blocks rather than an
interleaved pair: the test runs all of `cull=false` and then all of `cull=true`, so a drift between
them is charged to the change. §3.1 of the audit is explicit that a blocked comparison is not the
protocol. Quoted as suggestive; the counters are what the conclusion rests on.

### 3.3 Why the floors are so wide, and what would narrow them

This machine is shared between agents. The first full run of these curves was taken at a load
average of 62 and reported per-arm spreads of 85%, 130% and 172%; the run tabulated above was taken
at 20 and reports 49% to 87%. `tools/gpu-lock.sh` serialises everything that goes through it and
cannot see a process that does not.

So the honest statement of G2 is two-part and should not be collapsed into one:

- **On deterministic counters, the property holds exactly and is settled.**
- **On timings, this instrument on this machine cannot currently resolve it**, and it says so rather
  than reporting the number it happened to get. Re-running `[.perf][scalability]` on a quiet machine
  is a ten-minute job and the tables above are the format the answer will arrive in.

### 3.4 One fixture bug worth recording

The visibility sweep initially reported an unculled 8,192 visible at every arc. The cause was not the
renderer: `ProceduralRenderer` re-uploads an instance buffer only when `structureVersion` or the
instance *count* changes (`procedural_renderer.cpp:1473`), and a visibility sweep holds the count
fixed and moves the instances — so every arm rendered the first arm's positions. The engine
behaviour is the documented contract; the fixture was wrong, and `structureVersion` is now derived
from each arm's own parameters so it cannot be forgotten. It is recorded here because the symptom —
a perfectly flat curve — is indistinguishable from the result the sweep was hoping for.

---

## 4. G4 — the visual gate

`tools/certify.py --capture DIR` writes one frame per subject at a fixed second and, when DIR
already holds a capture, prints the pixel difference against it.

It compares against **the previous run**, not against a committed reference, and that is a decision.
A renderer upgrade changes pixels on purpose; an image baseline committed at the start of one is
discarded on its first day and then teaches everybody to ignore the check. That is the same argument
`examples/qa/baselines` makes for recording derived *state* rather than pixels, and it applies with
more force to a picture. What a run-to-run diff answers is the question actually worth asking during
an upgrade: this change — did it move the image, and where.

§50 then requires a person to look. The tool never says a frame is correct; it says whether it
moved.

**Both subjects captured twice, in two separate processes, are byte-identical.** That is worth
stating on its own: the capture is reproducible across process boundaries, so a non-zero diff from
this tool is a change and never a re-roll.

**And they were looked at.** `glowmere-medium` is the dense meadow it is supposed to be — ground
cover to the horizon, the pale hero tree at mid-depth, the canopy closing at the frame edges.

`open-vista` is a genuinely different shot: sparse ground running out to a distant ridge, a lot of
sky, wide negative space. It is the profile in the sense that matters for the renderer. It is **not**
the profile in the sense §45 has in mind, and the recipe now says so rather than claiming it: the
composer still places a foreground hero, so "almost everything in frame is far away and small" is
false of the frame. The counters say the same thing from the other side — 304 visible instances
carrying 277,661 submitted triangles is 914 triangles each, against `glowmere-medium`'s 103. A real
distant-representation torture case wants the opposite ratio, and reaching it is a composition
problem rather than a recipe one.

---

## 5. G5 — offline parity: two failures, both real

The determinism contract §41 depends on is intact and is not re-litigated here. `[determinism]`
passes: ten cases, 297 assertions, including that an offline job's frame is byte-identical to the
interactive path's for the same second, that a second reached by different routes replays
identically, and that Glowmere replays after a seek away and back.

What the upgrade puts at risk is *representation*, and there `tests/rendering/test_phase_g_certification.cpp`
finds two things, both left failing on purpose:

**[ADR-147] A batch render is rendered at the realtime tier.** `SceneRenderer::setQuality` has one
caller, on the interactive renderer; `app::RenderJob` builds a renderer of its own and
`RenderSettings` has no tier field. Measured: one frame of the RendererQA scene through the real
batch path is byte-identical to the interactive path at Realtime (0 of 576,000 bytes differ) and
differs from Offline (395 bytes, worst channel delta 116). The premise — that the two tiers differ at
all on that scene — is asserted first, so a null could not be read as a pass.

**[ADR-146] Offline does not remove the GPU ladder's hysteresis.** `RepresentationPolicy::forTier`
handles the CPU selector correctly. `LodSettings::lodHysteresis` — the instance ladder's dead zone —
reaches `cull.wgsl` without the tier being consulted. Measured at `QualityTier::Offline` with
hysteresis 0.3, the same camera position settled for twelve frames: 30 of 582 visible instances land
on a different rung depending on whether the camera approached from far or near. The control, the
same experiment at hysteresis 0, agrees exactly.

Neither is fixed here. Both are outside this agent's files, both are decisions rather than lines,
and **ADR-147 must be fixed first** — until the batch path asks for the offline tier, fixing ADR-146
buys a batch render nothing.

---

## 6. G6 — the frame-state baselines

Green. `[baseline]`, 29 assertions, three canonical scenes compared against their committed
snapshots, unchanged. Nothing in this phase regenerated them and nothing needed to.

---

## 7. What was found and not fixed

| | where | why not here |
|---|---|---|
| A batch render runs at the realtime tier | `app/render_job.cpp`, `app/render_settings.*` | ADR-147; not this agent's files, and a default that changes every existing render should be chosen deliberately |
| Offline does not zero `lodHysteresis` | `src/rendering/lod*`, `procedural_renderer.cpp` | ADR-146; `repr`'s files, and the fix is one of three shapes |
| `WorldRecipe::toJson` drops the `certification` block | `src/world/world_recipe.cpp` | a recipe round-tripped through the world editor loses its declaration silently; belongs with the serialiser's owner, and the failure mode is visible in `git status` |
| The density ladder is not monotonic in visible instances | `examples/recipes/glowmere-*.recipe.json` | §1; the rung named "extreme" is the lightest of the three upper rungs in scatter. Recorded, not re-cut — changing the ladder in the same change that measured it would leave nothing to check the measurement against |
| The light rungs do not resolve | same | `low` and `medium` are both 13.43 ms. benchmark-world §6 already said to judge regressions on `dense` and `extreme`; this is a second reason |

## 8. Running it

```sh
tools/certify.py                                        # the five subjects, 3 interleaved repeats
tools/certify.py --report cert.md --capture shots/      # the report and the visual gate
tools/certify.py --record                               # adopt new counters; review the diff in git
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[.perf][scalability]"
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[certification]"
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[baseline]"
```
