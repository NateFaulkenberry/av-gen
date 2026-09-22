# Autonomous character architecture — Phase D

Phase D (`docs/design/specs/phase-d.md`) asks what a character should *do*. This document is §1's
audit, §72's architecture deliverable and the phase log. ADR-670 records the one real decision.

**Status (2026-09-21, branch `agent/anim-phase-d`):** the Success Demonstration's pipeline runs end
to end on `examples/labs/character/autonomy-demo.scene.json`, with nothing authored for either
alien. See "The demonstration" below and `tests/unit/test_phase_d_autonomy.cpp`.

## The stack, and who owns each box (§78)

| §78 box | owner | status |
|---|---|---|
| WORLD STATE | `EntityWorld` (entities, interest points, `NavGrid`) + **semantic tags** (`SemanticTags`, §25) | tags new |
| WORLD EVENTS | **`EntityWorld::worldEvents`**: any named `ActionEvent` becomes one, on both paths (§26) | new |
| PERCEPTION | `GridPerception` (ADR-270/290) + **per-tag salience** (`perception.tags`, §9) | extended |
| ATTENTION | **`AttentionModel`** (mind.hpp) scores; Phase B `chooseAttention` holds (§10, §11) | new |
| MEMORY / NOVELTY | **`ObjectMemory`** (mind.hpp) + `PerceptMemory` (ADR-333) + `visited` (§21, §22) | new |
| GOALS / BEHAVIOURS | `decide`: `Selector` + considerers (ADR-269/333) + **`react`, `social`**, **personality** (§24, §27, §29) | extended |
| CHARACTER INTENT | `CharacterIntent`, **now produced**: WHAT by `decide`, HOW by `Move`/`Face` (§3, §15) | producer new |
| NAVIGATION | `NavGrid` A*, `NavigatorPath`, `RouteConsiderer` (ADR-193, 335) | existed |
| STEERING | `Navigator::steer`, crowd separation, **arrive radius** on `Move` (§14) | extended |
| MOTION REQUEST | `Entity::advanceMotion` → `MotionChain` (Phase B/C); dark in shipping scenes (ADR-615) | existed |

The WHAT / HOW / BODY boundary: nothing above CHARACTER INTENT names a clip or writes a transform
(R3, R4 in `character_ai.hpp`). Considerers score; the queue acts; the body is Phase A–C's.

## §1 audit: the ten rows the board listed as done, checked against D's text

Each was a pre-existing system claimed to meet D and never read against D. Verdicts:

| § | claim | verdict | what was missing, and what was done |
|---|---|---|---|
| 1 | inspect the existing system | **partial** | The previous version of this file existed but claimed "nine of eleven rows exist", described `CharacterIntent` as consumed by a behaviour (it had no producer, ADR-615) and named no ADR. Rewritten; ADR-670. |
| 5 | utility / priority selection | **partial** | Utility, dwell, margin: yes. Personality: no (added). Target validity: no (added: subject identity, hold-while-known, lifecycle). Cooldown: no (added as memory suppression and novelty). Priority/urgency as separate axes: **deliberately not**; ADR-269's one-axis rule kept, and `urgency` now rides on the option into the intent. |
| 6 | data-driven behaviours | **met** | Considerers are scene JSON. The new ones (`react`, `social`, `traits`, `tags`, `mind`) are too. |
| 7 | world query / perception | **partial** | Grid-backed radius/cone queries existed. **Semantic filtering and "recent world events" did not.** Added (§25 tags, §26 events). |
| 8 | spatial awareness | **partial** | `PointGrid`, 0.098 µs a query (ADR-270). **Not benchmarked at 100/500/1000 characters** as §8 asks. Open, with §44. |
| 9 | character perception | **partial** | Vision, proximity: yes. **Hearing / world events: nothing.** Target categories: per kind only. Added: event hearing with range and sensitivity, and per-tag salience. Found on the way: a 16-percept capacity cut dropped the saucer in favour of shore points (measured, fixed by tag salience). |
| 12 | navigation | **met, with a gap** | A*, obstacles, agent radius, unreachable. Dynamic obstacles only as crowd separation. |
| 13 | navigation architecture | **met** | Goal → destination → path → steer → request, with the motion system blind to the path. |
| 19 | behaviour lifecycle | **not met** | Per-action `Completed/Failed/Cancelled` existed, but **the decider never learned how its errand ended**. A finished character stood until a margin happened to clear, and an unreachable target could be chosen again at once. Built: drain serials, completion/failure memory, forget-and-choose, hold. |
| 33 | determinism | **claimed, partly false** | D1–D4 hold for the code as written, but **`seek` did not reproduce a play for a decider**: it skipped the t = 0 instant and never measured velocity. Both fixed and teeth-checked (ADR-670). |

