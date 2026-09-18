# Autonomous Character Intelligence: implementation plan

Companion to `docs/character-ai-research.md`. Reads against ADR-266 to ADR-270 and
`src/entity/character_ai.hpp`.

**A caveat about the mapping.** I do not have the verbatim text of the brief's §46 phase list —
it is not in the repository and was summarised to me second-hand. The units below are derived from
*capability* and *dependency*, and each says which of the brief's named concerns it serves. Where
the brief's own phase numbering differs, the dependency order is the part that matters and is the
part I would not reorder.

---

## 0. The critical path, in one line

```
P0 research (done) -> P1 determinism -> P2 perception -> P3 decision
```

Everything else hangs off that spine and can run beside it. **P1 is the only unit that must
complete before anything else is correct**, because every measurement a later phase takes is a
measurement of a simulation whose answer depends on the frame rate and on where the camera was.

At four concurrent agents the wall-clock shape is three waves, and the spine is the long pole in
all three.

---

## 1. File ownership

This is the part that stops eight agents colliding. A unit owns a file outright or it does not
touch it. Where two units need the same file, the table says which one edits it and what the other
must do instead.

| file | owner | note |
|---|---|---|
| `src/entity/entity.cpp` — `update()`, `seek()` | **P1 only** | the contended file. P2 and P3 add their stage through a one-line call site P1 lands for them |
| `src/entity/entity.cpp` — `socketTransform()` | **P5** | a different function, 20 lines, no overlap with P1's |
| `src/entity/entity.hpp` | P1 first, then P2, then P3 | serialised. Each adds fields; none reorders |
| `src/entity/perception.{hpp,cpp}` | **P2** | new |
| `src/entity/decision.{hpp,cpp}` | **P3** | new |
| `src/entity/behaviors.cpp` — `Explore` | **P3** | the goal-model extraction |
| `src/entity/character_ai.hpp` | **nobody**, after P0 | changes to it are an ADR, not an edit |
| `src/entity/navigation.{hpp,cpp}`, `nav_grid.*`, `obstacles.*` | **P7** | |
| `src/scene/animation.*`, `src/scene/skeleton.*` | **P5**, then **P6** | serialised |
| `src/scene/composition.cpp` — `updateBehaviour` | **P1** | |
| `src/scene/composition.cpp` — `AnimationSink`, `updateCharacters` | **P5/P6** | different functions |
| `src/app/engine.cpp` — `seekSeconds` | **P1**, then **P8** | serialised |
| `src/labs/*` | **P4** | globbed; no CMake edit |
| `examples/labs/character-ai/` | **P4** | |
| `tests/unit/test_entity_determinism.cpp` | **P1** | new |
| `tests/unit/test_character_ai_lab.cpp` | **P4** | new |
| `src/ai/engine_tools.cpp` | **P10** | 3,338 lines; one agent at a time, always |
| `tools/charai_probe.cpp` | whoever is measuring | delete when the plan it priced is built |

---

## 2. The units

### P1 — Determinism foundation  ·  critical path  ·  one agent  ·  blocks P2, P3, P8

**Delivers** (ADR-267 §4):

1. A fixed simulation step with an accumulator. `EntityWorld` advances in whole 1/60 steps; the
   frame rate decides how many, never how big. ~60 lines in `Composition::updateBehaviour` and
   `EntityWorld::update`.
2. Level of detail selects *which stages run* — perception cadence, decision cadence, replan
   suppression — and never the integration step. This is the change that retires 50.263 m of
   divergence and it is the one that will be argued about, because it makes a distant body cost
   more than it does today. The honest framing: it costs the *integrator*, which is cheap, and
   saves the *mind*, which is not.
3. The four seek defects: `BehaviorContext::self` left at 0; the crowd field never rebuilt; the
   action queue and schedule not re-simulated at all; `viewPosition` read inside the seek loop.
4. **The test that did not exist**: `play(t)` versus `seek(t)` on a real scene, with a control arm
   at a different frame rate that must disagree. There is currently no test in this repository that
   asserts scrub equals play for an entity, and given the variable-versus-fixed step none could have
   passed.

**Done when** `play(jittered 45–90 Hz, 30 s)` equals `seek(30 s)` to under a millimetre on
`glowmere-valley-2`, with the differing-frame-rate control still disagreeing.

**Risk.** Changing the integration cadence changes every existing scene's exact character
positions. That is expected and must be stated in the commit, not discovered: this is a
behaviour-changing fix and the old positions were the ones that depended on the frame rate.

### P2 — Perception  ·  critical path  ·  one agent  ·  needs P1

