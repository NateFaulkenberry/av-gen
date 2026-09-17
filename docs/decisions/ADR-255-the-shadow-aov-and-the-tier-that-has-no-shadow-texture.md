# ADR-255: The shadow AOV, and the tier that has no shadow texture to export

**Status:** Accepted — design settled, implementation not started
**Date:** 2026-09-16
**Context:** The Quality Lab's `shadowStability` has reported `unavailable: no shadow AOV` since
Phase 4. Asked whether the engine should get one, the answer was **yes**.
**Follows:** ADR-242 (a target nobody reads is not a feature), ADR-250 (the instrument is not the
engine), ADR-251 (an extent is not a size), ADR-182 (a probe that cannot fail proves nothing),
ADR-087 (the half-resolution shadow mask)

## Context

`artifacts/masks.hpp` says what is missing and why, in the file itself:

> *What is deliberately absent: shadow. There is no shadow AOV. ADR-242 lists it among the views not
> added because nothing had asked for one, and the Quality Lab is the first consumer with a reason to
> ask. Approximating it from "regions the lighting model says are shadowed" is refused — it would be
> a number whose name promised more than it knew.*

That reads like a small gap: the engine already exports five AOVs, adding a sixth should be a texture
and a case in a switch. **It is not, and the reason is worth an ADR rather than a commit message.**

## The finding: the obvious implementation is empty in exactly the configuration that needs it

The engine *does* have a screen-space shadow term as its own render target. ADR-087's
`ShadowMaskRenderer` writes, per leading directional light, "exactly the combined visibility the lit
pass would otherwise compute per pixel" — the cascaded PCSS lookup minned with the contact march.
Reading `output()` back looks like the whole job.

`render_quality.hpp` says otherwise:

```cpp
case QualityTier::High:
    q.shadowMaskScale = 1.0f; // the reference live picture: the term at full resolution
case QualityTier::Offline:
    q.shadowMaskScale = 1.0f;
```

and `ShadowMaskRenderer::update` reports and encodes **nothing** when `shadowMaskScale >= 1`: at
those tiers the lit pass computes the term itself, per pixel, inline, and the mask target is never
built. `output()` falls back to a 1×1 white texel so the binding stays valid.

**The Quality Lab renders its candidate at `--tier offline`.** So a shadow AOV built on the mask
would be a valid file, of the right size, in the right format, containing a constant — in precisely
the configuration the Lab exists to measure, and only in that configuration. At `realtime` it would
work perfectly, which is the worst possible failure shape: it would pass every test written against
the tier a developer runs interactively.

This is ADR-251's lesson arriving from the other direction. There, `settings_.width` was a true fact
about the output and a false fact about the texture. Here, "the shadow mask is the shadow term" is a
true fact about three tiers and a false fact about the two that matter.

## The two honest implementations

### A. A sixth scene target

`SceneOut` in `shaders/common.wgsl` writes five colour targets and **every pipeline in the scene pass
writes all five** — `common`, `pbr`, `procedural`, `water`, `atmosphere`, `skybox`, `grid`, plus
particles and the SDF raymarcher. The C++ side is centralised behind `kSceneTargetCount` and
`fillSceneTargets`, so that half is genuinely a one-line change.

Faithful by construction: it is the lit pass's own value, at the lit pass's own resolution, aligned
with every other AOV pixel for pixel.

The cost is that **every frame everyone ever renders pays for it**. WebGPU fixes a pipeline's colour
targets at creation, so there is no conditional sixth attachment without a second full set of
pipelines. ADR-250's whole argument is that the instrument is not the engine, and a permanent
bandwidth cost on every realtime frame to serve one tool is the clearest possible violation of it.
The five existing targets each have a *runtime* consumer — velocity for TAA, emission for bloom, ids
for picking. A shadow target would have exactly one consumer and it lives in `tools/`.

### B. A dedicated full-resolution shadow pass, encoded only when asked for

`shaders/shadow_mask.wgsl` already computes the term from inputs that exist at every tier: the linear
depth the prepass resolved, and the shadow atlas the cascade passes built. Running it at **full**
resolution, gated on `--aov shadow`, costs nothing when nobody asks, is pixel-aligned with the other
AOVs, and is independent of `shadowMaskScale` entirely.

Its weakness is the mirror of A's strength: it **recomputes** the term rather than capturing it. The
ADR-087 header claims the mask is "exactly" what the lit pass would compute, and at the offline tier
the lit pass runs 24 PCF taps and 24 PCSS blocker taps against a 4096 atlas. Whether "exactly"
survives those settings is a claim, not a guarantee.

## Decision

**Take B, and test the claim rather than inheriting it.**

The fidelity question is not a reason to prefer A; it is a measurement, and it is the kind this
repository already knows how to take. Before the AOV is believed:

1. Render one frame with the mask **forced on at full resolution** and one with it off, same scene,
   same seed, under `tools/gpu-lock.sh`.
2. Compare the lit output. If the mask is what the lit pass would have computed, those frames agree
   to within the resolve; if they do not, the AOV is a second opinion rather than a capture and the
   report must say so in the metric's own `reason` field.
3. The positive control that makes either answer mean something (ADR-182): a third arm with the
   shadow atlas resolution changed, which **must** move both. An agreement measured by an instrument
   that cannot disagree is not agreement.

That experiment is cheap, it is the first thing to do, and its result decides whether
`shadowStability` ships as a measurement or as an approximation that names itself one.

**`validate()` gains a clause either way.** `--aov shadow` on a scene with no directional light must
refuse, mirroring ADR-242: a mask over a scene lit only by point lights is a constant, and a constant
that looks like a render is the exact failure both those ADRs were written about.

## Consequences

**The estimate changed by an order of magnitude, and the user should know before the work starts.**
"Expose an existing texture" was the shape of the request that was approved. The shape of the work is
a new render pass, a shader variant, plumbing, a validator clause, and a fidelity experiment that
could still conclude the number has to be labelled an approximation.

**Nothing in `src/` gets a sixth permanent cost.** The realtime frame is unchanged, which is what
ADR-250 asks for and what option A could not offer.

**What this does not say.** It does not say ADR-087's mask is wrong or approximate — it is doing
exactly what it was built for, at the tiers it was built for. It says the tier the Quality Lab
measures is the one tier that does not use it, and that a shadow AOV taken from it would have been
correct everywhere except where it was needed.
