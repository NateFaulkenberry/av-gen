# The Director system and the UFO abduction POC — plan

**Status:** planned, not started. Deferred behind three in-flight branches (see "Why this is batched").
**Written:** 2026-09-14

## The headline: most of Parts 3–5 already exist

The brief asks for "a small, composable behavior system" with primitives, targets, animation
selection, completion, and a next action. **ADR-096 built that**, and `src/entity/action.hpp` opens
by stating the pipeline in the brief's own shape:

```
Character -> Action -> Target -> Animation -> Completion -> Next Action
```

What is already there, verified by reading it rather than assumed:

| The brief asks for | What exists today |
|---|---|
| MoveTo, MoveBy | `ActionKind::Move` with an `ActionTarget` and an `IPathProvider` |
| RotateTo, LookAt | `ActionKind::Face` |
| PlayAnimation, SetAnimationState | `ActionKind::Pose` — an *activity* name, never a clip name (`EntityDesc::clips` maps it per asset) |
| Wait | `ActionKind::Wait` |
| Sequence | `std::vector<ActionDesc>` with labels, `when` conditions and `otherwise` branches |
| Repeat, timing | `ScheduleDesc` / `Schedule`, with a pause-aware clock |
| Attach | `ActionKind::Equip` onto a named socket |
| Show/Hide, parameters | `ActionKind::Set` writing `PropertySet`s |
| A director tier | **`Authority::Director` already exists** — "a shot says exactly what happens" — and `Entity::…override(actions, Authority::Director, now)` is live at `entity.cpp:815` |
| Spatial queries | `spatial::PointGrid::query(p, radius, out)`, `spatial::ObstacleField` for obstruction |
| Parent/child transforms | the scene hierarchy; behaviours produce `MotionOffset`s that compose with routes rather than overwriting |

ADR-091's hierarchy is already **Director → Cinematic Action → Behavior → Navigation**, read
top-down, with the rule that a tier finishing resumes the tier below rather than resetting it.

**So this is not a new framework.** Building one would be the "parallel version of existing
functionality" the brief explicitly forbids. Four things are genuinely missing, and they are the
whole job:

1. **A decision layer.** `Authority::Director` is a tier that can be *told* what to do. Nothing
   currently *decides* at that tier. This is `FindNearest` / `FindWithinRadius` / `FindByTag`, the
   exclusion of targets already claimed by another action, and the loop: pick a target → issue
   actions → wait for completion → pick again.
2. **Actor grouping.** UFO + tractor beam as one logical unit the director moves. The transform
   relationship is ordinary scene parenting; what is missing is the name for the group.
3. **Sequencer binding.** A director event on the existing timeline. `ScheduleDesc` already has the
   scheduling semantics; it needs a track.
4. **`Parallel`.** The action queue is a stack of tiers per entity. Two things happening at once
   across *different* entities (UFO hovering while an animal rises) is a director-level concern, not
   an entity-level one — which is more evidence the missing piece is the decision layer.

### Naming

`WorldDirector` is taken: ADR-041 uses it for art-direction knobs (Drama, Warmth, Mystery…), which
are world macros with artistic names. The behaviour director needs a different name — **`Staging`**
or **`Scenario`** — and the docs must not conflate the two.

## Why this is batched

The user asked whether this conflicts with work in progress. It does, with all three branches, on
their primary files:

| Part | Files it must touch | In-flight branch that owns them |
|---|---|---|
| 1 — scatter animals | `examples/world/glowmere-valley-2.scene.json` | `agent/farm-animals` — **and the assets do not exist yet** |
| 2 — animal wander | `src/entity/behaviors.cpp`, `src/entity/navigation.*` | `agent/anim-cleanup` (these are its main files) |
| 3–5 — director core | `src/entity/action.*`, new files | `agent/anim-cleanup` (`src/entity/*`) |
| 6 — sequencer UI | `src/ui/sequence_panel.cpp`, `src/ui/control_panel.cpp` | `agent/world-effects` (control panel) |
| 7 — UFO + tractor beam | `src/rendering/*`, `shaders/*` | `agent/world-effects` (directly) |

Part 1 is also a hard dependency, not merely a conflict: there are no farm animals in the repository
until `agent/farm-animals` lands. And `glowmere-valley-2.scene.json` is fingerprinted by a test
(sha256 + size), so two branches editing it produce a conflict *and* a failing test.

## Order of work, once the branches are merged

1. Merge `agent/farm-animals`, `agent/anim-cleanup`, `agent/world-effects`; run the full suite.
2. **Part 1** — scatter. Use the existing placement/spatial-query infrastructure
   (`src/app/placement.*`, `src/scene/placement`, `ObstacleField`), not a new scatterer. Visible
   groups in clearings and near water; concealed ones behind vegetation. Refresh the scene
   fingerprint.
3. **Part 2** — wander. A `Territory`-style behaviour in the existing `behaviors.cpp` idiom: a home
   point, a radius, randomised waits and destinations, and a validated route before committing. Fall
   back to idle on repeated failure rather than allowing a stuck animal — the `sage` 62-second wedge
   is the precedent for measuring this rather than eyeballing it.
4. **Parts 3–5** — the decision layer, in new files. Scene queries with target claiming; an actor
   group; a director loop driving `Authority::Director` overrides.
5. **Part 6** — one track type in the existing sequencer. No second timeline.
6. **Part 7** — the UFO abduction, expressed *entirely* in terms of 3–5. The acceptance test for the
   architecture is that the UFO sequence contains no UFO-specific C++ beyond its parameters.

## What to measure, not assume

- Animals stuck: percentage of frames where a wandering animal is commanded to move and does not,
  and the longest single stall. The precedent number to beat is the sage fix — 92.3% still and a
  62.3 s longest stall, down to 31.7% and 0.1 s.
- Abduction: that the POC completes *several sequential* abductions with dynamically chosen targets,
  asserted by counting distinct abducted animals in a headless run, not by watching it.
- Failure cases from the brief's test list (no valid targets, target disappears, actor removed,
  sequence cancelled) — each must leave the director idle and restartable, asserted directly.
- Performance: no per-frame scan over all entities. Search on an interval, against `PointGrid`.
