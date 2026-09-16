# ADR-254: A benchmark scene that cannot show the artifact is not a benchmark

**Status:** Accepted
**Date:** 2026-09-16
**Context:** Quality Lab Phase 5 — authoring the first two benchmark scenes
**Follows:** ADR-182 (a probe that cannot fail proves nothing), ADR-243 (the detector and the eye
disagree), ADR-253 (the residual knows where the pixel came from)
**Amends:** `docs/quality-lab/benchmark-scenes.md` §3 and §4

## Context

`benchmark-scenes.md` puts the **aliasing** scene first — the artifact ADR-243's reviewer actually
reported, the one the metrics disagreed about, and the one with no existing coverage — and a
**spline-dolly temporal scene** second, because ADR-243 ends by naming a moving camera as the axis
nobody has looked at.

Both were authored. Both were **vacuous on the first attempt**, in two different ways, and neither
failure was visible in anything except a check the harness rules already required. That is what this
ADR is about, because the scenes are cheap and the two rules are not.

## Failure 1: the spline camera looks where it is going

`benchmark-scenes.md` §4.2 says *"Free or Spline cameras only"* — the legacy orbit mode integrates
`orbitSpeed · dt` and is path-dependent — and §3 recommends a **spline dolly** specifically.

A spline camera takes its position from the spline and its **target from a point `lookAhead` units
further along the same spline** (`Composition::evaluateMainCamera`). It always looks where it is
going.

The motion a temporal benchmark needs is **lateral**, because lateral motion is what produces
parallax and parallax is the only thing that disoccludes anything. A push-in down a corridor
disoccludes almost nothing, and a disocclusion arm with nothing to exclude is vacuous. So the scene
was authored with a lateral track — and the camera dutifully aimed sideways, off the subject
entirely, at empty background.

The frame it produced had `rms_contrast 0.0000`: a flat field, one constant colour, a valid PNG of
nothing.

**It was caught by the sequence hashes.** `reference-rendering.md` §3.4 rule 5 says two arms expected
to differ must hash differently, and the candidate and its `--supersample 2.0` reference came back
with the same hash — `a3e03269a9a0de34` — which for a supersampled render is impossible unless there
is nothing in frame to sample. No metric had been computed yet.

**Decision: the temporal benchmark uses a FREE camera with a keyframed position and target**, and
`benchmark-scenes.md` §4.2 is amended to say why. A free camera is equally path-independent — the
timeline is a pure function of time, which is the actual property §4.2 is protecting — and it can
look at the subject while moving across it. Orbit stays refused for the reason it was always refused.
A spline camera remains the right tool for a flythrough, where looking along the path *is* the shot.

## Failure 2: an anti-aliasing benchmark with no anti-aliasing

`metrics.md` §4.2 makes ADR-243's three arms a mandatory second gate — a metric set that does not rank
FXAA-off worse and supersample-2× better than the baseline is wrong. Reproducing them on the new
scene means `--disable fxaa`.

`--disable fxaa` logged **"this is a DIAGNOSTIC render — phase(s) disabled: fxaa"** and produced a
sequence hash **byte-identical to the baseline**.

The cause is not in the flag. `scene/post_settings.hpp` declares `float antialias = 0.0f;` with the
comment *"0 skips the pass entirely"*, and the scene never set `post/output/antialias`. FXAA was
never running, so switching it off changed nothing.

This is ADR-182's exact shape, one layer up from where ADR-182 found it: *a measurement whose null
result is indistinguishable from a broken instrument*. Read naively it is a finding — "FXAA does not
affect this scene" — and it is the reassuring one. ADR-182's fix made `--disable` *say* it applies;
this failure is the flag working perfectly on a subsystem the content had already switched off.

**Decision: a benchmark project pins every parameter its arms will toggle**, explicitly, rather than
inheriting a default. `examples/quality/aliasing.json` sets `post/output/antialias: 0.75` (the value
`world/art_direction.hpp` uses) alongside the bloom, vignette, grain and chromatic-aberration pins it
already had. With it set, the three arms produce three distinct hashes, and the FXAA-off arm
reproduces the no-antialias image bit for bit — which is the positive control that the arm is now
doing what its name says.

## The scenes

**`examples/quality/aliasing`** — a corridor of 2 cm fence slats at 22 cm pitch receding 34 m, a
24×64 tile floor at 24 cm pitch, a rotated array of thin diagonal plates, a ring of 3.5 cm emissive
spheres, and one large smooth orb. Static free camera, 1280×720/30, every seed pinned, bounded range.

Two properties of it are deliberate. **The fence crosses the sampling limit inside one frame** — near
the camera the slats are several pixels wide and resolve, at the far end they are well under a pixel —
because an aliasing arm whose subject is *uniformly* sub-pixel measures total signal loss rather than
aliasing, which is the mistake the distortion ladder's own first aliasing arm made. And the **orb is
a control**: it is large, smooth and fully resolved, so a metric that moves on this scene's arms must
not be moving there.

Rendered, the slats visibly break into dashes in the middle distance. That is the same artifact
ADR-243's reviewer described unprompted on the hero mushroom's gill filaments — *"look a little like
they're breaking apart or half rendered"* — reproduced in a scene that takes 0.3 seconds to render.

**`examples/quality/aliasing-dolly`** — the same subject, derived from it rather than copied, with the
camera tracking 4.4 m laterally in two seconds on a linear interpolation. Linear on purpose: an eased
dolly has near-zero velocity at each end, and a residual measured where nothing moved is a
measurement of the static case wearing a moving camera's name.

## Consequences

**Both of `benchmark-scenes.md` §4's new rules were bought with a vacuous render**, and both renders
were caught by checks the document already required — the sequence-hash rule and ADR-182's
change-the-output rule. The rules paid for themselves on their first use, which is worth recording
because the alternative history is a quality report full of confident numbers about an empty frame.

**The Quality Lab's own scenes are now the regression case for its own harness rules.** A future
scene that repeats either mistake fails in the same place.

**What this does not say.** It does not say the spline camera is wrong — it does what it is
documented to do, and for a flythrough that is the behaviour you want. It says a benchmark's camera
has a requirement a shot's camera does not: it must keep the subject in frame while moving relative
to it, and only one of the three placement modes can do both.
