# ADR-086: The key light's shadow map is looked up once per four pixels; its contact march is not

Status: accepted
Date: 2026-09-11

## Context

Phase 3 (ADR-085) established that Glowmere's scene pass is **fragment-bound** and said where the
fragments go. At the resolution the editor actually renders — the dock centre at the display's
backing scale, 2880x1166 for a 1440x900-point window — `directLighting` is 81% of the pass, of
which the key light's cascaded PCSS lookup is 34% and the screen-space contact marches are 19%.
Phase 3's LOD work took 41% off the pass at the 720x450 benchmark and 7% at the editor's canvas,
because the two ends of that range are bound by different things. What the editor is short of is
fragment throughput.

The cascade lookup is a good candidate for evaluating at reduced resolution: a penumbra is a
low-frequency signal, and the ambient-occlusion pass already proves the shape works — GTAO runs at
half resolution with a depth-aware upsample and costs 0.13 ms.

## Decision

**A half-resolution pass computes the shadow-map term of the leading directional lights, and the
lit pass upsamples it bilaterally.** `shaders/shadow_mask.wgsl` runs once over the linear depth the
prepass already resolved, reconstructs world position and normal from it, and calls the same
`shadowFactor` the lit pass would have called — the cascaded PCSS lookup with its ADR-081
crossfade for the key light, plain PCF for any other directional light. The result goes to one
channel of an RGBA16Float target (three lights fit, alongside the view depth the upsample rejects
on); `shadowMaskLookup` in `shaders/shadows.wgsl` reads four texels back.

`lighting.wgsl` and `shadows.wgsl` are the same code split in two so that the mask pass and the lit
pass cannot drift apart: they call one `shadowFactor`, not two implementations of it.

**The contact march stays at full resolution, and that is a measurement.** See below.

**The mask is consulted only where it has something to say about this fragment's surface.** Two
gates, both load-bearing:

- `ShadeContext::maskable` is false for a *blended* surface. The depth prepass draws opaque and
  alpha-masked geometry only, so the depth under a blended fragment belongs to whatever is behind
  it and the mask describes the wrong surface entirely. This is a per-draw uniform branch on the
  alpha mode, so it costs nothing.
- The upsample rejects a texel whose view depth disagrees with the fragment's by more than about
  7% relative, and reports `valid = false` when no tap in the footprint agrees. That covers
  disocclusions and anything else the prepass did not see. A fragment the mask cannot describe
  takes the full-resolution path, unchanged.

Local lights, spot lights and any directional light past the third are not masked and shade exactly
as before.

**Alpha-tested foliage keeps its silhouette and loses shadow resolution inside it.** The cutout is
the material's own alpha test in the lit pass and is untouched. What changes is that the shadow
*within* a leaf is resolved at half resolution, which is where almost all of the remaining image
difference is.

**`shadowMaskScale >= 1` means "build no mask"**, and the High and Offline tiers sit there. An
offline render therefore computes the term per pixel and is the frame this optimisation did not
touch; a test asserts it rather than trusting it.

## What the reconstructed normal cost, and the bug it exposed

The mask has no normal+roughness target to read — it runs before the scene pass writes one — so it
reconstructs a geometric normal from the depth buffer, the way `gtao.wgsl` does. The first version
copied GTAO's orientation step too:

```wgsl
let unit = normalize(cross(dx, dy));
return select(unit, -unit, unit.z < 0.0); // face the camera
```

**On a surface the camera sees nearly edge-on that test is a coin flip**, because the normal's
view-space z is within rounding of zero. It landed wrong on Glowmere's ground: the reconstructed
normal pointed straight down, the shadow lookup's normal offset — which is metres wide on a far
cascade — pushed its sample point *into* the ground, and the frame filled with bands of acne
aligned to the shadow map's texel grid. Because the bands were smooth and followed the terrain,
they read as art rather than as a bug; they were found by writing the mask to the screen and
comparing it against the same term computed at full resolution.

The orientation now comes from the winding of the two screen-space differences, which cannot be
ambiguous: `dx` runs along +x in view space and `dy` along -y, so `cross(dy, dx)` is the
camera-facing side at any angle. With that fixed the mask's term is indistinguishable from the
full-resolution one on open ground, and the frame's pixel difference fell from 15.2% to 8.4%.

