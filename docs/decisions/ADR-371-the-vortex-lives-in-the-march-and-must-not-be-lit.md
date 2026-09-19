# ADR-371: The vortex lives in the march, and it must not be lit

- Status: Accepted (2026-09-19), **art direction unfinished — see Consequences**
- Extends ADR-032/139/140 (the volumetric atmosphere). Related: ADR-358 §6, which refused the
  volumetric beam for the reason this ADR ran head-first into.

## Problem

The brief's cosmic vortex: an enormous swirling structure **below** the island, seen in perspective,
occluded by the rock, gaining depth as the camera drops. Explicitly not a skybox and not a backdrop.

## Decisions

### 1. It is a term in the volumetric march, not a new pass

`volume.wgsl` already marches world space, stops at the depth buffer, and composites depth-aware
from a half-resolution target (ADR-139). That is world-space placement, perspective, occlusion,
parallax and a scalable quality knob — every property the brief asks for — already built and
already tested. A second raymarcher would be a second copy of all of it, drifting.

The model is the polar construction the brief describes: radius and angle about a world centre, the
angle sheared by radius so the structure winds, resampled into a cartesian frame that winds *with*
it, then fBM at three spatial **and three temporal** rates. The three temporal rates are the part
that matters: one rate makes everything move together and the eye reads that instantly as a
screensaver.

`radius = 0` is the gate and the default, and every function returns before doing work.

### 2. `VolumeRenderer::enabled` had to learn about it

It was `volumeDensity > 0`. The Tree of Life scene runs with **no fog at all**, so gating the vortex
behind the fog's density would have made it unreachable in the one scene it exists for — the exact
shape of ADR-360's defect, and avoidable because ADR-360 had just been written.

### 3. **The vortex contributes extinction and emission and does not scatter**

This is the load-bearing line, and the first version did not have it.

With the vortex folded into the march's single `density`, the `scattering` term multiplied it by
`volumeScattering` and `inScatterAt` summed the scene's directional lights into it — including the
celestial key at **intensity 22**. Over a 2.6 km march that accumulated so much in-scattered key
light that the frame came back at **mean luminance 131 of 255 with the vortex's own emission set to
zero**. A sane frame for this scene is around 30.

That is precisely the flat haze ADR-358 §6 predicted when it refused to build a volumetric beam
without shadow-atlas sampling — the same defect reached from the other direction, and it would have
been invisible as a *cause* if the emission had been tuned first, because turning emission down
would have made the picture darker without ever making it right.

So the fog scatters the scene's lights; the vortex emits its own. A nebula four hundred metres below
an island is not lit by that island's key light, which is both physically right and what keeps the
effect out of the scene's lighting entirely.

| arm | mean luminance |
|---|---|
| vortex density 0.0015, **emission 0**, shared scattering | 131.11 |
| the same with emission swept ×16 to 0.0008 | 131.99 |
| after separating: emission 0 | **35.90** |
| after separating: emission 0.0008 | 39.97 |

The middle row is the diagnosis in one number: sweeping emission over four steps moved the frame by
0.9 luminance levels out of 131. Whatever was making it white, it was not the thing I was tuning.

### 4. Units are per metre, and that bit twice

`density` is extinction per metre and `emission` is emissive density per metre, both integrated over
a step length that is `volumeMaxDistance / steps` — 27 m at the settings this needs. The first
values were written as though they were unit-scale field amplitudes: `density 0.9`, `emission 2.2`.
Over a 1.3 km structure that is an optical depth in the hundreds. The working values are
`density ≈ 0.0012` and `emission ≈ 0.005`, three orders of magnitude down.

Also: `volumeMaxDistance` defaults to **200 m**. A vortex 400 m below a camera 219 m out is entirely
outside it. A scene that wants one has to raise it, and raising it makes each step coarser, so
`volumeSteps` goes up with it.

## The measurement

`tests/rendering/test_vortex_gpu.cpp`, 2 cases, 22 assertions:

- `Vortex{}.radius == 0` and `active()` false — the gate is the default, asserted so re-defaulting
  it has to break a test.
- A fog-only scene renders identically twice, and `enabled()` is false with no fog and no vortex but
  **true** with no fog and a vortex.
- The vortex draws something; **dropping the camera below the disc changes the picture**, which is
  what separates a world-space phenomenon from a backdrop and is §20 stated as a test; and the same
  camera move *without* the vortex changes fewer bytes, so the change is attributable to it rather
  than to the rest of the scene moving.

## Consequences

- **It ships switched off (`radius` 0), and that is not the same decision as the wind's.** The wind
  shipped off while *finished*, which was wrong and has been corrected. This ships off because it is
  *unfinished*: the mechanism is right and measured, but at the hero camera the disc is nearly
  edge-on and reads as a band of atmospheric depth rather than as a vortex. Turning it on in that
  state would repeat the falling-leaf blizzard — a change the owner can see and would not want.
  The tuned arms are committed as `_ca-vg-*` so the next pass starts from them rather than from
  zero.
- **The next pass is art, not engineering.** Either the disc comes much closer and larger so its
  structure is legible from the hero camera, or the hero camera drops — and §20 says the camera
  dropping should reveal it, so that is worth trying first.
- Raising `volumeMaxDistance` to 4 km costs march steps. Not yet measured against the frame budget;
  it must be before this is switched on.
- Vortex particles (the brief's Phase 12) and the flow that carries tree particles into the vortex
  (Phase 11) are not built.

## Revisit when

- The vortex is switched on, at which point teaching the march to sample the shadow atlas becomes
  worth its cost and buys ADR-358's crepuscular ray in the same change — the trigger named there.
