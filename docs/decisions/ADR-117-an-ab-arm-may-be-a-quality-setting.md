# ADR-117: an A/B arm may be a quality setting, not only a missing pass

Status: accepted

## Context

`--ab <phase>` (ADR-113) runs a baseline and an arm interleaved in one process, which the audit
established is the only comparison this machine admits: two Glowmere figures taken in different
sessions differ by 28% with nothing to explain it (§3.3), and a null A/B on Constellation reports a
37% within-session spread (§3.5).

An arm was a `PassToggles` boolean -- a pass removed from the frame. That covers every *subsystem*
question and none of the *setting* questions, and Phase B's two largest questions are setting
questions:

* **What did ADR-112 buy?** Its rule is `QualitySettings::shadowTexelTarget`, and zero restores the
  pre-ADR-112 range. ADR-112 measured its own effect at 4.2% and then flagged the measurement as
  weak, in its own ADR, because *"the two arms were run in blocks rather than interleaved run-by-run,
  because switching arms needs a rebuild"*. The rebuild was needed because the harness could not
  express the arm.
* **What does the contact march cost?** `contactShadows`. Same shape, same problem.

An agent facing that wall does what ADR-112 did: builds twice and compares blocks, in two sessions,
against a rule that says not to. The instrument shaped the evidence.

## Decision

A `QualityArm` is a named mutation of `QualitySettings`, applied per block by the benchmark
schedule, selectable by the same `--ab <phase>` that selects a pass arm. Four exist:

| arm | what it sets | why it is here |
| --- | --- | --- |
| `shadowrange` | `shadowTexelTarget = 0` | the pre-ADR-112 range: three scene radii |
| `contact` | `contactShadows = false`, `contactSteps = 0` | the one shadow term the mask does not cover |
| `pcss` | `softShadows = false` | PCF instead of PCSS for the key light |
| `maskfull` | `shadowMaskScale = 1.0` | the mask per pixel, which is what High and Offline already do |

Three things about the design are deliberate.

**It is a separate concept from `PassArm`, not an extension of it.** A pass arm removes work the
frame asked for and produces a frame nobody should ship. Every quality arm above selects a value
some tier in `QualitySettings::forTier` already sets, so a quality arm may honestly be read as
*"what would this tier choice cost"* and a pass arm may not. Collapsing them would lose that.

**The setting is re-applied at the top of every block, not once.** `QualitySettings` is renderer
state, and the `SYM-TERRAIN-1` investigation produced four wrong attributions from a probe that
established the state it was measuring once and then rendered other frames between measurements.

**The arm's name is what the summary reports.** The summary line used to print `"no-" + arm`, which
for `shadowrange` says the opposite of what ran.

## What it bought immediately

`--ab shadowrange`, Glowmere 1280x800, three interleaved pairs of 120 frames, baseline blocks
varying 1.48% GPU. Reported separately and at length in ADR-120; the headline is that ADR-112 is
worth **6.9% of GPU frame time**, not the 4.2% its own weaker measurement reported, and that the
arm's shadow draw count moves 34 -> 99 in every arm block and back, which is the evidence that the
state really is being re-established per block rather than assumed.

## Consequences

* An arm name is now ambiguous between two tables. `setQualityArm` is tried first and the error
  message lists both sets. Two arms may not share a name; nothing enforces that beyond review.
* A quality arm cannot be combined with `--disable`, which takes pass arms only. Not needed yet.
* The four arms are not a closed set, but they are not free either: each is a claim that the setting
  is worth pricing, and an arm nobody reads is an arm that rots.
