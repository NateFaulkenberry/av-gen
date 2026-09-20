# ADR-382: The quality ladder, the song, and two views that would have found the bugs

- Status: Accepted (2026-09-19)
- Phases 15, 18 and 19 — the last of the brief. Extends ADR-139 (volume scalability),
  ADR-011 (modulation), ADR-374/380 (the measurements this ladder is built on).

## §18 — the quality ladder, and the lever nobody should reach for

`QualitySettings` already scaled the volume by resolution and by step count. It gains
`particleSpawnScale`: 0.25 at Preview, 0.6 at Realtime, 1.0 at High and Offline. **Spawn rate and
not capacity**, because changing capacity destroys and recreates the pool (ADR-015), so a tier
change would empty every particle system mid-shot.

The comment on it records the thing that is easy to get wrong, at the site rather than in this file:

> **The volume's step count is not the lever it looks like.** Measured on the cosmic vortex, minima
> of three runs at 1920x1080: a 1 km march at 32 steps is 14.615 ms and at 48 steps is 14.549 —
> inside the noise — while a **4 km march at 48 steps is 12.911, cheaper than the 1 km one**. The
> cost is how many pixels have non-zero density and therefore evaluate their noise, not how far or
> how finely the ray is marched.

Everybody reaches for the step count first. `volumeResolutionScale` is what moves the number.

**And the constraint §18 states, which these obey by construction:** a tier may scale resolution,
sample counts and particle counts, and may **not** remove an artistic control. Every field here is a
renderer setting; none of them is a parameter's visibility. A Preview that hides the vortex's colour
knobs is a different product, not a cheaper one.

### Measured, with the particle-free baseline subtracted

| tier | particles on screen | spawn scale |
|---|---|---|
| Preview | 52.6% of High | 0.25 |
| Realtime | 73.8% of High | 0.6 |
| High | 100% | 1.0 |

**The reduction is smaller than the scale**, and that is worth knowing before anyone relies on this
tier to buy a specific saving: at High these pools are near capacity with long lifetimes, so
lowering the spawn rate removes fewer live particles than proportionally. The lever is real and
monotonic; it is not linear.

Preview is 0.25 and deliberately not lower. The Tree of Life's motes take **30 to 50 seconds of
playback** to reach the vortex (ADR-380), so a tier that cut them hard would make a working effect
look broken to the person most likely to be previewing it.

## §15 — the song, through the modulation that already exists

Twelve routes on the shipped project, no state machine — which the spec asks for and which this
project already demonstrates, since the hueShift has ridden `state.progress` across the song from
the start.

Bass to the slow large things (vortex breathing and density, the tree's conduction); mid to the
middle of the ecosystem (turbulence, shimmer, leaf emission); treble to the small bright ones
(filaments, mote brightness); `beat.pulse` to the tree's energy; and `state.progress` to the
long-form arc — the same signal the hueShift rides, so the colour travel and the energy travel are
one gesture rather than two.

Every route resolves: no `unknown parameter` and no `not modulatable` in the load. Against a control
with only those twelve disabled, the frame differs by a mean of 9.9 to 11.1 luminance levels at
three points in the song, over 1.3 M pixels.

**What I could not show, and will not claim.** The long-form component is present but modest: the
whole-frame change between t = 12 s and t = 68 s is 40.61 with the routes on and 39.38 with them
off. Most of the song's visible evolution is still the hueShift. On a measure less dominated by a
global hue rotation — the lower frame's luminance over the tree's — the routes lift the vortex
consistently at all three times (+0.11), but that is the audio responding, and I did not isolate the
`state.progress` routes from the audio ones. The arc exists; its size is not established.

## §19 — two developer views

`--debug-draw wind` and `--debug-draw vortex`, on the existing developer surface rather than as
artist controls, which is what the spec asks for.

Wind draws an arrow per grid point from `wind::sampleWind` — the **same CPU function**
`shaders/wind.wgsl` transliterates, so a divergence between the two shows up here as arrows that
disagree with the foliage — coloured by strength so the regional variation and the travelling gust
fronts read as bands. Plus the first declared wind body's origin, height and radius, which are what
the deformation is keyed on and whose being wrong is invisible until the tree bends about the wrong
point. Vortex draws the mouth, the throat rings sampled down the funnel **by the same narrowing
`vortexShape` applies**, the depth, and tangents showing which way and how hard it turns.

These exist because of what this branch cost. Three of its real bugs were geometry errors that a
picture would have shown in a second and that no metric did: the funnel extending **upward** as a
full-radius cylinder, the camera sitting **inside** the mouth, and a per-metre conversion missing so
a term was two orders of magnitude out. None of those is visible in a luminance number, and two of
them took several render rounds to find.

Both draw **nothing** when the thing they describe is inactive, per this file's own rule that a
diagnostic showing the same picture whatever the state is worse than none.

## Consequences

- `particleSpawnScale` is applied in `ParticleRenderer::update`, so it affects emission from the
  frame the tier changes. Particles already alive are not removed, which is the right behaviour
  mid-shot and means a tier change settles over about one lifetime.
- The twelve routes ship in the project, not in engine defaults. A second scene wanting this arc
  copies them; there is no `defaultTreeRoutes` helper, and there probably should be if a third
  scene wants it.
