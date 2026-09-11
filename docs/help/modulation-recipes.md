---
id: modulation/recipes
title: Modulation Recipes
category: Modulation
summary: Worked examples — bass-reactive glow, a beat-synchronised camera, and a gated kick.
order: 33
audience: beginner
tags: recipes, examples, tutorial, glow, camera, kick
keywords: how do i make something glow with the bass; beat synchronised camera; make it pulse on the kick; audio reactive example; worked example
related: modulation/overview, modulation/routes, audio/analysis, rendering/emission-and-bloom
features: panel.modulation
---

# Modulation Recipes

Three things people usually want first, each done the way the engine actually works.

## 1. Bass-reactive glow

**Goal:** an object brightens with the low end.

1. Load audio and press **Play**, so you can see the signal move.
2. Open **Modulation ▸ Routes** and add a route.
3. Source `audio.bass`, target the object's emission parameter — for a material that is
   `material/<name>/emissionIntensity`; for the built-in orb it is `orb/emissive`.
4. Set `op` to `add` and `amount` to taste. Start around 1.0.
5. Set `attack ms` to about 20 and `decay ms` to about 200. This is what makes it feel percussive
   rather than wobbly.

> [!WARNING]
> **Raising emission does not light anything nearby.** Emission makes a surface bright; it does not
> make it a light source. If you want the glow to illuminate its surroundings you need an actual
> light in the same place. See [Emission, bloom and glow](help://rendering/emission-and-bloom) —
> this is the most common surprise in AV Gen.

To make the glow *bloom* as well as brighten, `post/bloom/intensity` and `post/bloom/threshold`
control how much brightness spills. A route from `audio.rms` to `post/bloom/intensity` already
exists by default.

## 2. Beat-synchronised camera movement

**Goal:** the camera breathes in time.

1. Add a route with source **`beat.phase`** — not `audio.beatPhase`, which only updates about 94
   times a second and will step visibly.
2. Target `camera/distance` (or the parameter your scene uses for the camera's standoff).
3. Set `bipolar` on, so the phase sweeps −1 to 1 across each beat rather than sawtoothing from 0.
4. Set `op` to `add` and keep `amount` small — a fraction of the camera's distance.
5. Set `curve` to `scurve` to soften the turnaround.

For a kick on the beat rather than a continuous sweep, use `beat.pulse` with `envelope` set to
`linear fall` and `fall/s` around 4.

> [!NOTE]
> If the camera is being driven by the timeline — after **Camera ▸ Direct to Music**, or from a
> sequence — your route writes a value the timeline replaces on the next frame and the camera will
> not move. Use **Camera ▸ Hand Camera Back to the Viewport** first.

## 3. A kick, not every hit

**Goal:** react to the drum and not to the hi-hat.

`audio.onset` is computed over the whole spectrum, so a cymbal fires it as readily as a kick does.
There is no per-band onset in AV Gen. Two honest approaches:

**Gate the onset against the bass.** Route `audio.onset` to your target with `envelope` set to
`peak hold`, then in the project file set the route's `threshold` to a level and `thresholdMode` to
`gate`. Below that level the route produces nothing.

**Use the band alone.** Route `audio.bass` with a short attack and a long decay. This is less
precise about the exact moment of the hit but needs no gating, and for most visual effects the
difference is invisible.

Remember that `audio.bass` is auto-gained over about four seconds, so a track with a sustained bass
line will hold it near 1 and give you very little movement. In that case the onset with a gate is
the better route.

## A note on ranges

`audio.onsetStrength` is declared 0 to 4, not 0 to 1. If you route it directly at amount 1 into a
parameter whose useful range is 0 to 1, it will sit clamped at the top for most of the track. Either
use `audio.onset` (which is 0 to 1 and carries velocity) or scale the amount down to about 0.25.
