# ADR-185: A continuous shot carries its aim across the cut, not just its eye

**Status:** Accepted
**Date:** 2026-09-14

## The report

The auto-director's continuous mode "just bakes a different film" -- choosing it gave a sequence of
shots, not one unbroken move.

## What was actually wrong

`directFromStructure`'s continuous branch already joined the *eye* path: each shot started where the
previous one ended, and `easeIn`/`easeOut` were switched off so the speed did not dip at the seam.
What it did not join was the **aim**. Every shot computed its target from its own hero, so at the
moment the shots changed over, the point the camera was looking at jumped from one hero to the next
in a single frame.

Measured over sixty seconds of the Glowmere Valley 2 track: the worst single-frame eye step was
0.582 m -- continuous, as designed -- and the worst single-frame aim step was **278.03 m**. A pan of
278 metres in one sixtieth of a second is not a camera move; it is a cut, which is exactly what the
mode exists not to produce.

## The fix

`Shot::startTarget`: where a shot's aim *begins*, the mirror of the `startPosition` the eye path
already had. The continuous branch fills it with the previous shot's final target, and `targetAt`
eases from it into the shot's own aim over half a second with a smoothstep.

`targetWithoutStart` exists so that the previous shot's *final* aim can be asked for without
recursing into its own start -- a shot's ending aim is its own, whatever it began from.

After: worst aim step **1.5497 m**, eye step unchanged at 0.582 m.

## What this is not

It is not an aim-follow change. The probe clears `setAimFollow({})` and re-measures; the number is
the same 1.5497 m either way, so the discontinuity was in the shot list and not in the hero tracking
that runs on top of it.
