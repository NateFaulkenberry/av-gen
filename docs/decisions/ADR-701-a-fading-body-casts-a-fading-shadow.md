# ADR-701: A fading body casts a fading shadow, by stochastic transparency in the depth pass

**Status:** Accepted
**Date:** 2026-09-22
**Related:** ADR-385 (the whole-node opacity multiplier, and the alpha-mode promotion that makes it
visible), ADR-034 (the cascaded shadow maps), ADR-046 (the second caster pass), ADR-087 (the shadow
mask), ADR-182 (a probe that cannot fail proves nothing)
**Implemented by:** `fs_depth` in `shaders/pbr.wgsl` (and therefore `pbr_skinned.wgsl`, which
includes it), `rendering::casterEligibility`, `SceneRenderer::renderFrame`'s bucket loop
**Tests:** `tests/rendering/test_abduction_fade_gpu.cpp`, `tests/unit/test_abduction_fade.cpp`

---

## Context

The owner, on the UFO abduction in `glowmere-valley-2-multicam`: *"there's also an interesting bug
where the shadow of the animal will pop out of existence before the animal does"*.

It did, and the two halves of the engine that produced it were each doing what they were told.

ADR-385 fades an abducted animal out over the last 1.2 s of its 4.6 s lift by driving
`nodes/<animal>/opacity`, and it has to do a second thing to make that number mean anything:
`pbr_shade.wgsl` reads `let alpha = select(1.0, baseColor.a, alphaMode > 1.5)`, so an OPAQUE
material's alpha is discarded outright and every farm GLB in the repository is authored OPAQUE. So
`Composition::update` **promotes** a node under 1.0 opacity to `AlphaMode::Blend` for exactly the
frames it is fading, and puts the authored mode back afterwards.

`SceneRenderer` sorted every drawable entity into one of a few buckets, and only the opaque one
reached the shadow pass:

```cpp
} else if (entity.material.alphaMode == scene::AlphaMode::Blend) {
    blended.push_back(*item);           // and nothing else
} else {
    opaque.push_back(*item);
    if (entity.castsShadow) { shadowCasters.push_back(*item); }
}
```

`casterEligibility` -- the Shadow Lab's own verdict function -- said the same thing in words: *"a
blended surface casting a hard silhouette is the bug that flag was added to avoid"*. Which was a
correct reading of a world where "blended" meant a pane of glass or a leaf card, and stopped being
correct the day a fade could promote anything to it.

**So the act that makes the fade visible is the act that deletes the shadow, on the frame the fade
starts.** Measured on the shipped film, at the project's own render block (1920x1080, tier offline,
2x supersample, limits unlimited), through the film's own camera:

| t (s) | animal opacity | `shadows.entityDraws` |
| --- | --- | --- |
| 13.683 | 1.000 | 31 |
| 13.700 | 0.999 | 31 |
| **13.717** | **0.998** | **30** |
| 13.800 | 0.973 | 30 |
| 14.850 | 0.000 | 30 |

The shadow left at an opacity of **0.998**, and the animal was not gone until 14.85 s: **1.13 s of
the 1.2 s fade** with a lit animal hanging in a beam and no shadow under it. Over the first three
lifts of the film, 0 of 70 fading frames per lift had the animal as a caster.

## Decision

**A blended body casts a shadow in proportion to its alpha.** Two changes, and neither is a
threshold:

1. **`fs_depth` gets a BLEND arm: stochastic transparency by ordered dither.** A depth map stores
   one occluder per texel and has nowhere to put "half blocked", so the only honest way for a
   caster to throw a partial shadow is to claim a *fraction of the texels* it covers. A Bayer 4x4
   matrix indexed by the depth target's own pixel decides which fraction; the 16-tap rotated
   Poisson disc in `shadows.wgsl` averages it back into a smooth term. At alpha 1 nothing is
   discarded and the shadow is the opaque one; at 0 everything is.

   **Ordered and not hashed, deliberately.** Whichever texels a caster claims has to be the same
   every time the same frame is drawn, or an offline render stops reproducing -- which this project
   checks by hashing captured frames. A Bayer matrix is a pure function of the texel. A hash of the
   world position is not, once the caster moves.

2. **`SceneRenderer` offers blended entities to the shadow pass, and asks `casterEligibility`
   whether to.** Not a second copy of the rule written out at the call site: `shadow_math.hpp`
   exists precisely because "two copies of a six-line frustum test in two files is how a
   diagnostic and the pass it describes come to disagree about one entity". `casterEligibility`
   loses the blanket exclusion and keeps a narrower one -- a body at **zero** opacity is still not
   a caster, which is right twice over, since there is nothing to cast and the dither would discard
   every fragment of it anyway.

Grid and Water keep their exclusions unchanged: a wireframe is not a surface, and water's
translucency is ADR-099's own subject.

## Consequences

**What the frame gains.** On the synthetic floor-and-box rig, sweeping one box's opacity with
everything else held (the open floor reads 0.8220 on every arm, which is the control that the
exposure did not move):

| box opacity | floor under it | the box itself |
| --- | --- | --- |
| opaque | 0.6064 | 0.7678 |
| 1.00 (blend) | 0.6064 | 0.7678 |
| 0.75 | 0.7034 | 0.7563 |
| 0.50 | 0.7566 | 0.7210 |
| 0.25 | 0.7941 | 0.6272 |
| 0.00 | 0.8200 | 0.0177 |

Before this change that first column read 0.8201 -- the open floor -- at **every** opacity from 1.00
down, including 1.00. A blended body cast nothing, ever.

The reading at 0.50 is 70% of the way from shadowed to open rather than 50%, and all of that is the
sRGB in the number: half the texels buys half the *direct radiance*, and the measurement is a
tone-mapped luminance. Half of a nonlinear scale is not the scale of a half. The test asserts the
thing a threshold could never produce -- a reading genuinely between the two ends -- rather than a
midpoint the encoding does not promise.

**What else in the shipped film changes, and it is small.** The multicam draws four blended
entities before anything fades: the alien visors (`tide/Head_Helmet`, `sage/Head_Brain`,
`ember/Head_Mask`, `vane/Head_Mask`), at opacity 0.444, all four with `castsShadow` already true.
They now cast a 44% shadow where they cast none, so the aliens' shadows stop having a hole where
their heads are. Caster counts on the abduction frames rose from 44 to 47-48 and
`shadows.entityDraws` from 31 to 32.

**What this does not fix, and was reported alongside it.** The other half of the owner's report was
that the animals "are not fading out at the top of the animation". Measured, they are: see
`tests/unit/test_abduction_fade.cpp` and the note at the head of it. All ten lifts of the 226 s
film reach zero opacity on the animal's *own* meshes, the seeked path reproduces the played one
exactly, and a GPU render at the project's own settings shows the dissolve. The shadow leaving at
99.8% opacity is the one thing that was genuinely popping at the top of the lift, and it is what
this ADR is about.

**Cost.** One extra draw per blended caster per shadow view, and a 16-entry array lookup per
fragment of it in a pass that had no fragment work. On the film that is one animal and four visors.

**The alternative that was rejected.** Keeping the caster until its opacity crosses a threshold --
0.5, say -- is one line and no shader change, and it moves the pop rather than removing it: the
shadow would then vanish at 0.5 opacity instead of 0.998, which is still a body visibly there with
no shadow. The owner asked for a shadow that fades with its caster, and a depth map can give one.
