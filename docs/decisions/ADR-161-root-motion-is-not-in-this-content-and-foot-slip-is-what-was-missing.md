# ADR-161: Root motion is not in this content, and foot slip is the thing that was missing

**Status:** Accepted
**Date:** 2026-09-14

## The request

From the animation review: *"No root motion — foot speed and travel speed only agree by
coincidence."* The proposal was to add root motion, so that the animation drives the travel and the
two agree by construction.

## What the measurement said

Root motion means extracting the root joint's translation from a clip and using it to move the body.
So the first question is whether the clips carry any.

The wanderer's three clips were read straight out of `assets/imported/alien.gltf` and their root
(`mixamorig:Hips`) translation channels integrated:

| clip | duration | xz span | **net xz displacement** |
|---|---:|---:|---:|
| Idle | 3.633 s | 1.018 | **0.0002** |
| Walk | 1.100 s | 1.555 | **0.0000** |
| Run  | 0.900 s | 3.163 | **0.0000** |

These are Mixamo **in-place** takes. The hips sway — that is the span — and return to exactly where
they started. There is no root motion in this content to extract.

**So root motion is not implemented.** Building the system would produce a feature with nothing in
the project to drive it, tested only against a synthetic clip authored to exercise it, waiting for an
asset that may never arrive. If an asset with real root translation is ever imported, this ADR is the
place to start; the measurement above is the check that says whether it has any.

## What the actual defect was

The speed a walk cycle *means* is not in an in-place file. It has to be authored, and
`GaitSettings::walkSpeed` / `runSpeed` is where this engine authors it. There is also already a
mechanism for reconciling it with the body: `matchRate` scales clip playback by
`speed / authoredSpeed`, clamped — a walk clip authored at 1.6 m/s played at 2.0 m/s runs 1.25×, and
the feet keep up.

It is fully wired — `Gait::playbackRate` → `Locomotion::playbackRate` → `setNodeAnimation` → the
player's speed — and **no scene in the repository has ever switched it on.** Glowmere's wanderer
authored no `gait` block at all, so it took the defaults with `matchRate = false`, while its explore
behaviour travels between 2.5 and 5 m/s. The feet were doing a 4.0 m/s run at 2.5 m/s of ground.

The gap was never the mechanism. It was that **nothing compared the two numbers.** A behaviour's
travel speed and a gait's stride speed live in different blocks, are set at different times by
different people, and the only symptom of a mismatch is an animation that looks wrong in a way
nobody can name.

## The decision

`Gait::footSlip(settings, activity, speed)` — the ratio between the ground a body covers and the
stride its clip was authored for, *after* rate matching has had its say. 1 means the clip is playing
at the speed it was authored for; 3 means three metres of ground per metre of stride.

An entity whose slip exceeds 1.5× either way warns **once per entity per session**, naming the travel
speed, the clip, the authored speed, the ratio, and whether rate matching is off or on-and-saturated.
Once rather than per frame, because a body whose gait cannot represent its speed is a persistent
authoring fault rather than an event, and a warning printed sixty times a second is one people filter
rather than fix.

A rate matcher that has saturated at its clamp is the case worth catching and the reason `footSlip`
is computed after matching rather than before: the setting is on, it looks handled, and the feet are
still wrong.

Non-locomotor activities return 1 — standing and turning have no stride to be out of step with. A
silent answer has to mean "no opinion" rather than "fine", or the check becomes a source of false
confidence.

## What changed in the shipped scene

Glowmere's wanderer now authors `"gait": {"matchRate": true, "rateMin": 0.55, "rateMax": 1.9}`. The
warning fired on the unmodified scene at 2.51 m/s against a run clip authored for 4.00 (0.6× slip,
rate matching off) and is silent after.

## Evidence

`tests/unit/test_entity_action.cpp` covers matching off (raw ratio), non-locomotor activities
(silent), matching on and inside the clamp (absorbed), and matching on and **saturated** (still
slipping). The warning fires on the unmodified shipped scene and clears after the gait block.

## What this does not fix

It measures the mismatch; it does not remove it. A body whose behaviour genuinely wants to travel
faster than any clamp can cover still needs either a faster clip, a wider clamp, or a slower
behaviour — and which of those is right is an art decision. The engine's job here is to stop the
mismatch being invisible.
