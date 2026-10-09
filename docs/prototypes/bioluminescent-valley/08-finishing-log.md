# 08: Cinematic finishing pass log (spec 07)

Review media lives in `~/Desktop/av-gen-review/45-bioluminescent-valley/finishing/`.

## Deliverable A: the water investigation (2026-10-09)

### Diagnosis (§3.1)

The diagnostics are in `A-water/diagnostics/`. Each render isolates one component in the riverbank camera.

- **Body colour and reflection off, transmission absorbed:** the surface is black. So the flat blue is not the
  water's colour.
- **Environment reflection only:** the whole blue sheet comes back. Its source is the environment cube (the sky)
  returned at a grazing Fresnel. But this river is under a forest, and the only thing the water could reflect was
  the sky. ADR-099 water samples the cube alone. The bank, its organisms, the canopy and the falls never reached it.
- **The bank's light on the water:** what there was came from ADR-1281's emitter mirror (sprites only). That is
  the "blobby dashes".
- **Depth:** shown as one alpha over the bed, with no absorption by colour, so it reads as opacity, not depth.
- **Normals:** three layers of one value-noise texture with no broad swell and no calm or ruffled regions, so the
  surface is uniformly glossy. The normals were not flat; the reflection had nothing to show in them.
- **Not the cause:** tone mapping and bloom. The water pixels sit below the bloom threshold, and AgX keeps their
  hue. The cause is the light the surface was given, not how it was displayed.

### Research

**Evan Wallace's WebGL Water** (madebyevan.com/webgl-water; MIT):
- what it does: a heightfield wave simulation, ray-traced reflection and refraction against a known analytic pool,
  and caustics from refracted-ray area compression;
- what transfers: the idea that reflection and refraction are rays traced against scene geometry, not a cube
  lookup, and absorption by depth.
- what doesn't: the analytic pool. Our scene is arbitrary geometry, so here the trace has to run against the depth
  buffer.

**jeantimex/webgpu-water** (MIT): a WebGPU port of the same design: a 256² heightfield on a finite-difference wave
equation, normals from the heightfield, Snell refraction, Fresnel blend, 1024² caustics. No code was copied. The
WGSL structure of its Fresnel blend is the standard Schlick form we already use.

**Production practice** (SSR in Frostbite/Unreal, planar reflection, water in Sea of Thieves/Uncharted):
- screen-space reflection with an environment fallback for off-screen rays;
- reflections stretched along the view on rippled water, from the normal distribution;
- Beer-Lambert absorption by channel;
- flow-aligned swell under capillary detail;
- roughness variation between calm and wind-ruffled water.

Planar reflection would be exact for this flat river, but it needs a second render of the scene from the mirrored
camera. It is noted as the next step if SSR's off-screen gaps prove limiting.

### Built (ADR-1286)

An optional cinematic water path, byte-identical when off:
- a frame copy before the water;
- world-space SSR with refinement and N view-streaked rays;
- transmission with absorption by channel;
- swell;
- calm/ruffled regions;
- tint patches;
- glints in the colour of what the reflection found.

### Experiments (§3.6)

All four are the same camera, exposure and time (5 s), at 1920x1080. Files: `A-water/W0..W3.png`, `A-sheet.png`,
`A-water-crops.png`.

- **W0:** Checkpoint D.
- **W1** (multi-scale normals, Fresnel, depth):
  - shallow [0.003,0.01,0.03], deep [0.002,0.006,0.03], roughness 0.04;
  - ripple 0.35 at 0.9/m, chop 0.6, undulation 0.6 at 0.06/m, roughnessVariation 0.7;
  - transmission 1, absorption [3,2,1.1], clarity 2;
  - reflection 1.0, tint [0.07,0.09,0.3], fresnel 0.1, specular 1.5.
- **W2** (reflections and highlight structure): W1 plus
  - ssr 1, 120 m, 128 steps, gain 1.8, streak 0.08, 4 rays, glints 3;
  - emitter mirror stretch 5, softness 9.
- **W3** (combined): W2 plus
  - tintPatches 0.7, tint2 [0.22,0.1,0.5] at 0.05/m;
  - glow 0.06 [0.35,0.15,1.0] at 0.06/m, coverage 0.16;
  - refraction 0.4;
  - mirror ×1.3, stretch 6, softness 10.

Rejected on the way:
- glow 0.35 with coverage 0.3 turned the whole river into a flat lilac sheet;
- a streak of 0.6 scattered the rays off everything and left only the environment;
- an environment tint of [0.5,0.35,1.0] made the river a glowing teal-violet slab.

**Creative Critic** (deep, job_1a120e0818b4ceabb):
- water-shot luma rises 0.090 → 0.113 and contrast 0.110 → 0.120 across W0-W3, with clip at 0;
- it flags W2 and W3 as "repeating the composition" of W1, which is expected for a controlled experiment;
- it cannot judge the water itself.