**`gtao.wgsl` had the identical defect and is fixed the same way.** It matters far less there —
5.4% of Glowmere's pixels move and 99.5% of those by one or two of 255, because GTAO weights each
slice by how much of the normal lies in it and a flipped grazing normal barely lies in any — but a
wrong normal is not left standing because it happens to be survivable.

## What the contact march cost at half resolution, and why it is still full resolution

Masking the contact march as well was the original plan and is measured, not assumed. With the
normal bug fixed, at 2880x1166:

| what the mask carries | pixels differing from the unmasked frame |
|---|---:|
| the shadow-map term only | **8.4%** |
| the shadow-map term and the contact march | 30.2% |

Glowmere's ground is a field of grass and reed cards. A march evaluated once per 2x2 applies
whatever it hit to all four pixels, so every contact shadow doubles in width, and dense ground
cover goes black. A penumbra survives being computed at half resolution; a screen-space contact
shadow is exactly the high-frequency signal that does not. ADR-034 introduced the march to make
small parts sit against each other, and that is precisely the detail half resolution destroys.

The march is 19% of the pass at the editor's canvas and stays there.

## What else was measured and rejected

**More taps in the mask.** The mask runs at a quarter of the pixels, so it can afford a finer
filter, and a twelve-tap PCF quantises visibility to twelve levels. Twenty-four taps (with
`pcf` rotated a further half-spacing per revolution, so the second sixteen land between the first
sixteen rather than on top of them) measured *no better* — 8.50% of the frame differing from the
reference against 8.42% at twelve — for 1.0 ms at 2880x1166. The banding that motivated it was the
normal bug, not the filter. The tap count follows the tier.

The rotation change to `pcf` is kept anyway, because it is a real fix for a case the tiers already
reach: the Offline tier asks for 24 taps and the disc has 16 points, so taps 16..23 were landing on
points 0..7 again and quietly over-weighting half the filter.

**Correcting the upsample's half-texel placement.** A mask texel's centre falls exactly between two
full-resolution texels; the pass picks one, so its sample arguably sits a quarter of a mask texel
from where a bilinear filter assumes. Shifting the lookup to compensate measured *worse in both
directions* — 14.7% and 15.2% of the frame differing from the unmasked reference against 8.4%
unshifted. The reasoning is wrong somewhere; the plain filter is what stands.

**Sampling the dither on the mask's own grid** rather than on the full-resolution pixel it stands
on: no measurable difference (15.18% against 15.15%, before the normal fix). **Widening the normal
reconstruction stencil** to 2 and 4 texels: no measurable difference (15.14% both). **Removing the
bilateral depth rejection entirely**: worse (16.1%), and it did not touch the banding either.

## Consequences

Min of 5 interleaved runs, `--tier realtime`, headless, Glowmere, on a machine shared with other
agents. The arms are `--disable shadowmask` against the default, interleaved round-robin; the
absolute level drifted about 10% between measurement sessions and the ratios did not.

| canvas | pixels | scene pass off -> on | mask pass | GPU frame off -> on |
|---|---:|---:|---:|---:|
| 720x450 | 0.32 MP | 13.04 -> **11.27** (-13.6%) | 0.20 | 14.75 -> 13.17 (-10.7%) |
| 1440x900 | 1.30 MP | 19.40 -> **14.94** (-23.0%) | 0.59 | 22.35 -> 18.48 (-17.3%) |
| 2880x1166 (editor, 1440x900 pt) | 3.36 MP | 42.80 -> **31.39** (-26.7%) | 1.25 | 47.91 -> 37.68 (-21.4%) |
| 2466x1766 (editor, 1920x1200 pt) | 4.36 MP | 39.58 -> **28.44** (-28.1%) | 1.44 | 45.42 -> 35.72 (-21.4%) |

The shape of that table is the finding, and it is ADR-085's shape inverted: the LOD work bought 41%
at the benchmark resolution and 7% at the editor's, and this buys 14% at the benchmark resolution
and 27% at the editor's. Below the crossover the invocation count is geometry-floored and a
per-pixel saving has little to work on; above it, it is the whole story. The two phases are
complementary rather than alternatives, and together they take Glowmere's editor canvas from the
46.0 ms GPU frame ADR-085 measured before it to 37.7 ms.

