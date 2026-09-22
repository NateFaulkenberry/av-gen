# Character behaviour model (Phase D §4–§6, §19–§24, §29–§31, §34, §53–§55, §72)

How an AV Gen character decides what to do. The architecture record is ADR-269, ADR-333 and
ADR-670; this is the model as one page.

## The loop, per character, per step

```
percepts (GridPerception, cadenced)      world events (EntityWorld, strictly earlier)
        \                                   /
         +-- ObjectMemory.noticed / hear --+        (mind.hpp, bounded, replayed)
                         |
                  AttentionModel  ---- glance: look target when nothing else set one
                         |
          DecisionContext { percepts, visited, mind: personality, memory, events, attention }
                         |
   considerers (scene data) -> Options { score, factors, intent, subject, actions }
                         |
          Selector: tick cadence, dwell, margin, hold-while-known
                         |  changed?
          ActionQueue.override(Routine)  -> serial
                         |
          Move / Face / Pose / Interact  -> travel, yaw, speed, intent HOW
                         |
          drained(serial)? completed -> investigated; failed -> suppressed; choose again
```

## Why a scored option list, and not the alternatives (§51)

| architecture | verdict | reason |
|---|---|---|
| FSM | rejected (§4) | every new behaviour is an enum case and a transition table |
| Behaviour tree | rejected (ADR-269) | a second interpreter over `ActionQueue`'s primitives, with its own running/succeeded/failed beside `ActionResult` |
| GOAP | rejected for now | the demonstration's goals are one step deep (go, look, leave); a planner would search a space of size one. `Move`+`Interact` sequences are the plan. |
| Utility AI | **adopted** | explicable: one axis, every option's factors printable (§41) |
| Planner + utility | partly | the queue *is* the hierarchical executor; considerers emit short action plans |
| HSM | not needed | the queue's three authority tiers are the hierarchy |

The phase's own expectation — "a utility/goal layer + hierarchical behaviour execution + explicit
motion requests" (§51) — is what exists: considerers + `ActionQueue` tiers + `CharacterIntent`.

## Hysteresis (§20), the mechanisms and what each stops

- **dwell** (`dwellTicks`): a briefly stronger challenger.
- **margin**: a near-equal challenger.
- **commitment** (`Selector::commit`, aware deciders): a running errand is compared at 1.25x its
  live score -- a tug between two authored pulls (Glowmere's ember: greeting vs its leash).
- **hold** (ADR-670): an errand whose proposer went quiet mid-walk (the shore creep).
- **exclusion** (`Selector::exclude`): an option whose plan just failed, for `failSeconds` (rook's
  blocked return, 2,758 retries in 50 s).
- **personal-space band** (`social`): approach/avoid alternating.
Measured failure each prevents is recorded at its code.

## Lifecycle (§19)

`CanStart` is "offers an option with score > 0". `Start` is the commit. `Update` is the queue.
`CanInterrupt` is the selector rules above. `Interrupt` is `override` (reported `Cancelled`).
`Complete`/`Fail` are the drained serial. Failure is first-class: an unreachable target is
suppressed for `failSeconds`, so there is no unbounded retry (§59).

## Personality (§29)

Nine traits, 0.5 neutral, entering only as `(0.5 + trait)^w`. `react` and `social` read traits
directly to split their options; any other considerer is bent by declaring `"traits"`.

## Hybrid and authored modes (§34, §35)

- **Autonomous**: considerers only.
- **Hybrid**: a `goal` considerer names *what* ("investigate mushroom-1 from 32 s"); the character
  does the rest. Its `weight` is a registered parameter, so a timeline track keyframes it.
- **Authored**: `schedule` / `actions` on the Action or Director tier, which outrank the decider and
  resume it afterwards (ADR-091).
A director targets character, goal, behaviour or event through the same seams; no clip is named.

## Behaviour LOD (§30, §31) — the design, not yet built beyond what existed

| tier | what runs | how it is chosen |
|---|---|---|
| 0 full | senses at `perception.hertz`, decides at `hertz`, full step | inside `fullDetailDistance` (existing) |
| 1 reduced | senses and decides at half rate | *not built* |
| 2 background | coarse step every `coarseInterval` (existing) — **violates D3**: a coarse step integrates differently | existing band; see character_ai.hpp §1 |
| 3 dormant | state snapshot only | `cullDistance` (existing); resumes from its snapshot |

D3 forbids a tier that changes the integration step, and tier 2 does.

**Why tier 1 is not chosen by the camera, and is therefore authored.** A cadence that falls with
distance *from the camera* makes a character's decisions a function of where the camera was: a
render from a different shot, or a scrub (which has no camera, ADR-273), would decide differently.
ADR-671 made a scrub land exactly on the play, and a camera-keyed tier would undo that for every
distant character. So behaviour LOD here is **authored per character**: the two `hertz` knobs
(`perception.hertz`, `decide.hertz`) and `memorySeconds` are the tier, set by what a character is
for in the film, not by what the lens sees this frame. Measured cost says this is enough: 0.011 ms
per decider per step (performance section of the report), so 1,000 background characters would
spend about 11 ms, and the scene update, not behaviour, is the limit.

## Runtime cost

Per decider per decision tick: considerers O(percepts), attention O(percepts + events), memory
O(capacity). Measured in the phase log (§44). Allocation: option names and trace lines allocate on
*changes of mind* only; the per-step path reuses storage.

## Future learned behaviour (§53, §54, §71)

A learned policy is an `IConsiderer`: it reads a `DecisionContext` and appends scored options. It
replaces behaviour selection without touching perception, navigation, IK or motion. An optional
LLM goal provider is a `goal` considerer whose subject it writes — high-level goals only, never in
the frame loop (§50).

## Limitations

- Options are scored independently; no joint reasoning across characters (§24 is minimal).
- Goals are one errand deep; no multi-step planning beyond an action list.
- Signal-bus events are not reproduced by a scrub (ADR-670); director beats are (ADR-671).
