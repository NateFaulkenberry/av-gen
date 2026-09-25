# ADR-709: The march's "whole-frame perturbation" was the harness, and a reciprocal

**Status:** Accepted
**Date:** 2026-09-24
**Resolves:** ADR-702 Consequences ("Not fixed here: the march's whole-frame perturbation when any
medium is live"); `docs/design/effect-library/tornado-fog-production-pass.md` §2 (fog, last row) and
§4 step 1.
**Implemented by:** `shaders/volume.wgsl` (`fs_composite`)
**Tests:** `tests/rendering/test_march_perturbation_gpu.cpp`

---

## Context

ADR-702 measured that switching the volumetric march on for any placed medium, even one entirely
behind the camera, moved up to ~70% of the frame by at most 7/255 per channel (mean 1.4). It left
the cause open and set a 24/765 "visible pixel" threshold in the coexistence tests to stay above it.
The plan ordered this diagnosed before the step redistribution (ADR-710) lands, so the two cannot be
confused. Measured on this branch, after ADR-707's ray fix.

## The measurement

`PROBE the march's whole-frame perturbation, bisected` (hidden, `[.probe]`), on the film ADR-702
used (Glowmere Valley 2 multicam, 192x108), a tornado placed behind the camera. Share of pixels that
differ, and the largest channel difference:

| arm, against the medium-off frame | t = 20, 150 m behind | t = 62, 150 m behind | t = 20, 2 km behind |
|---|---|---|---|
| the same scene drawn again (renderer determinism) | 0 | 0 | 0 |
| **ADR-702's arm**: tornado on, reached by `Engine::update` | 79.8%, 25 | 41.9%, 197 | 79.8%, 25 |
| the same scene with the media stripped before the renderer sees it | 79.9%, 25 | 41.9%, 197 | 79.9%, 25 |
| **medium still OFF**, two more `Engine::update`s at the same second | **91.5%, 48** | **56.5%, 228** | 91.5%, 48 |
| tornado on, both arms reached by **a seek** | **0.23%, 2** | 0.04%, 1 | 0.23%, 2 |
| the medium-off scene with only `mediumCount = 1` and an all-zero slot | 0.35%, 1 | 0.14%, 3 | 0.35%, 1 |
| ...with the march forced to ignore every medium | 0.35%, 1 | 0.14%, 3 | 0.35%, 1 |
| ...with `fs_composite` leaving an untouched pixel exact (this ADR) | **0** | **0** | **0** |

Two findings, neither of them the march.

**1. Almost all of it was the test harness.** Stripping the media from the "tornado" scene changed
nothing, so the difference was not in anything the renderer does with a medium. Then the control:
the medium switched off in *both* arms, two updates apart, differs *more* than the tornado arm did.
ADR-702's helper reaches every arm through `Engine::update(FrameTime{t, 1/60, ...})` at the same
second, and the engine advances the scene's content by `dt` on every update whether or not time
moved (the camera does not move: 0.0000 m). So every arm was a slightly later scene than the one
before, and the "perturbation" was the scene's own motion between them. Reached through a seek --
what every offline render does, and what `test_tornado_structure_gpu.cpp`'s rig already did --
two arms at one second are one scene.

**2. The small remainder was the composite.** This film's environment fog is off, so the volume
pass does not run until a medium exists. Then every pixel whose four march texels are "nothing
here" -- no light, transmittance exactly 1 -- was still normalised: `accum.a / weightSum`, the same
four products summed in the same order, which is 1 in exact arithmetic and 0.99999994 when the
compiler turns the division into a multiply by a reciprocal. `hdr * 0.99999994` rounds to a
different half-float in a fraction of pixels. Proven by elimination, not argued: `mediumCount = 1`
with an all-zero slot reproduces it, forcing the march to ignore every medium does not remove it,
and short-circuiting the untouched case does. Independent of where the medium is (150 m and 2 km
alike), which is why ADR-702 saw it "independent of where the medium is".

## Decision

`fs_composite` returns exactly `(0, 0, 0, 1)` -- a no-op under the `scatter + hdr * transmittance`
blend -- when all four texels it reads are exactly `(0, 0, 0, 1)`. That is a defect with a one-line
cause and a four-line fix, so it is fixed. Pixels any medium or fog reached are unchanged.

The harness is **not** changed. `test_effect_stack_gpu.cpp`'s arms and its 24/765 threshold are
ADR-702's acceptance evidence; rebasing them onto a seek would change what that evidence measured.
Its comment, which attributed the drift to the march, is corrected where the claim lives
(ADR-577's rule: a misattributed cause sends the next person to the wrong knob), and so is the
comment in `test_tornado_structure_gpu.cpp` that repeated it.

## Test

`a medium no camera ray reaches leaves the frame byte-identical` (`[gpu][volume][effects]`): the
film at t = 20 through a seek, with a tornado 2 km behind the camera switched off and on. Premise
checks that the medium-off frame ran no volume pass and the medium-on frame marched one medium;
then the two frames' hashes must match. **With the old composite restored it fails: 0.23% of
pixels, max 2 levels.**

## Consequences

- A scene whose only volumetric content is a placed medium (the Tree of Life, the tornado labs, the
  fog arms) is now untouched wherever no march texel saw anything. Everywhere else is unchanged.
- **Whether the engine should advance when `update` is called twice at one time is not decided
  here.** For a live engine `dt` is the step and advancing is correct; for an offline one it makes
  any harness that updates per arm measure scene motion. Recorded for whoever owns `Engine`; the
  safe pattern for a GPU arm comparison is `seekSeconds` + `tick` + `update`.
- The 24/765 threshold in `test_effect_stack_gpu.cpp` now guards against the harness's own drift,
  not the march. If that harness moves to a seek, the threshold can drop to "any byte".
