# ADR-601: The solver's reach is a property of the frame, and on this rig the frame changes it

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-543 (a limb is three named joints and a write-back rule), ADR-553 (a rotation
retarget cannot animate a rig that is not a hierarchy), ADR-600 (a gate with nothing configured
refuses nothing), `docs/testing.md` #24 (seventh variant)
**Implemented by:** nothing — this ADR records a measurement and a deliberate non-change
**Tests:** `tests/unit/test_alien_validation.cpp` — "a hand target's reachability depends on the
frame it is asked on"

---

## Context

`solveTwoBone` takes a `TwoBoneChain` of three model-space positions and derives the two bone
lengths from them: `l1 = |mid - root|`, `l2 = |tip - mid|`, `maxReach = (l1 + l2) * extension`.
That is correct and deliberate — ADR-543's whole point is that a limb is three *named joints*,
not an ancestor chain, so the solver cannot consult a hierarchy that may not exist.

It carries a consequence that has never been written down. On an ancestor chain the derivation is
safe by construction: a clip that only rotates cannot change the distance between a joint and its
parent, so `l1` and `l2` are constants of the skeleton however the clip poses it. On a rig whose
joints are siblings rather than ancestors, nothing guarantees that. The lengths become a property
of the frame.

Phase B §5 found this from the wrong end: a fixture posed a tip without its mid, the second bone
shortened from its rest 0.626 to 0.430, and the control I had built to test something else refuted
my diagnosis of a clamping foot. The open question §45 inherited was whether this is a hazard of
synthetic rigs built with no slack, or something the shipping character actually does.

## Decision

**Record it, assert the bound, and change nothing yet.**

The shipping character does it. `assets/aliens/alien-scout.glb` is flat — every joint of both solved
chains is a sibling under `rig` or `root.x`, so no step of either chain is structurally
length-preserving. Measured across all 26 clips:

| segment | rest | worst, any clip | worst, a walk |
|---|---|---|---|
| leg.l upper (`thigh_twist.l` → `leg_stretch.l`) | 0.2819 m | 0.0% | 0.0% |
| leg.l lower (`leg_stretch.l` → `foot.l`) | 0.3736 m | 0.0% | 0.0% |
| **arm.l upper** (`shoulder.l` → `forearm_stretch.l`) | 0.3316 m | **21.4%** (`Crazy`) | **14.3%** |
| arm.l lower (`forearm_stretch.l` → `hand.l`) | 0.2018 m | 0.0% | 0.0% |

All 26 clips carry translation channels on all eight joints, so the three zeros are not "no channel"
— they are channels whose values never leave the rest translation. Blender exports TRS for
everything; the animator moved one of them. **The legs are safe by accident, not by structure**, and
a re-export in which someone keyframes a hip slide moves them into the arm's column silently.

The consequence is a band of hand targets whose reachability depends on which frame of the walk they
are asked on. Over `Walking`, the arm's `maxReach` runs 0.5091 m to 0.5398 m: a **0.0307 m band,
5.7% of the arm**. A Reach layer holding a fixed world point inside that band reports `Solved` on
some frames and `Clamped` on others, and the hand leaves the target and returns once per stride.

**In metres that is large. In this scene's frame it is half a pixel.** Glowmere's valley cameras sit
170 m from their target at a 40° vertical field; at 1080p that is 8.7 px/m, and the whole 1.9 m alien
is sixteen pixels tall. The band is 0.52 px. At a character-scale framing — the alien filling 60% of
frame height — the same 6 cm is 20.3 px, and 20 px of hand detaching once per stride is not subtle.

So: real on shipping content, and invisible at the only framing that exists today. That is the same
conversion the foot-lock drift needed, and it sets the priority rather than the fix.

## What was rejected, and why it is the interesting part

The obvious move is to add optional `upperLength` / `lowerLength` to `TwoBoneChain`, defaulting to
0 meaning "derive from the pose", so a caller that knows its rest lengths can pass them. It is a
structural fix, it is a no-op for every existing caller, and it costs an afternoon.

**It is also exactly ADR-600.** A field nobody sets refuses nothing. Shipped today it would add a
knob at its inert default, an ADR claiming the issue was addressed, and a test proving the default
path is unchanged — which is precisely the shape of the dead gate §44 found, built again by someone
who had just written the ADR about it. The thing that makes the fix real is a *caller* that sets it,
and Phase B has no Reach consumer at a framing where 20 px matters.

Two further reasons to wait, both of which are why this is not merely procrastination:

- **The blast radius is not the alien.** `solveTwoBone`'s `extension` defaults to 1 because every
  rig in `assets/farm` binds with its hind leg at 97.9%–100% of its own span. Those are the callers
  whose behaviour a rest-derived length would change, and the bull at 100% is the one it would
  change most. A fix validated only on the alien would ship to the herd untested.
- **Rest is not obviously the right answer either.** A stretchy rig — `leg_stretch`, `forearm_stretch`
  are named what they are named — may intend its segments to change length. "Use the rest length"
  assumes the variation is an accident. On `Crazy` it plainly is not.

## Consequences

- The measurement is an assertion, not a note. `test_alien_validation.cpp` asserts the band is
  non-zero, under 10% of reach, sub-pixel in the valley framing and over 4 px in a close one. A
  re-export that keyframes a hip, a retarget that changes the chains, or a solver change that closes
  the band all fail here and say which.
- **The legs' zeros are asserted too**, so the accident is monitored. They are the ones that would
  hurt: a foot lock is the layer that holds a fixed world point, and it is on the chain that is
  currently safe for no reason anyone chose.
- A consumer that needs a hand held at a close framing is the trigger to implement. At that point
  the decision to make is per-chain and per-rig, informed by the farm pack, not a global default.
- ADR-543 is unchanged and still right. The lesson is not "the solver should consult a hierarchy" —
  it cannot, that is the point — but that *what a hierarchy used to guarantee for free now has to be
  stated*, and on this rig nobody stated it.
