# ADR-182: A diagnostic arm that cannot fail is worse than no arm

**Status:** Accepted
**Date:** 2026-09-14

## What happened

Attributing temporal instability in Glowmere meant rendering the same sequence with subsystems
switched off and comparing. Four arms — `--disable particles`, `water`, `volume`, `ao` — over a
`--render` sequence came back **byte-for-byte identical to the baseline**, all four.

Read naively, that is a finding: none of those subsystems contributes to the flicker. It is not a
finding. `--disable` never reached the offline renderer at all.

The cause is the one this project has already recorded once: **the offline engine builds its own
renderer**, so a toggle set on the interactive one is discarded. `--debug-target` had exactly this
defect and was fixed by making it *say* it does not apply. `--disable` had it and said nothing.

## Why this is worse than an unimplemented flag

A flag that does nothing and admits it costs an experiment. A flag that does nothing and returns a
clean null result costs a *conclusion* — and the conclusion it hands you is the reassuring one. The
arm reports that the subsystem you suspected is innocent, with an image-identical diff to prove it.

This is the same shape as the four tests found during the renderer upgrade that compared two paths
through the same defect and therefore could not fail, and the same shape as the probe that measured a
culled entity for three runs while three different navigation fixes were evaluated against it. The
common form: **a measurement whose null result is indistinguishable from a broken instrument.**

## The decision

`RenderSettings::disablePasses` carries the arms into the render job, applied after the tier for the
same reason ADR-147 put the tier there — the tier is the policy, and this is a removal from whatever
the policy chose. An unknown phase name fails the render rather than being skipped.

The job logs **"this is a DIAGNOSTIC render"** at warning level when any phase is off. A sequence
rendered without its water is not a deliverable, and the file on disk does not know that.

## The check that made it visible, and the rule

The arms were only caught because the frames were compared for byte-identity before their statistics
were read. That is the generalisable part, and it is cheap:

> **An attribution arm must be shown to change the output before its null result means anything.**

It caught a second vacuous arm in the same session that was *not* a harness bug: setting the water's
`sparkle` to zero also produced an identical frame, because the sparkle is band-passed in screen
space and contributes nothing at that camera distance. Same symptom, different cause — and without
the identity check both would have been filed as "this subsystem is innocent".

After the fix, `water` and `volume` change the image and `particles` still does not, which is now a
statement about the scene rather than about the harness.

## Evidence

The same four arms, before and after, on the same project and camera. Before: four of four identical.
After: `water` and `volume` differ, and the attribution they then support is that water accounts for
**57%** of flickering pixels in that view (3.116% → 1.332%) while bloom accounts for about 17% and
volumetrics for none. None of those numbers was obtainable while the arms were vacuous.
