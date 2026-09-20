# ADR-546: A planted foot is low and still, not stationary

**Status:** Accepted
**Date:** 2026-09-20
**Related:** ADR-161 (root motion is not in this content), ADR-359 (a leg is three names), ADR-540
(the motion database does not exist, and every locomotion clip is in place), ADR-543, ADR-544
**Implemented by:** `src/scene/motion_analysis.{hpp,cpp}` — `detectContacts`, `extractPhase`,
`analyseClip`. Offline only; nothing here runs per frame.
**Tests:** `tests/unit/test_motion_analysis.cpp` — 13 cases, 1,028 assertions.

---

## Context

Phase A needs foot contacts and a motion phase: contacts weight foot IK and bound its lifecycle,
phase makes a transition land on the right foot instead of on clip frame zero.

The textbook detector is "a foot is planted when it is near the ground and not moving". It is in
every paper and every reference implementation, and **on this repository's content it is exactly
backwards.**

## The measurement that said so

The first implementation used ground-frame speed — the joint's full 3D velocity with the root's
horizontal travel removed. Run over all 26 clips of `alien-scout.glb`:

| clip | left contacts | right contacts | left duty | right duty |
|---|---:|---:|---:|---:|
| **`Walking`** | **0** | 1 | **0.00** | 0.06 |
| **`Running`** | **0** | **0** | 0.00 | 0.00 |
| `Walking_crouch` | 0 | 0 | 0.00 | 0.00 |
| `Walking_low_grav` | 0 | 0 | 0.00 | 0.00 |
| `Idle` | 1 | 1 | **1.00** | **1.00** |
| `Flying_jet` | 1 | 1 | **1.00** | **1.00** |

It found no contacts on any clip locomotion uses, and a 100% duty cycle on a clip where the
character is *flying*.

**The cause is ADR-540's finding, one level down.** Every locomotion clip in this repository is
authored **in place**. In an in-place cycle the body does not travel, so the *stance foot is the
thing that moves*: it sweeps backwards under the hips at exactly the authored stride speed while
the swing foot comes forward. A horizontal-speed test therefore finds the swing and calls it the
plant. And in `Idle` and `Flying_jet` nothing moves at all, so everything is "planted".

Removing the root's travel does not help, because there is no root travel to remove. **An in-place
clip has no ground frame.**

## Decision

**Planted = low, and not changing height.** The horizontal component is used only when the clip's
root genuinely travels (planar root speed above 0.05 model units/s), where a ground frame exists
and the textbook rule is correct.

Height and vertical speed have no dependence on whether the clip travels: a stance foot is at its
lowest and stays there either way, and a swing foot is rising, falling, or at an apex the height
test excludes. The same code now reads both kinds of content.

Three further decisions, each forced by a measurement rather than chosen:

1. **The height band is a fraction of the joint's own vertical range in this clip (15%), not an
   absolute distance.** An absolute 0.06 found a *false* plant on `Walking`: the left foot dips to
   0.173 and hesitates at the bottom of its swing, 0.035 above the true plant at 0.138, and the
   band swallowed both — reporting two contacts where there is one, and a 0.200 s "cycle" on a
   1.033 s clip. A relative band is also the only scale-free choice: an absolute one means a
   different thing on a 1.66 m alien, a 0.11 m chick, and a rig authored in centimetres.

2. **A looping clip's contacts wrap.** `Running`'s right foot is planted from 0.633 s through
   0.033 s of the next lap. Read linearly that is two contacts at opposite ends of the clip, and a
   one-cycle clip then looks like a two-cycle one. A span may now record `start > end` and report
   its own duration across the seam.

3. **A looping clip with exactly one plant of the reference joint is cyclic, with a cycle equal to
   the clip's own length.** A one-cycle walk plants each foot once; requiring two plants to admit a
   cycle throws away the phase of precisely the clips phase matching exists for.

**Phase is not a walk cycle.** It is a monotone [0,1) coordinate anchored on the reference joint's
plants where there are any and on normalised time where there are not. A one-shot gets a phase and
says `cyclic == false` — `Landing` is exactly the clip a transition most wants a phase for, and a
system that only spoke about cycles would have nothing to say about it.

## The result on the real pack

| clip | left | right | duty L | duty R | cyclic | cycle |
|---|---|---|---:|---:|---|---:|
| `Walking` | 1 span `[0.300..0.667]` | 1 span `[0.833..0.067]` (wrapped) | 0.38 | 0.31 | yes | 1.033 |
| `Running` | 1 span `[0.300..0.367]` | 1 span `[0.633..0.033]` (wrapped) | 0.14 | 0.23 | yes | 0.700 |
| `Idle` | 1 span, whole clip | 1 span, whole clip | 1.00 | 1.00 | yes | 1.967 |

A walk with one alternating plant per foot per cycle, half a cycle apart; a run with short stances
and a flight phase between them; an idle with both feet down. All 26 clips now yield a phase.

## Rejected alternatives

* **Reconstruct the implied ground velocity from the feet and test against it.** It works — the
  stance foot's backward speed *is* the character's forward speed — but it needs a reliable
  stance to estimate from, which is what is being detected. Circular, and the vertical test needs
  none of it.
* **Author contacts by hand.** 26 clips × 2 feet on one character, and the whole point of the
  offline pipeline is that a 4-million-frame corpus (§8.3) cannot be hand-labelled. NSM's data cost
  is what that looks like at scale.
* **An absolute height threshold with a per-asset override.** A number in every scene file, wrong by
  default on every new asset, and silently wrong rather than loudly.
* **Requiring two plants for a cycle.** See Decision 3.

## Consequences

* Phase-aware transitions and contact-weighted foot IK have the metadata they need.
* `ClipAnalysis` records per-clip root travel and ground speed, so a motion pack can say which of
  its clips actually travel — the fact ADR-540 had to measure by hand.
* The offline pass is the only consumer; nothing here is on the frame path.

## Revisit triggers

* A travelling corpus arrives (100STYLE, §8.3). The horizontal arm will start firing, and its
  0.05 m/s travel threshold is the switch — it should be re-measured on that content, not assumed.
* A character with a non-vertical notion of "down" — a wall-walker, a character in a beam.
* Hand contacts: the height datum is "the lowest this joint got in this clip", which is right for a
  foot and wrong for a hand that only touches something once.
