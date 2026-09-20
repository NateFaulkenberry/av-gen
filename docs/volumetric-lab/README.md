# The Volumetric / Atmosphere Lab

Lab #9 of the Engineering Lab Suite (ADR-261, `docs/engineering-labs.md`). Registered in
`src/labs/lab.cpp`; fixture `examples/labs/volumetric-atmosphere-lab.scene.json`; cases
`examples/labs/volumetric/cases.json`, reachable as `avgen --lab-case volumetric:<n>`.

**The question**: *what is the air doing between the camera and the subject?*

**What it owns**: the volumetric march, its resolution and step scaling, the depth-aware composite,
and the atmospheric effects.

**What it does not own**: the bloom that the in-scatter later feeds. That is the HDR Lab. The two
meet in the HDR target — this pass adds into it, the bright pass reads it — and the boundary is
`--disable volume`, which is also this lab's most useful arm.

---

## 1. The pass, in two halves

`rendering::VolumeRenderer` (ADR-032), encoded after the lit pass and before the post chain.

1. **The march** — `shaders/volume.wgsl`, at `QualitySettings::volumeResolutionScale` of the scene
   resolution, into this renderer's own RGBA16F target: `rgb` is in-scattered radiance, `a` is
   transmittance. It reads the scene depth, so the fog is occluded by geometry.
2. **The composite** — full resolution, depth-aware upsample, blended `src One, dst SrcAlpha`, so
   `hdr = scatter + hdr * transmittance`.

Splitting those two costs was ADR-140, and it is the reason `VolumeStats` has `marchMs` and
`compositeMs` rather than one number: the march is O(pixels × steps) and scales with the resolution
scale; the composite is O(full-res pixels) and does not. Only splitting them makes any volumetric
trade decidable.

`VolumeStats::marchWidth` / `marchHeight` report the march target's real size, so a scale that was
**clamped away by a small viewport** is distinguishable from one that was applied. That distinction
is why those fields exist and it is what case 5 reads.

**Off is free.** `enabled()` is false when `volumeDensity <= 0` and no vortex asks otherwise;
`update()` then allocates nothing and no pass is encoded. Case 4 asserts that the un-run pass and
the disabled pass produce the same frame at the byte — two different routes to "the volume did not
run" that disagreed would mean one of them is not what it says.

Note the overload: `enabled(scene)` rather than `enabled(environment)` is the one that answers
correctly for a scene with a vortex and no fog. ADR-387 is the record of what the other one cost.

## 2. The density, term by term

```
density = volumeDensity * heightFalloff(y) * (1 + noise) * densityField(p)
```

Three of those four are **off in the fixture**, deliberately and not by omission. Each is
multiplicative, so a fixture carrying them cannot state the density at a point, and a lab whose
fixture cannot state its own input measures nothing. The arms that want them switch them on through
`scene/volume*` and read against the fixture as the zero.

Scattering, absorption and the Henyey–Greenstein anisotropy sit on top; anisotropy is 0 here so the
phase function is isotropic and a reading does not depend on where the light is.

## 3. The fixture, and its own control

`examples/labs/volumetric-atmosphere-lab.scene.json`. Five identical unlit white slabs at
**4, 12, 24, 40 and 60 m**, 2.4 × 6 m each, same material, in a homogeneous volume of density 0.02.

The only thing that differs between them is how much air is in front of them, which is exactly the
quantity the march integrates. They are five samples of one exponential.

**The near slab is the control.** At 4 m the volume should barely touch it. An arm in which the near
slab moves as much as the far one is measuring something other than distance — an ambient term, an
exposure change, a composite that is not depth aware — and the ladder it produced is not a
transmittance ladder.

The slabs are **unlit** (`pbr_shade.wgsl`'s `object.flags.z` branch), so a slab's radiance before
the volume is the number written in the file. A fixture whose subject came out of a BRDF would make
every reading a joint measurement of the Lighting Lab's code and this one's — which is the argument
the HDR Lab's fixture makes for the same choice.

No sky, no IBL, black background: every pixel not on a slab is fog against nothing, so the
transmittance of the empty frame is readable at the edges. And no vortex: ADR-371/388's hero effect
has its own parity test and would dominate every reading here.

## 4. The instruments, and the one it deliberately does not have

- `--disable volume` — the boundary. Case 3.
- `--aov depth` — the distance every reading is taken against, in metres.
- `--debug-target` — the march target itself, before the composite.
- `VolumeStats` — `steps`, `resolutionScale`, `marchWidth`/`marchHeight`, `marchMs`,
  `compositeMs`, `glowSystems`.
- `VolumeRenderer::target()` — the half-res march target, for a test that wants the two halves
  separately.

**The overlay profile is empty, and stays empty.** Every question here is about a value at a pixel,
and the march is additive into the same HDR target — so an overlay would not sit beside the
measurement, it would be inside it. The HDR Lab's profile is empty for the same reason one stage
later; the Rendering Lab's is empty because an overlay in a frame it grades is a defect it would
then report as an artifact.

## 5. Findings

Nothing yet. This lab is newly built and its first job is the ladder in case 2, which is a
measurement rather than a defect.

Two things are named as the next arms rather than run:

- **The step count against a non-uniform density** (case 6). A homogeneous medium is the easy case
  for a raymarch: the integral is analytic and any step count lands near it. Switch `volumeNoise` on
  and the same 32 steps are a different question, and an under-sampled march shows as banding
  rather than as bias. ADR-393 measured exactly that failure one system along — the cosmic vortex's
  grain at 32 samples over four kilometres — and the finding is about sample *spacing* against
  feature size, which applies here the moment the density stops being uniform.
- **The volume pass and the shadow atlas.** `volume.wgsl` does not sample it, so a directional key
  in-scatters uniformly through the marched volume and a "beam" would be flat haze. ADR-360
  re-affirmed that decision and named the trigger to revisit: once the volume pass is carrying a
  hero effect, teaching its march to sample the atlas buys the effect's self-shadowing and the
  crepuscular ray in the same change.

## 6. What this lab's case format cannot say

A case cannot set a parameter, so `scene/volumeDensity`, `scene/volumeNoise` and the resolution scale
are all test-file arms rather than case arms. Cases 4, 5 and 6 name the arm and say where it lives.
That limit is `labs::LabCase`'s and is shared with every other lab; it is worth stating here because
almost everything this lab wants to vary is an `scene/volume*` parameter.

## 7. How to use it

```
avgen --lab-case volumetric:1                # the bench
avgen --lab-case volumetric:3                # the same frame with the pass off
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[volumetric][lab]"
```

GPU tests take `tools/gpu-lock.sh` (ADR-170), minima over repeats, never means.