**Delivers** `IPerception` (ADR-270): grid-backed candidate scan, `PerceptionSettings` registered as
parameters under `entity/<name>/perception/`, a `perception` key on `EntityDesc`, a scripted
implementation for tests, and the round-robin occlusion budget with `Percept::tested` reported
honestly.

**The one number this unit was told to measure first has been measured.** The `spatial::PointGrid`
radius query is 0.098 µs at 60 m over the real 505 interest points, against a 2–4 µs estimate — so
the scan is effectively free and the 4 Hz cadence is **not** a budget. Keep it anyway, for the
reasons ADR-270 §4 now gives: instantaneous reaction reads as a machine, and `Percept::seenAt` is
meaningless if it is always now. The budget argument applies only to occlusion, where it applies by
four orders of magnitude.

**Done when** two characters with different `PerceptionSettings::range` in the same scene demonstrably
notice different things, and the occlusion budget is provably respected under a crowd — with a
control arm at `occlusionTestsPerSecond = 0` that must report `tested == false` everywhere.

### P3 — Decision  ·  critical path  ·  one agent  ·  needs P2

**Delivers** `IConsiderer`, `Option`, a selector with dwell and margin, three or four stock
considerers, and the extraction of `Explore`'s goal model into one of them.

**The extraction is the delicate half and it needs a byte-identical control.** `Explore` is the
only autonomous mind in the engine and five Glowmere characters depend on it. The refactor is done
right when the same seed produces the same route to the millimetre before and after — the arm that
proves it is a before/after position trace, not a test that both versions "still walk".

**Done when** a character that is not an explorer exists: a guard that holds a post and abandons it
when it perceives something, expressed entirely as considerers and scene data, with no new C++ class
per character kind.

### P4 — Character Intelligence Lab  ·  parallel from day one  ·  one agent

**Delivers**, per ADR-261 and with zero engine code and zero CMake edits: a `LabId` enumerator, a
descriptor in `src/labs/lab.cpp:30` in enum order, an overlay profile case, a fixture,
`examples/labs/character-ai/cases.json`, tests, and the count bump at
`tests/unit/test_lab_registry.cpp:149`.

**Also fixes two registry drifts** found in Phase 0, which the registry test cannot catch because
both targets exist: the `animation` and `character` labs' `doc` fields point at
`docs/engineering-labs.md` rather than `docs/character-animation-lab.md`, and their `fixture` fields
name `examples/characters/alien{,-wander}.json` rather than the lab's actual
`examples/lab/character-animation-lab.json`.

**The overlay this lab owes** is the one thing nobody can currently see: the losing option scores
beside the winner. `NavDebug` already carries route, leg, destination, status, phase, goal name and
goal kind through `IBehavior::navDebug` and this lab is where they get drawn.

**Every lab case must be able to name a project, not only a scene** (ADR-264). The multicam project
carries 5,489 parameter overrides and every test in this repository stops at the scene.

### P5 — Skeleton query and real sockets  ·  parallel from day one  ·  one agent

**Delivers** A1 and A2 from the research document: `ISkeletonQuery` implemented over
`scene::SkinnedRig`, `Entity::setSkeleton` actually called, and `socketTransform` reporting its
fallback instead of returning `true` on it.

**The smallest change with the largest reach in this whole plan.** It is the only thing between the
engine and every socket, attachment, carried prop and aim. Today every socket silently returns the
entity frame; ADR-262 is the ADR about what that costs when a beam is 28.661 m from the body it is
lifting.

**Done when** a prop attached to a hand socket moves with the hand, measured as a position trace
against `jointPositionAt` in `tests/support/stride_speed.hpp` — which is a working model-space joint
query that already exists and is test-only.

### P6 — Animation layer stack  ·  needs P5  ·  one agent

**Delivers** A3: two slots with a joint mask, so `LocomotionState::reaction` and `lookTarget` stop
being published into nothing. Four fields of the animation-intent seam are currently written every
frame and read by nobody.

**Prerequisite, not enhancement.** Everything expressive the brief asks for — a head turn, a
flinch, an aim — needs somewhere to be put, and a single cross-fade between two clip slots is not it.

### P7 — Navigation performance and honesty  ·  parallel from day one  ·  one agent

**Delivers**, in value order:

1. **Steer and validate against the grid rather than the analytic world.** `steer` is 59.135 µs and
   `pathValid` is 74.481 µs — three times the cost of planning a fresh route — because both sample
   the analytic world where the grid holds the same answer at 0.024 µs. This is what decides whether
   a cast can be 100 or 500, and it is the single largest optimisation in this document.
