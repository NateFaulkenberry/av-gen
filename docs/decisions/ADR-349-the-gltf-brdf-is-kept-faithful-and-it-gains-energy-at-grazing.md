# ADR-349: The glTF BRDF is kept faithful, and it gains 68% of its energy at grazing

**Status:** Accepted (a finding, and a decision the owner has now made)
**Date:** 2026-09-18
**Relates to:** ADR-348 (the path tracer), spec sections 18 and 19
**Note:** main is at 339 and two other branches hold 340; expect this to be renumbered at merge.

## What was checked

Phase 2 implements the glTF 2.0 metallic-roughness BSDF: a Lambertian diffuse lobe and a
Cook-Torrance specular lobe with GGX/Trowbridge-Reitz, Smith height-correlated visibility and
Schlick Fresnel, combined exactly as the glTF specification's Appendix B combines them:

```
F        = f0 + (1 - f0) * (1 - |v.h|)^5
diffuse  = (1 - F) * baseColor/pi * (1 - metallic)
specular = F * D * Vis
f        = diffuse + specular
```

The white-furnace test asks whether the directional albedo -- the integral of `f * cos` over the
hemisphere -- ever exceeds 1. A BRDF that reflects more than it receives brightens every bounce, and
in a path tracer that compounds: an interior scene glows and nothing in the image says why.

## The finding

**Metals conserve energy. Dielectrics do not, and the gain is large.**

Measured with the BSDF's own sampler over 120,000 samples per cell, white base colour:

| metallic | base | roughness | worst directional albedo | at |
|---:|---:|---:|---:|---|
| 1.0 | 1.0 | 0.1 | 0.9988 | n.v 0.95 |
| 1.0 | 1.0 | 0.5 | 0.9355 | n.v 0.05 |
| 0.0 | 1.0 | 0.1 | **1.6758** | n.v 0.05 |
| 0.0 | 1.0 | 0.5 | **1.1325** | n.v 0.05 |
| 0.0 | 0.8 | 0.1 | **1.4883** | n.v 0.05 |
| 0.0 | 0.5 | 0.1 | **1.2071** | n.v 0.05 |
| 0.0 | 0.5 | 0.5 | 0.6639 | n.v 0.05 |

**This is not Monte Carlo noise, and that was checked rather than assumed.** Two independent
estimators -- cosine-hemisphere sampling and the BSDF's own importance sampler, which have entirely
different variance -- agree to four decimal places:

| roughness | n.v | cosine estimator | BSDF estimator |
|---:|---:|---:|---:|
| 0.1 | 0.95 | 1.00029 | 0.99963 |
| 0.1 | 0.5 | 1.02198 | 1.02531 |
| 0.1 | 0.2 | 1.29301 | 1.29818 |
| 0.8 | 0.5 | 0.98664 | 0.98650 |

## Why it happens, and why it is not a bug in this code

**The specular lobe is correct.** With `metallic = 1` and a white base, `f0` is 1, Fresnel is
identically 1, and the integral is pure `D * Vis`. It never exceeds 1 and it *falls* as roughness
rises, which is the signature of correct single-scattering GGX losing energy to the multiple-scatter
term this model does not have. If GGX, Smith or the `1/(4 n.v n.l)` folding were wrong, this row
would be the one that broke.

**The fault is the combination.** The diffuse lobe is suppressed by `(1 - F(v.h))`. For a diffuse
direction `h` is near the bisector of two widely separated vectors, so `v.h` is usually close to 1
and `F` is close to `f0 = 0.04` -- the diffuse lobe keeps about 96% of its energy no matter what the
viewing angle is. Meanwhile the *specular* lobe's directional albedo at grazing is large, because
Fresnel approaches 1 there. Nothing tells the diffuse lobe how much the specular lobe already took.
A physically-correct coupling would suppress the diffuse by the specular's **directional albedo**
E(n.v), not by `F(v.h)`.

## The owner's decision, 2026-09-18

Put to the owner with the measurements and the three options below. Their answer: **"Leave it
faithful I guess."**

So this is settled rather than deferred. The model stays as the glTF specification defines it, and
the grazing gain is a known, measured, accepted property of this renderer rather than an open
question. Two things follow from the *"I guess"*, which is not an enthusiastic yes:

* **The band stays, and stays tight.** It is the only thing that will report this getting worse.
* **The caveat is printed at render startup**, in the section 55 capability report, not only here.
  An ADR is no use to somebody who does not already suspect the BRDF; the log is what they read.
  `CapabilityReport::caveats` exists for this and this is its first entry.

**Do not build the compensation, even behind a flag.** If the owner revisits it, it is a focused
change with its own renders. A dormant, unexercised code path is worse than none.

## Decision: keep it faithful, pin it, do not quietly fix it

Spec section 19 is explicit: *"glTF metallic-roughness as the BRDF baseline. Not an arbitrary
'pretty' BRDF."* Adding an energy-compensation term would make this renderer disagree with every
other glTF renderer, including AV Gen's own rasteriser, on assets authored against the spec. That is
a deliberate art-direction decision with consequences for every existing Glowmere material, and it
is not one to take silently inside a Phase 2 commit.

So the model stays as the specification writes it, and the behaviour is **pinned as a band** in
`tests/unit/test_pathtrace_bsdf.cpp`:

* metals conserve at every roughness and angle (`<= 1.001`), with a live-arm check that the number
  is not zero;
* ordinary dielectrics conserve head-on and at ordinary roughness;
* the white smooth dielectric at grazing is asserted to be **greater than 1.5 and less than 1.8**.

That band is the important part. A floor alone would stay green if the gain doubled; a ceiling alone
would stay green if somebody silently swapped in a different BRDF. Bands, not floors (ADR-182).

## What this costs in practice, stated plainly

The gain needs a **bright, smooth, non-metallic** surface seen at a **glancing angle** to matter.
Ordinary materials are safe: base 0.5 at roughness 0.5 comes out at 0.66. But a white smooth floor
or a pale wall viewed along its length is exactly the grazing case, and with `maxDepth` above 1 the
error compounds per bounce. **If an interior render ever looks inexplicably bright, this is the first
thing to check.**

## Revisit when

Indirect lighting matters -- realistically when Phase 3's MIS and Russian roulette make deep paths
routine, or when the first Glowmere interior is rendered. The options then, in increasing order of
deviation: a multiple-scattering compensation term for the specular lobe only (recovers the energy
GGX loses, does not address the diffuse coupling); suppressing the diffuse lobe by the specular's
directional albedo via a small precomputed table (fixes this finding, deviates from the spec); or
accepting it. Each is a decision for the owner, with an image, not for a commit.
