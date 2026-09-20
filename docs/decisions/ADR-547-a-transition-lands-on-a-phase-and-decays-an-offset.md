# ADR-547: A transition lands on a phase, and decays an offset instead of playing two clips

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-086 (the player stores *when*, not how long), ADR-089 (a timeline cue restarts),
ADR-541 (state lives where a seek replays it), ADR-546 (contacts and phase)
**Implemented by:** `AnimationState::matchPhase`, `AnimationPlayer::play(…, PhaseMatch)`,
`AnimationPlayer::inertializeHalflife`, `SkinnedRig::analyse`, and the scene keys
`animation.contacts` / `animation.matchPhase` / `animation.inertialize`.
**Tests:** `tests/unit/test_transitions.cpp`.

---

## Context

Two defects, both reachable from one line of `AnimationPlayer::play`:

```cpp
current_.start = now;
```

**The incoming clip starts at its own frame zero.** A walk at 73% of its cycle cross-fading into a
run lands on the run's frame 0, and whether the feet agree is luck. Phase 0 recorded this
(`docs/design/autonomous-character-animation.md` §1.6) and called it the cheapest visible quality
win available; ADR-546 built the phase tracks that make it answerable.

**And the transition is a cross-fade**, which evaluates *both* clips for its whole duration. The
cost of a transition is therefore highest exactly while the character is busiest, and it scales
with how many joints the two clips disagree about.

## Decision

### Phase matching

When the target state sets `matchPhase` and both clips have a phase track, `play` rebases the
incoming clock so that `now` lands at the incoming clip's instant of **equal phase**:

```
want = phase[outgoing].at(localTime(outgoing, now))
land = phase[incoming].timeAt(want)
current_.start = now - land / speed
```

`PhaseTrack::timeAt` is the inverse of `at`, following the same shortest-way-round rule across the
wrap so the two really are inverses. It is a search over the grid rather than a formula, because
the phase track is data.

**Off by default, and it falls back rather than failing.** A clip nobody analysed has no phase
track, and a transition into it starts at frame zero exactly as it always did. That is the honest
answer, not a degraded one: there is nothing to align to.

### Inertialization

When `inertializeHalflife > 0`, `evaluate` samples **only the incoming clip** and adds the pose
difference captured at the transition instant, decaying it to nothing:

```
offset = pose(outgoing state, at blendStart) - pose(incoming state, at blendStart)
y      = 2 ln2 / halflife
decay  = (1 + y t) e^{-y t}          // critically damped, released from rest
final  = pose(incoming, now) + offset * decay
```

`(1 + yt)e^{-yt}` is exactly 1 with zero slope at `t = 0`, so the pose at the transition instant is
*exactly* the outgoing pose and there is no step — and no kink either, which a plain exponential
would have.

**The offset is recomputed, not remembered**, and that is the decision that matters. It is a pure
function of `(previous, previous start, current, current start, clips)` — all of which the player
already holds — so a scrub landing mid-transition reconstructs the same offset instead of
inheriting one from wherever the playhead came from. The class's whole contract is that a pose is a
function of *when* a state was entered (ADR-086), and storing a captured offset would have been the
one thing in it that was a function of how it got there.

Rotational offsets are composed and slerped from identity by the decay, not lerped as quaternions,
so a half-decayed offset is half the *angle*.

### Authoring

Three node keys, all opt-in: `contacts` (the joints to analyse, first one is the phase reference),
`matchPhase`, `inertialize` (the half-life). `SkinnedRig::analyse()` runs at bind time and is
offline in the sense that matters — once per rig, not per frame — and it warns when no clip came
back cyclic, because phase matching on such a rig would silently do nothing.

## Evidence

Two one-second ramp clips whose sampled value names the instant being played: `walk` 0→10, `run`
100→110. Entering `run` at t = 0.6:

| | root X at the transition instant |
|---|---|
| `matchPhase` off | **100.0** — the run's frame zero, as before |
| `matchPhase` on | **106.0** — 0.6 through the run's cycle, matching the walk |
| phase tracks absent | 100.0 — the documented fallback |

The two differ by six units, which is the arm the old behaviour fails outright.

Inertialization, across a deliberate 100-unit step between clips: the pose at the transition instant
is **0.00** (the outgoing pose, no step); it rises monotonically with no overshoot below 0 or above
100 across 35 samples; it reaches 100 within a few half-lives; it differs from the cross-fade it
replaces by more than a unit at the same instant; and **poisoning the outgoing clip after the
transition instant changes nothing**, which is the arm that proves only one clip is being read.

Determinism: evaluating an instant after walking forward through the transition, and evaluating the
same instant cold, give bit-identical results.

## Rejected alternatives

* **Bollo's quintic** (GDC 2018). It bounds overshoot explicitly and reaches zero in finite time,
  which the spring does not. It is also parameterised by a *duration* and needs `x0` and `v0`
  captured at the transition — more state, and state of exactly the kind ADR-086 forbids here. The
  spring's closed form takes `(offset, elapsed, halflife)` and nothing else. If bounded finite
  transitions are ever needed, the quintic goes in beside it rather than instead of it.
* **Storing the captured offset.** One `std::vector<Transform>` per rig, invalidated by a seek,
  re-captured by a flag — the shape of `SkinnedRig::reseedPrevious`, which exists because that
  pattern is hard. Recomputing costs two extra clip samples while a transition is in flight and
  nothing at all when one is not.
* **Phase matching on by default.** It changes which frame a state starts on. Every scene that
  authored a transition around the old behaviour must keep getting it.
* **Making `analyse()` implicit at import.** It costs milliseconds per clip and most rigs in this
  repository will never use phase. A scene that wants it says so.

## Consequences

* Walk→run and run→walk can land on the correct foot for the first time.
* A transition's cost stops doubling for its duration.
* `SkinnedRig` carries per-clip contacts and phase, which is the metadata a motion pack (ADR-542)
  will serialise rather than recompute.

## Revisit triggers

* A clip whose phase track is non-monotone — `timeAt` returns the *first* bracketing interval and a
  track that revisits a phase would want the nearest instead.
* Inertialization interacting with the pose layers: the offset is applied before layers run, so a
  foot IK correction during a transition solves against the blended pose. That is probably right
  and has not been measured.
* Bounded-duration transitions: see the quintic above.
