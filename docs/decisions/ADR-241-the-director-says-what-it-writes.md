# ADR-241: The director says what it writes

**Status:** Accepted
**Date:** 2026-09-15
**Follows:** ADR-210 (the director is a decision layer), ADR-211 (the Inspector answers "why is this
moving?"), ADR-182 (a probe that cannot fail proves nothing)

## Context

ADR-211 gave the Inspector a kind for every writer of a parameter's final, and a hygiene test that
greps `src/` for those writers and fails on any the panel cannot name. It found `stage/staging.cpp`
on the day it landed, and ADR-211 then recorded — rather than fixed — what that finding meant:

> `stage/staging.cpp` → `Kind::Entity`, and only PARTLY. A scenario drives a body through
> `entity::DirectorMotion`, so a staged *entity* is named — but a scenario also writes nodes that are
> not entities, such as the tractor beam's visibility, and those still show nothing. The honest
> answer is a `Kind::Staging` that names the scenario and the role.

That comment has been sitting in the test as a paid-for TODO. This is the fix.

The hole is not cosmetic. The one panel whose entire job is answering *why is this moving* said
"nothing modulates this object; its parameters are static" about a beam a scenario hides and shows,
a particle system whose spawn rate a `set` step ramps, and any absolute path an author wrote into a
step. Every one of those is a parameter that visibly changes while the panel denies anything can
change it — which is worse than no panel, because a panel that is right most of the time is trusted
all of the time.

## Decision

### 1. A scenario is asked what it writes, rather than the panel guessing

`Staging::writersOf(path)` returns a `PathWriter` per scenario: the scenario's name, the role that
resolved the path when a role did, whether that scenario is running right now, and where the answer
came from. `influencesOf` turns each into an `Influence::Kind::Staging`.

The alternative was for the panel to walk `StagingDesc` itself, the way it walks routes and tracks.
Rejected: resolving a step's target is `parameterPath`'s job and it depends on a role binding, a
`resolveTarget` fallback chain and the entity world. A second implementation in the UI would be a
second spelling of the rule, and the first thing it would drift on is the fallback order.

### 2. Two sources, and the difference is reported rather than smoothed over

**Declared.** A step whose target contains a `/` is an absolute path, written as the author wrote it.
That is knowable from the description alone, before anything runs — and it is the case this ADR
exists for, because the tractor beam's `nodes/<beam>/visible` is exactly that. A beam nobody has
fired yet still has a director that can hide it.

**Observed.** A step whose target is a role-relative name — `visible`, `spawnRate` — cannot be
resolved until the role binds to a body, so it is knowable only once written. `Staging::writeParameter`
already recorded every path it touched, with the authored base, so `reset` can put the scene back;
that record now also carries the scenario and the role. The two have the same lifetime and are
cleared together, which is the reason to put them in the same place rather than in a second list
that could disagree with the first.

`PathWriter::live` says which of the two an answer is, and the panel prints it. Reporting a declared
target as "not yet" would answer *what can move this* with *nothing*, which is the original defect
wearing a different hat. Reporting an observed one as though it were declared would claim knowledge
of a scenario that has never run.

**One entry per scenario per path**, deliberately not keyed on the role as well: a `set` step naming
an absolute path still carries its cue's role, so a pair key reports the same scenario twice — once
as declared with no role, once as observed with one.

### 3. What is *not* an influence, stated

`Staging::setParameter` writes a base and a final on a scenario's own knob —
`staging/<scenario>/<name>`. It is not listed, for the same reason `ui/edit_history.cpp` is not: that
is an author moving a slider. The hygiene test's comment now says so, so the next person does not
re-litigate it from the grep alone.

## What was measured

`tests/unit/test_staging.cpp`, three sections, and **the negative controls are the test**. A query
that answered "the director" for every path would pass the positive half while being worthless.

| | asserted |
|---|---|
| an absolute target, before anything runs | one writer, `live == false`, `running == false` |
| the role-relative step in the same cue, before anything runs | **empty** — the declared branch matches on the path, not on the scenario |
| the same path after the scenario runs | one writer, `live == true`, role `actor` |
| the absolute path after it has been both declared and observed | **one** writer, not two |
| a path no scenario touches (`position`, `scale`) | **empty** |

The second row is the control that matters. Without it, a declared branch that reported every step of
any scenario touching any path in the same cue would pass every other row in this table.

## Consequences

**The Inspector's claim is whole for the first time.** Every writer the hygiene grep finds now has a
kind, and `stage/staging.cpp` is named for both of the things it does rather than one.

**A scenario that has never run still under-reports its role-relative targets.** This is a real
limit and not a bug: the path does not exist yet. An author who wants the Inspector to name a
scenario's effect on a body before it fires can write the absolute path in the step. Recorded here
rather than claimed away.

**`writersOf` is linear in scenarios × beats × cues × steps.** It runs once per Inspector row on a
selected node, against a description with single-digit scenarios. If a scene ever carries hundreds,
this is a map to build once — but ADR-233 measured the whole ImGui pass at under 3.5% of a frame,
and this is a fraction of that, so it is not worth pre-building today.