2. **Reachability reporting.** 28 regions; the largest holds 9,938 of 19,584 walkable cells. Half
   the walkable ground of the shipped world is not reachable from the other half and nothing says so.
   `NavGridStats::regions` is computed at build and read by no UI; `PathStatus::Unreachable` exists
   and is surfaced nowhere.
3. **Dynamic obstacles** — a partial grid rebuild over an XZ rect, so a door that closes or a craft
   that lands blocks a route.

**Done when** the `explore` scaling arm of `tools/charai_probe.cpp` moves. It is the instrument; it
is already built; it has controls.

### P8 — Scrub latency  ·  needs P1  ·  one agent

**Delivers**, in this order and stopping as soon as the editor is usable:

1. **`maxSeconds = 90.0` becomes a budget in entity-steps.** It is a literal in `app/engine.cpp` and
   it is the wrong unit: a 23-body scene should keep its 90 s of history and a 250-body scene should
   keep what it can afford. One line of policy where there is one line of constant.
2. **Move the re-simulation off the main thread.** It is 99.999% of a seek, it runs synchronously
   inside `SequencePanel::draw` draining no input, and one drag gesture issues 52 seeks of which 51
   are obsolete.
3. **Checkpointing, only if 1 and 2 are not enough.** ADR-267 §5: the replay is already exact, so
   this is latency work, not correctness work, and it is the most expensive of the three.

Why it matters, measured rather than extrapolated (the extrapolation was low by 2–5×, see ADR-267
§5): one `EntityWorld::seek(90 s)` costs **9.10 s at 10 `explore` characters, 64.63 s at 50,
161.51 s at 100, 594.66 s at 250**. The frame cost at 100 is 9 ms. **Two hundred and fifty
explorers is a ten-minute stall for one timeline click.** The scrub, not the frame, is what caps
the cast size in an editor.

### P9 — Root motion  ·  needs P5 and P6  ·  one agent

**Delivers** A4: extraction behind a per-clip opt-in, writing `MotionAuthority::Simulation`.
ADR-161 decided root motion was not implemented because the content had none; the repository's own
inventory test now requires that travelling clips exist, and `Landing`'s **−0.567 m** is discarded
every time Glowmere plays it. The opt-in is what keeps the other 163 clips exactly as they are.

### P10 — Staging and character tools for the control plane  ·  independent  ·  lowest priority

`src/ai/` has 58 tools and **none of them can reach `stage::Staging`**: `grep -rn "staging|director"`
over the whole module returns only the word "directory". A scenario's *numbers* are reachable today
because `staging/<scenario>/<name>` are ordinary parameters; its *structure and control* are not.
The API a tool would call is complete (`start`, `stop`, `trigger`, `running`, `cycles`, `beat`,
`binding`, `parameter`, `setParameter`, `writersOf`). This is a clean seam and it is not on the
critical path.

---

## 3. Waves, for four concurrent agents

**Wave 1** — P1, P4, P5, P7.
P1 is alone on `entity.cpp:update/seek` and `composition.cpp:updateBehaviour`. P5 touches
`entity.cpp:socketTransform`, a different function. P4 touches only `src/labs/` and `examples/`.
P7 touches only `src/entity/navigation*` and `nav_grid*`. **No two of these edit the same function.**

**Wave 2** — P2, P6, P8, and P7 continuing.
P2 needs P1's call site. P6 needs P5's skeleton query. P8 needs P1's fixed step before it changes
how much re-simulation happens.

**Wave 3** — P3, P9, P10.
P3 needs P2. P9 needs P6. P10 needs nothing and is the one to drop if time runs out.

**Merge order within a wave:** P7, then P5, then P4, then P1. P1 last because it is the one whose
diff touches the file everything else is near, and a conflict resolved *into* P1 is resolved by the
agent who understands the update order.

---

## 4. What every unit owes, regardless of which one it is

* **A control that can fail.** Two arms of the Phase 0 probe could not, and both are recorded in
  ADR-267 §2. An arm that agrees because it never ran is the same lie as one that cannot fail.
* **Minima, not means, and the load average stated** (ADR-170). Every timing in
  `docs/character-ai-research.md` was taken between load 3.75 and 4.85 and says so.
* **No wall clock, no frame index, no global randomness.** ADR-267's D1.
* **Seed-and-index hashes, never PRNG streams, for anything a decision depends on.** D2.
* **A named position.** Which of ADR-260's three a change reads and which it writes, in the comment,
  every time. Four defects in this repository have had exactly that shape.
* **A statement of what it changed that nothing tested.** P1 will move every character in every
  existing scene. That belongs in the commit message, not in a bug report a week later.