Visually, at 2880x1166, **8.4% of pixels differ by more than 0 and 1.0% by more than 8 of 255**.
The difference is concentrated on tree canopies and foliage silhouettes — a leaf a few pixels wide
takes its shadow term from a half-resolution texel — plus the edges of cast shadows, which move by
up to a pixel. Open ground, the terrain, the water and every hero are unchanged. Composition,
exposure and colour are unchanged.

**Lights that march but cannot cast.** Glowmere's `skyfill` and `elder-practical` are
`castsShadow: false` and still run a twelve-step contact march per fragment, because
`PunctualLight::contactShadow` defaults to true and no rig field set it. `RigLight::contactShadow`
now exists and serialises, defaulting to **true** so no existing rig changes. Turning both off in
Glowmere's rig is a 1.55 ms saving at 2880x1166 and a visible change to the image; ADR-034 put the
march on every light deliberately, so this stays an art decision expressed in the rig rather than
one the renderer makes.

**Not fixed here, stated plainly.** The mask's normal is geometric, reconstructed from depth; the
lit pass shades with the material's normal. On Glowmere's stylized path those differ only where a
surface is faceted, and the residual was measured at 5.6% of pixels before the half-resolution
sampling was added on top. Writing a geometric normal from the depth prepass would remove it and
would cost a full-resolution RG16Float attachment plus a fragment shader in three renderers; it was
not worth it against an 8.4% total.

## Also in this phase: the sloppy simplifier now reaches its target

ADR-085 ended by naming this: `meshopt_simplifySloppy`, asked for 35% of `CommonTree_1`, returned
**7.6%**, so LOD0 -> LOD1 at 28 px of screen radius was a thirteen-fold drop rather than a
threefold one. It quantises the mesh onto a grid and the triangle count it returns is a step
function of the grid it chose, so a single call lands wherever the steps happen to fall.

`LodChainSettings::sloppyIterations` (6) now **bisects the request** rather than asking once. `lo`
is a request known to come back at or under the target, `hi` one known to come back over it, and
each step halves the gap; the candidate nearest the target from below is kept, and the search stops
early once a level lands within 70% of its target. The answer is monotonic in the request but
nowhere near proportional to it, which is why bisecting the request and not extrapolating from the
result is the shape that works. Glowmere's ladders:

| layer | before | after | asked for |
|---|---|---|---|
| `valley_canopy` | 8% / 8% / 2% | **34% / 13% / 2%** | 35 / 12 / 4 |
| `valley_pines` | 11% / 2% / 1% | **34% / 11% / 2%** | 35 / 12 / 4 |
| `valley_bushes` | 9% / 9% / 3% | **27% / 9% / 9%** | 35 / 12 / 4 |

**It costs, and the cost is the point.** Min of 4 interleaved runs, two binaries, same shaders:

| canvas | submitted tris | scene pass | GPU frame |
|---|---:|---:|---:|
| 720x450 | 201,174 -> 235,761 (+17.2%) | 11.21 -> 11.34 (+0.13) | 12.98 -> 13.17 (+0.19) |
| 2880x1166 | 694,977 -> 767,961 (+10.5%) | 29.23 -> **31.06** (+1.83) | 35.59 -> 37.62 (+2.03) |

That is a 1.83 ms regression at the editor canvas, landed deliberately, because the millisecond was
being bought by drawing a thinner world than the ladder asked for and reporting that the ladder
worked. §72 covers exactly this. 2.8% of the frame's pixels change, all of them in the mid-ground
band where LOD1 instances live, and the change is trees with the foliage mass the ratio specified
rather than canopies eaten down to a tenth of it.

The other half of ADR-085's note — whether ADR-082's per-instance LOD spread makes the transition
acceptable — is **still unverified**, because nobody has yet watched a moving camera cross the
threshold and this run was headless too. What has changed is that the ladder's ratios are now a real
control: 35% means 35%, so re-calibrating them is a decision that can be made and measured, which
it could not be while the simplifier silently returned a fifth of what it was asked for.

## Revisit triggers

- A world whose key light is a spot rather than a directional light gets nothing from this; the
  mask covers directional lights only.
- A scene with heavy blended geometry pays for both paths on those fragments.
- If the depth prepass ever writes a normal target for another reason, the mask should read it.