## What was built

- **`src/entity/mind.{hpp,cpp}`**: `SubjectId`, `SemanticTags`, `WorldEvent`/`PerceivedEvent`,
  `Personality` + `TraitWeights`, `ObjectMemory` (bounded, habituation that decays, quadratic
  recovery after investigation, failure suppression, eviction that never drops a hot entry),
  `AttentionModel` (additive named factors, Phase B hysteresis), `MindView`.
- **`decide`'s `"mind"` block**: records what it perceives, hears events (strictly-earlier rule),
  attends, glances (publishes the look target when nothing else did), habituates only while still,
  holds a running plan while its subject is known, observes the plan's end, remembers on arrival,
  publishes the WHAT of the intent, and keeps a bounded decision history.
- **`react`** (approach vs flee, split by curiosity/caution) and **`social`** (greet vs avoid, with a
  personal-space band so the two cannot alternate). **`investigate`** gains `tags`, `intent`,
  novelty, subject identity, tracking of moving bodies (EntityRef), and an arrival radius.
- **`Selector`** tracks the committed option's subject and can hold.
- **`ActionQueue`** reports drained lists; **`Move`** publishes vector intent and honours `arrival`.
- **Scene data**: `personality` (entity), `perception.tags` (entity), `worldEvents` (scene),
  `orbit.authority: "simulation"` (a craft that actually goes somewhere). All round-trip.
