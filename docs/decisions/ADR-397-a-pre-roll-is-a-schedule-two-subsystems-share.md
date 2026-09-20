# ADR-397: A pre-roll is a schedule, and two subsystems share it rather than write it twice

- Status: Accepted (2026-09-20)
- Builds on ADR-395 (ADR-360's particle warm-up, the first consumer), ADR-035 (temporal history),
  ADR-012 (the time model). Adjacent to the temporal-media work's own ADR-394.

## Problem

Two subsystems needed the same thing within a day of each other.

ADR-360 wanted a bounded warm-up for particle pools: a render whose range opens at t > 0 starts
with every pool empty and the field blooms in from nothing over one particle lifetime. The
temporal-media work wants one for history buffers, on the framing that **history is not state, it
is a cache of a pure function** — so a seek rebuilds it by re-running a bounded number of frames
and nothing accumulates without bound.

That framing is right, and it is the same shape as re-seeding a particle pool. Written twice it
would be two caps, two step-size rules, two notions of what a discontinuity is — and the third of
those is where the repository has already been hurt.

## The distinction this is really about

`ao_renderer.cpp:271-296` carries the scar. Two cases need **opposite** treatment and were once one
case (`SYM-TERRAIN-1`):

- A **jump** — a seek, a cut, a reverse — invalidates the history. The previous frame is not this
  frame's past, so accumulating against it smears across the jump.
- A **repeat** — the same frame rendered twice — is the opposite. The first render legitimately had
  history and used it; dropping it on the second makes the second render *a different picture from
  the first*, which is exactly the non-determinism the rule exists to prevent.

Any subsystem that re-runs frames after a discontinuity has to make that distinction, and every one
that re-derives it is a chance to get it wrong again.

## Decision

`src/core/pre_roll.hpp`, in `avgen_core`, so both the CPU suite and every renderer can reach it.

**`classifyStep(havePrevious, previous, current) -> TimelineStep`** — `First`, `Repeat`,
`Continuous`, `Jump`. It reads the frame index **and** the render time, and both halves are load
bearing:

- On the index alone, a seek in the live application looks continuous. `RealtimeClock::seek` moves
  `renderTime` and leaves the counter climbing, so the frame after a scrub is index + 1 carrying a
  second from somewhere else.
- On the render time alone, an offline re-render of one frame looks like a repeat of a frame that
  was never drawn.

So: `Continuous` only when the index advanced by one *and* the second advanced by this frame's own
delta; `Repeat` only when neither moved. The tolerance is float slop (1e-9 + 1e-6 relative), not a
window — both clocks in this engine produce render times by exact accumulation or exact
multiplication, and a window wide enough to absorb a real seek would classify a small one as
motion, which is the smear the enum exists to prevent. `tests/unit/test_pre_roll.cpp` pins half a
frame as a `Jump` and two ulps as `Continuous`.

**`planPreRoll(roll, time) -> PreRollPlan`** — the schedule. `roll.frames` (0 by default, so no
caller that has not opted in pays anything), `roll.cap` (240, and a request above it is clamped
rather than honoured), `roll.stepSeconds` (0 = take the arriving frame's own delta, or 1/60 when it
has none).

What is shared is the **schedule, not the work**. The plan says which timeline seconds the roll
consists of and what frame indices they carry; each subsystem re-runs its own simulation over them.
That seam is deliberate: the schedule is the part that is easy to get subtly wrong, and it is the
part that is checkable on the CPU with no device in the room.

**`PreRollPlan::arrivalFrameIndex`** is the one piece that took a second attempt to get right. The
roll's indices are consecutive and end one below the arriving frame's, so a consumer accumulating
across the roll sees `Continuous` at every step *and on arrival*. When there is no room below the
arriving frame — a render range that opens at frame 0 with a roll of 36 — the arriving frame's
index is shifted up rather than the roll being silently shortened. A shortened roll at exactly the
moment a roll is wanted would be the mitigation quietly not happening, and a roll whose product is
discarded on the frame it was built for is the same thing one step later. The test asserts both:
that the shifted arrival is `Continuous`, and that the *unshifted* one would have been a `Jump`.

A consumer whose re-simulation keys nothing on the frame index ignores it. `ParticleRenderer` does,
and says so at the call site: the compaction carries its pools across frames by itself, and
rewriting the arriving frame's index would only move the trail stride's phase.

## Consequences

- `ParticleRenderer::runWarmUp` is now eleven lines of loop over `planPreRoll`. The cap, the step
  rule and the index chaining left it.
- The interface is defined by the first consumer and is the second's to use as it stands. If the
  temporal-media work needs something the plan cannot say, that is an addition here rather than a
  second mechanism.
- `classifyStep` is not yet wired into `ao_renderer.cpp`, which still open-codes the same rule on
  `frameIndex` alone. It is correct there — AO accumulates per frame index and that is genuinely
  what it cares about — but it is a second implementation of a distinction that now has a name, and
  it should converge when somebody is next in that file.

## Revisit when

- A third consumer appears, or the second one needs the arriving frame's *time* adjusted as well as
  its index.
