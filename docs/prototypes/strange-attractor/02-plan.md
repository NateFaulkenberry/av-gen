# The Strange Attractor: plan

## The art direction: "the sediment of a flow"

A canyon country at the last light of dusk, whose rock is the deposit of a dynamical system:
- its escarpments are the coastline of a warped erosion front;
- its strata are level sets that fold in waves no sediment makes;
- where the system's attractor surfaces, the stone itself is the trajectory: a ribbon that climbs out of the gorge,
  loops and returns into the cliff, with the layers bent around it.

The landmarks (Stage 2) repeat the land's own profile at smaller scales: arches inside arches, buttes on buttes.
The camera and the light stay naturalistic, so the strangeness reads as the world's, not the lens's.

**One look:**
- **Light.** A low sun from the side. It lights the faces that look toward it and throws long violet shadows across
  the bench.
- **Sky.** Twilight: the warm horizon on the sun's side, the Belt of Venus over Earth's shadow opposite, and an
  indigo zenith.
- **Air.** Analytic height fog, cooling with distance.
- **Rock.**
  - Strata colour on the cliffs: rust and coral low in the stack, sandstone in the middle, a bone caprock high.
  - Rubble on the talus.
  - Mauve dust, with desert varnish, on the flats.
  - A pale mineral playa in the gorge.

**Palettes:**
- **Dusk Mineral** (the default);
- **Nocturne** (the darker one: blue hour, last light on the caprock);
- **Chromatic** (Danxia-bold bands under a cobalt sky).

The `palette` parameter blends between neighbouring palettes, so audio can move along the axis later.

## Representation (ADR-1850)

| Element | Representation | Where |
|---|---|---|
| Terrain | a CPU-baked height field (R32F) plus masks (RGBA8: rock, playa, cavity, gully ridge), marched per pixel | `scene/attractor_world.cpp`, `shaders/attractor/world.wgsl` |
| Near detail | analytic displacement and bump (rock grain, cliff-plane flutes), fading out by 1.8-2.5 km | shader |
| Strata | `y + tilt + warp + fold`. The same coordinate cuts the terraces on the CPU and colours them in the shader (the hash is bit-identical) | both |
| Landmarks (Stage 2) | SDFs evaluated in the same march, inside bounding volumes, blended into the height field by smooth-min | shader, with the CPU recording placement |
| Branching (Stage 3) | inner gorges: a primary meander already exists; tributaries carved into the bench by the same profile | generator |
| Attractor (Stage 3) | an RK4 trajectory (Rössler first, Thomas as the alternative), normalised and arc-length resampled, as a tube SDF with varying radius, rising from the gorge | generator plus shader |
| Sky and air | analytic, in the same pass | shader |

## Determinism and lifecycle

- The world is a pure function of `AttractorGen`: seed, resolution, extent, terrainScale, roughness, ridgeStrength,
  domainWarp, terraces, erosion and strataFold.
- Rows are generated in parallel. Each texel is independent, so the bytes do not depend on the thread count.
- A structural parameter change regenerates once, on the next frame. It is the same world object until then, and a
  last-world cache makes a reload free.
- Look parameters never regenerate and never re-upload (tested).
- A regeneration is a camera cut (ADR-912), so temporal history never smears across worlds.

## Stages and gates

1. **Gate 1 (this):** Stage 0 research plus the Stage 1 skeleton. **Done**; see PROGRESS.md.
2. **Gate 2: Stages 2 and 3.**
   - **Recursive landmarks:** two strategies tested, an SDF arch-in-fin recursion and stepped buttes.
   - **Branching tributary gorges.**
   - **One attractor ribbon:** Rössler against Thomas, judged on the frame.
   - Stills and a short clip with a slow drift.
3. **Gate 3: Stages 4 and 5.**
   - **Final art direction across seeds.**
   - **Audio**, one mapping at a time (each candidate to be checked against the lab's measured behaviour):
     - `response.bass` (smoothed) to the attractor ribbon's swell and a bounded fold amplitude in the shader (the
       fold is a shader-side strata term, so it never regenerates);
     - `response.onset`/`response.onsetEnv` to a light front travelling down the gorge network;
     - `response.mid`, or a sustained stem role, to the palette axis and the haze;
     - `response.level` to key light, within limits;
     - silence settles all of them.
   - A 30-60 s clip with music.
4. **Final: Stages 6 and 7.**
   - Quality tiers: march steps, the near detail, shadow steps, render scale, and the height-field resolution (2048
     live, 4096 offline).
   - Measured with `tools/live_perf_baseline.py`; the GPU suite and the CPU-assets suite.
   - Docs.

## Risks

- **Realism over surrealism.** The skeleton is a convincing canyon, and convincing canyons are not this brief.
  Gates 2 and 3 have to land the impossible geology.
- **Height-field cliff artefacts near the camera.** Vertical faces are stretched texels, and the near foreground is
  where they show.
  - Mitigations: 4096 offline, cliff-plane flutes, keeping the camera off near walls (the camera search already
    does this).
  - Next: SDF landmarks can carry the near foreground.
- **Live cost.** The march is per pixel. Rough evidence: a 60-frame 1080p headless render ran at 61 fps end to end.
  That is not a measurement, but the budget looks sound. Landmarks and the attractor add per-step SDF cost: keep them
  inside tight bounds.
- **Generation time.** 2048² takes about 1.2-1.5 s and 4096² about 6-10 s on the CPU. That is acceptable at load and
  on a seed change. A seed change while live stalls one frame: move it to a worker thread before Stage 6.
