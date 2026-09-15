# ADR-227: The blocker search gets its own budget, and the lane it needed

**Status:** Accepted
**Date:** 2026-09-15

## The defect

`QualitySettings::pcssBlockerTaps` was read by nothing. Four tier tables set it — 6 at Preview, 12
at Realtime, 16 at High, 24 at Offline — and `shaders/shadows.wgsl`'s PCSS blocker search took
`shadowPcfTaps` instead, through `ShadowUniforms::info.z`.

The §15/§16 parity audit (`docs/renderer-level3/01-assessment.md`) found it by grepping every
`QualitySettings` field for a reader outside its own header, recorded it, and did not fix it, for a
reason it stated plainly: *"every lane of `info` and `splits` is already taken, so wiring it is a
uniform-layout change rather than a line."*

That is a true statement about the cost and it is also the whole reason the field survived so long.
A field nobody can wire in one line is a field that stays unwired.

## The decision

`ShadowUniforms` gains a fourth `vec4`, `info2`, and grows from 672 to 688 bytes. `info2.x` is the
blocker-search tap count; `yzw` are free and deliberately named nothing.

Two counts rather than one, because they are two costs and two pictures. The blocker search is
uninterpolated `textureLoad`s over a fixed radius — ADR-111 measured it as the largest single
contributor to the shadow mask's residual — while the filter is hardware comparison samples over a
radius the search chose. A tier that wants a cheap search and a wide filter could not say so.

**One meaning for the number in both layers.** A fallback was written and removed: the shader read
`info2.x == 0` as "the lane was never written, take the filter's count", which gave 0 a second
meaning that the C++ — which clamps to 1..32 before writing — can never produce. Two readings of one
lane in two layers is exactly how `info.z` came to serve two tap counts, and repeating it inside the
fix would have been absurd. The shader now clamps `info2.x` the way `shadowTaps` clamps `info.z`, and
a request of 0 is a request below the floor.

## What it moves

The High tier is the only one whose two counts differ (20 filter, 16 blocker), so it is the only
tier whose picture changes at all. On the reference soft-shadow scene — a 1.0-softness directional
key over a box nine units above the floor, 192×192 — the blocker search dropping from 20 taps to 16
moves **14 pixels of 36,864**, worst channel delta 13.

That is the change, stated as a number. Every other tier sets the two equal and is byte-identical.

## Evidence

`tests/rendering/test_shadows_gpu.cpp`, *the PCSS blocker search takes the tap count the tier asked
it for*. The arm is shown to change the output before anything else is claimed (ADR-182): at the
extremes of what the field can express, 1 tap against 32, **927 of 36,864 pixels differ, worst 40**.
The extremes rather than two adjacent tiers for the reason the SDF probe in the same audit gives —
adjacent tier values may agree about a scene, and "no difference" read from two values that agree
cannot be told from "the field is not wired".

Three controls around it: the same blocker count twice is byte-identical, so the difference is the
count and not the frame; 0 and 1 are the same frame, which pins the clamp; and 0 is *not* the same
frame as the filter's 16, which pins the absence of the fallback.

The negative control is the defect reproduced. With `pcss` fed `taps` for its search instead of
`shadowBlockerTaps()` and nothing else changed, moving the field from 1 to 32 changes **zero
bytes** — `0/36864 pixels differ, worst 0`.

## What this does not do

The other half of the audit's item 10 — **the raymarched SDF's shadow-map quad** — is untouched and
still recorded. `SdfRenderer::update` computes each object's screen-space rect from the camera's
view-projection and the shadow pass reuses it against the light's, so a raymarched SDF casts no
shadow at all; `avgen_render_tests "[.probe][sdf]"` passes when that is gone. It is not fixed here
because the fix is a per-view rect or a full-screen quad in the shadow pass and that carries an
unmeasured cost — a full shadow-map quad marched per SDF per cascade — which is a decision that
wants a number before it wants a patch. No shipped example scene uses a raymarched SDF, so nothing
is currently wrong on screen because of it.
