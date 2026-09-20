# ADR-374: The vortex is a funnel, and the camera has to be outside its mouth

- Status: Accepted (2026-09-19)
- Finishes ADR-371, which shipped the mechanism switched off because it was unfinished.

## The cost gate, answered — and the premise was wrong

ADR-371 left a gate: `volumeMaxDistance` defaults to 200 m, a vortex 400 m down is outside the
march, and raising it to 4 km "costs steps and must be measured before switching the vortex on".

Measured. 1920x1080, `--headless --frames 90`, `--bench-json`, **minima of three runs** (ADR-170),
through `tools/gpu-lock.sh`:

| arm | GPU min | volume pass |
|---|---|---|
| no vortex (pass off) | 7.995 ms | — |
| 1 km march, 32 steps | 14.615 ms | 7.274 ms |
| 1 km march, 48 steps | 14.549 ms | 7.209 ms |
| 4 km march, 48 steps | 12.911 ms | 5.636 ms |
| 4 km march, 96 steps | 13.042 ms | 6.226 ms |

**Step count is within noise** (32 vs 48 at 1 km: 14.615 against 14.549) and **4 km is cheaper than
1 km**. The cost is not march length and is not sample count — it is *how many pixels have non-zero
density and therefore evaluate three fBMs*. A near vortex covers more of the frame than a far one.

> **Correction, ADR-389 (2026-09-19).** The half-sentence "step count is within noise" is **no
> longer true and should not be quoted**. It has already misled one investigation today.
>
> Re-measured on the shipped scene at 1920×1080, `volume.march` alone, two runs per arm, GPU
> serialised:
>
> | steps | 32 | 48 | 96 |
> |---|---|---|---|
> | march | 4.78 ms | 8.20 ms | 17.5 ms |
>
> Superlinear. Ninety-six steps would spend the whole 16.7 ms frame budget on the march, so raising
> the step count is not available at Realtime.
>
> The claim was true when it was written, and the reason is the sentence beside it: cost is *how
> many samples land inside non-zero density*. At the time of measurement the funnel was newly cut
> out of a slab, most rays missed it, and the extra samples fell almost entirely in the region the
> 1e-6 early-out rejects — so doubling them cost nothing. The same early-out that made the
> measurement true is what makes it false now that the funnel fills the lower frame from the hero
> camera: the extra samples land *inside* it and each one evaluates three fBMs.
>
> The general fact survives and the number attached to it did not. That is the shape of ADR-385's
> stated reason that is not evidence, one ADR later and in an accepted document.

So the gate's premise is inverted: raising `volumeMaxDistance` is close to free, and the proposed
fallback of "bring the disc closer" is the **more** expensive direction, not the cheaper one.

### The optimisation that follows from that

If coverage is the cost, the fix is to stop evaluating noise where the answer is already zero. The
cheap masks — the dark centre, the rim, the distance from the funnel's surface — are computed first,
and the three fBMs are skipped when their product is below a threshold.

Threshold **1e-6**, chosen by measurement rather than by argument: at 1e-3 the saving is larger
(volume 5.636 → 3.670 ms) but the frame is **not** identical — 31 887 channels differ, 57 pixels by
more than 8 levels and 5 by more than 40, all of them isolated near-saturated specular highlights
where a hair of transmittance flips the tonemap. At 1e-6 the frame is **byte-identical** to the full
path and the volume pass still falls 5.636 → 4.588 ms. The exact one is worth the 1 ms.

That comparison needed its own control first: rendering the same configuration twice gave 0
differing channels, so the difference really was the threshold and not run-to-run variation.

## The shape: a funnel, not a slab

ADR-371's vortex was a flat slab, and from the hero camera — pitched 8.8° down with a 36° field —
a slab 700 m below is seen edge-on and reads as a band of haze. The brief's own diagram is a funnel
narrowing into a void, and a funnel has an inner wall a level camera can see down into. That is the
difference between "there is something below" and "the island is hanging over a hole".

`funnelDepth`, `throat` and `throatDensity`. Depth 0 keeps the slab, so the shape is a superset.

### Two bugs the band measurement found, which looking could not have

**The funnel extended upward.** `yn = clamp(-rel.y / depth, 0, 1)` is 0 for anything *above* the
mouth, so `throatFade` evaluated to 1 up there and the funnel became a full-radius cylinder at
`throatDensity` reaching into the sky. Every wide variant washed the top of the frame as badly as
the bottom, and it read as "too bright" rather than as "wrong shape". The diagnosis came from
splitting the vortex's contribution by frame band: a funnel that only descends cannot add +12
luminance to the sky *above* the island. With the fix, the same geometry gives **top −3.88, mid
+2.74, low +14.34** — negative at the top, because the void now occludes the starfield, which is
what a void should do.

**The camera has to be outside the mouth.** The hero camera is 232 m from the vortex axis. Every
mouth radius above that put the camera *inside* the funnel, which is why enlarging the disc — the
obvious response to "it is too subtle" — made it worse every time, filling the frame uniformly and
destroying §23's hierarchy. This is geometry, not taste, and it bounds the whole design: the mouth
must be narrower than the camera's distance to the axis.

The shipped mouth is **200 m**, about 2.7x the island's own radius, with the island sitting directly
over it — which is the brief's diagram, arrived at from the constraint rather than from the picture.

### And a third thing, worth stating because it looks like a defect and is not

At moderate emission the vortex's net contribution is **negative**: its extinction removes more
background starlight than its emission adds. An early tuning read as "the vortex is invisible" when
it was in fact working and subtractive. Raising emission is the answer, but the frame band split is
what distinguishes "not there" from "there and dark".

## Shipped

Mouth 200 m at y = −70, funnel depth 1500 m, throat 0.10, wall thickness 70 m, density 0.0013,
emission 0.040, contrast 3.6, filaments 2.2, inner void 0.24, swirl 6.5, rotation 0.028 rad/s,
breath 0.05 at 0.18 Hz. `volumeMaxDistance` 4000, `volumeSteps` 48.

Composition: **top −4.08, mid +1.47, low +10.78**, tree/lower-frame luminance ratio **1.54**. Dark
above with the stars visible, luminous swirl below, and the tree still the brightest and most
legible thing in frame, which is §23.

**Cost: 13.500 ms against the 7.995 ms baseline, +5.5 ms.** Higher than the 4 km table above because
this mouth is close to the camera and covers more near-field pixels at high density — the same
coverage rule, applied to the shipped geometry. 13.5 ms is 74 fps of GPU and is affordable; it is
also the single most expensive thing this branch has added and should be the first thing turned down
if the frame budget tightens. `scene/vortex/density` and the quality tier's
`volumeResolutionScale` are the two knobs for that.

## Consequences

- The hero camera is not the best camera for this effect and cannot be made so: §20's "if the camera
  moves lower, the vortex should reveal more depth" is literally true here, and a lower shot would
  show the funnel properly. That is an edit decision, and the film can cut to it.
- Vortex particles (Phase 12) and the tree-to-vortex flow (Phase 11) remain unbuilt.

## Revisit when

- The frame budget tightens, or a shot needs the camera below the mouth plane, where the throat
  becomes the subject and the tuning above will be wrong for it.