- **Diagnostics**: `explainCharacter` (§41's report), `BehaviorTraceRecorder` (§64/§65), and
  `avgen_behavior_trace` (§58/§64): headless simulate-and-trace with `--repeat`, `--seek`,
  `--explain` and `--options`.

## The demonstration

`build/release/tools/avgen_behavior_trace examples/labs/character/autonomy-demo.scene.json
--seconds 180`. The scout's lines, abridged, as this commit's build produces them:

```
  0.00  (start) -> rock-1                       wander
 10.23  rock-1 -> investigate [mushroom-1]      attending: mushroom-1 (semantic)
 33.53  investigate -> company/greet [warden]   previous-plan=completed
 60.23  ... -> startle/approach [crack]         curious; the warden takes startle/flee
 73.23  -> investigate [mushroom-2]
 95.23  -> startle/approach [bloom]
111.10  -> home                                 returnTo
140.73  -> watch-sky [saucer]                   looks up, observes
158.17  watch-sky -> rock-1                     previous-plan=completed; back to exploring
```

| Success Demonstration step | where it is asserted |
|---|---|
| 1–3 wanders, notices, decides | `an alien notices a glowing mushroom…` (noticed before chosen; not at t = 0) |
| 4–5 navigates, avoids the rock | same test: the straight line enters the rock, the body never does |
| 6–7 approaches, slows | same: last metre < 0.6 × cruise (fails with `arrival` reverted) |
| 8–10 turns, looks down, observes | same: faces within 15°, look target on the mushroom below the head, ≥ 3 s still |
| 11–12 loses interest, leaves | same: plan `completed`, no return for 20 s |
| 13–14 another event, personality | `the same world event, two personalities…` + swapped-personality control |
| 15–16 another alien | trace: `company/greet` / `company/avoid` (no dedicated test yet) |
| 17–20 UFO, looks up, returns | `an alien notices the saucer, looks up…` |
| determinism | `the same scene twice…`, `a scrub lands on the decision the play made…` |

## Owner rulings (2026-09-21) and what was done

1. **Awareness layer on for Glowmere's five aliens** (Q1). Applied by
   `tools/phase_d/glowmere_awareness.py`, a text edit (no JSON round trip) that adds a `mind` block,
   a `react` to `abduction/beam` and a `social` considerer to each decider, plus `tags: ["alien"]`
   and a personality. The director's beats are world events (`"abduction/beam"`, heard within 60 m).
2. **Roam data made consistent** (Q2): each roam's `minRange` is set to its `approach`, so an
   errand's target can no longer leave range before the body arrives. Decisions over 180 s: 44→34,
   47→27, 26→10, 32→37, 24→22 (rook, tide, sage, ember, vane). Running the cast exposed three
   loops in code, now fixed: a stuck wander target re-chosen at once, a blocked return re-chosen
   2,758 times in 50 s, and a greeting and a leash trading the slot every dwell.
3. **Terrain is not a wander destination** (Q3, ruled technical). A terrain node stays in the
   landmark list and is tagged `terrain` by its kind (never its name); aware deciders refuse it.
   Removing it from the list outright was tried first and changed three pre-Phase-D golden traces
   (`test_decision_extraction`, `test_route_pricing`, a perception subset test) whose deciders read
   the list -- a change nobody asked for, so the refusal lives where the ruling applies.
4. **A scrub replays the director** (ADR-671). The film lands every body exactly where the play
   does, abduction included.

### Glowmere's cast, and how each should read on screen

| alien | personality | why | what the owner should see |
|---|---|---|---|
| rook | curiosity .85, caution .15, sociability .35 | the scout's scene role: first out, furthest out | walks toward the tractor beam and stands watching from ~18 m; roams widest; short looks |
| tide | attentionSpan .95, curiosity .6 | its roam already favours water and glow | lingers longest at glow and the water's edge, slow to lose interest |
| sage | caution .9, curiosity .3 | its `holdPost` grove already made it the homebody | backs away from the beam and from aliens that come close; stays near its grove |
| ember | sociability .95 | its roam already weights characters highest | seeks the other aliens out and stands with them |
| vane | eventSensitivity .95, attentionSpan .8 | its `watch` already weights characters 4.8 | the first head to turn to anything that happens or moves nearby |

Authored beats win by construction. The staging binds only `animal`-tagged bodies and flies the
craft. The aliens' decider fills the lowest authority tier (Routine), so any director or action cue
outranks it. Maximum distance from anchor over 180 s did not grow: rook 43→41 m, tide 70→57,
sage 26→26, ember 59→53, vane 68→63. The Glowmere-tuned suites pass unmodified, except the layers
control, which now also turns off the new second source of look targets and gains an arm that
proves that source works.

## Section status (D 1–78)

`done` = built and tested here, or audited as met; `partial` = some of it; `open` = not started.

| § | status | § | status | § | status |
|---|---|---|---|---|---|
| 1 | done (audit above) | 27 | done | 53 | done (IConsiderer is the seam; no model) |
| 2 | done (boundary held) | 28 | done (signal→semantic events) | 54 | done (IConsiderer) |
| 3 | done (producer) | 29 | done | 55 | done (decide + queue; ADR-670) |
| 4 | done (no FSM) | 30 | partial (existing cadences; no budget tiers) | 56 | done |
| 5 | done | 31 | open | 57 | done |
| 6 | done | 32 | done (sim ≠ render; LOD caveat D3) | 58 | done (trace + validate) |
| 7 | done | 33 | done (two replay defects fixed) | 59 | partial (unreachable/deleted handled; audit open) |
| 8 | partial (benchmarks open) | 34 | done (goal considerer) | 60 | done (no teleport path) |
| 9 | done | 35 | open | 61 | done (decides at t = 0) |
| 10 | done | 36 | open | 62 | done (fixed step) |
| 11 | done | 37 | done | 63 | done (director replayed, ADR-671) |
| 12 | done | 38 | done (beam as event; react) | 64 | done |
| 13 | done | 39 | done (20-alien test; 1-200 benchmark) | 65 | done |
| 14 | partial (arrive; no ORCA) | 40 | partial (data; no canvas overlay) | 66 | done (owner ruling Q1) |
| 15 | done | 41 | done | 67 | done |
| 16 | done | 42 | partial | 68 | partial (glance while idle) |
| 17 | done | 43 | partial | 69 | partial (saucer tagged `world_effect`) |
| 18 | done | 44 | partial (crowd benchmark) | 70 | done (as far as events) |
| 19 | done | 45 | open (doc) | 71 | done |
| 20 | done | 46 | partial | 72 | partial (this doc + ADR-670) |
| 21 | done | 47 | done | 73 | n/a (ordering) |
| 22 | done | 48 | open | 74 | partial |
| 23 | partial (interest over percepts) | 49 | partial | 75 | open (final report) |
| 24 | done (minimal) | 50 | done | 76 | done |
| 25 | done | 51 | open (research note) | 77 | done (pasture) |
| 26 | done | 52 | open | 78 | done (this table) |

## Phase log

**2026-09-21: the pipeline runs end to end.** Findings that shaped it, each measured on the demo:
the shore creep (approach inside minRange, then the novelty memory discounting the destination one
tick after choosing it); a 16-percept capacity cut dropping the saucer; a watch interrupted by the
stall breaker at exactly its dwell; greet/avoid alternating inside two seconds; and a scrub 111 m
from the play. Each is fixed where it lives and cited in the code at the fix.
