# ADR-1200: The ecosystem is the first Environment

- Status: Accepted (proto/bioluminescent)
- Found by: the Bioluminescent Environment brief (`docs/prototypes/bioluminescent/00-brief.md` §1, §6, §13). The brief
  asks for a third visual mode beside conventional worlds: specialised environments with their own rendering and
  simulation, which may coexist with conventional content (HYBRID).
- Evidence: `docs/prototypes/bioluminescent/03-architecture.md` §3.

## Problem

The Rift (the first Environment) wants millions of tiny light sources: photophores on giant crinoids, polyps on mats,
fans and sea pens, crusts on canyon walls, and plankton in a river.

The general renderer can draw them only as meshes. CP1 drew them as 20-triangle beads, and at 1080p (realtime tier,
`06-flight`) they cost about 22 ms of a 76 ms frame. Drawing them **unlit** saved nothing (42.4 ms lit pass against
44.3), so the cost is the rasterisation of sub-pixel geometry, not shading. No material or light setting can fix it,
so the density the brief asks for cannot be had that way.

There is also nowhere to put a specialised renderer. No scene "mode" exists, and `Environment` already means sky,
IBL and fog in the code.

## Decision

**The mode is a property of a scene's content, not a switch.** A specialised Environment is a scene-level block with
its own renderer in `SceneRenderer`, writing the shared HDR, emission and depth targets:
- a scene with no block is a WORLD;
- with the block alone, an ENVIRONMENT;
- with both, a HYBRID.

Audio, analysis, the signal bus, routes, MIDI, scene states, the timeline, camera, post, bloom, tonemap, every output
path (PNG, EXR, video, AOVs) and the live quality ladder are shared unchanged. None of them knows the block exists.

The first such block is `"ecosystem"` (`scene::Ecosystem`, `rendering::EcosystemRenderer`, `shaders/ecosystem.wgsl`):
- **Emitter layers.** A layer attaches a template of emitter points (`[x, y, z, v, u, radius]` in the host's source
  space) to every instance of one or more ordinary procedural nodes, its **hosts**. The bodies stay conventional:
  instanced, culled, LOD'd and lit by the general renderer.
- **Per frame, after the lit pass:**
  1. `cs_emit` evaluates every (host instance, template point): the host's instance record transforms it, the
     prepass's linear depth tests it, and it is lit by the model below.
  2. A point under `spriteRadius` pixels is **splatted**: its energy (radiance × projected area, with a floor of a
     tenth of a pixel so the far field adds up) is spread bilinearly into a u32 fixed-point accumulation buffer with
     atomics, which is order-free and so deterministic.
  3. A larger point is appended to a sprite list.
  4. A resolve pass adds the accumulation into HDR and emission (so the selective bloom sees it as light).
  5. An indirect pass draws the sprites as soft discs with the hardware depth test and no depth write.
- **The light model** is a pure function of time and identity, so play, scrub and offline agree:
  - **rest** = `intensity` × breathing (a slow cycle per organism, phase per point), × twinkle, gated by
    `sparsity`, plus spontaneous flashes (`pulseRate` per point per minute, fading over `pulseDecay`);
  - **response** = `responseGain` × max(0, `responseField`(p) − `responseThreshold`). With `travel`, a point far
    along its organism (v → 1) hears `lagField` instead, so light climbs a stalk behind a front;
  - **colour** = mix(`color`, `excitedColor`, saturate(response)). This is the fluorescence the art direction
    needs (a fan is violet at rest and magenta only under the passing wave);
  - **radiance** = colour × (rest + `excitedIntensity` × response), faded over the last quarter of `maxDistance`.
- **Live parameters.** Every behaviour leaf is a parameter at `ecosystem/<layer>/<leaf>`: `enabled`, `color`,
  `excitedColor`, `intensity`, `excitedIntensity`, `size`, `responseGain`, `responseThreshold`, `travel`, `breath`,
  `breathRate`, `flicker`, `flickerRate`, `pulseRate`, `pulseDecay`, `sparsity`, `maxDistance`. Routes, MIDI,
  presets, the timeline and the editor's generic parameter panel reach them with no further wiring.
- **The gate.** With no active block the renderer records nothing, and the frame is byte-identical
  (`a disabled ecosystem renders the same bytes as a scene with none`).
- **Unknown keys are refused**, in the block and in its layers, so a typo is an error, not a silent no-op.

## Measured (M2 Max, 1920x1080, realtime tier, GPU p50, under the lock)

The same camera, organisms and world, with the micro-emitters as tessellated beads against ecosystem layers:

| | GPU frame | submitted triangles |
|---|---|---|
| CP1, emitters as beads (`cp1/ab-base`) | 77.6 ms | 8.56 M |
| CP2, emitters as an ecosystem, plus two new layers, wall crust (6,058 patches × 600) and river plankton (624 × 900) (`cp2/ab-eco`) | 56.0 ms | 3.46 M |
| CP2 with the ecosystem disabled (`cp2/ab-ecooff`) | 55.4 ms | 3.46 M |

The whole ecosystem costs **about 1 ms** (p50 56.1 against 55.4; min 52.8 against 51.5) for about 19 M candidate points a
frame. The geometry it replaces cost about 22 ms and drew a fraction of the points.

## Consequences

- WORLD, ENVIRONMENT and HYBRID need no switch and no compatibility path. A HYBRID is any scene with conventional
  nodes beside the block: they share depth both ways (a body hides an emitter, an emitter never writes depth).
- The next Environment (Astral Forge's particle anatomy, Echo Field) is another block and another renderer at the
  same seam. ADR-1200 deliberately builds no registry: two use cases come before a shared abstraction.
- Hosts are read from `ProceduralGeometry::instances` (the records the procedural renderer draws) and the source
  transform. A host that animates its instances per frame on the GPU (a generator distribution, vegetation wind) is
  not followed yet. The Rift's hosts are `points` distributions, and its bodies do not sway.
- Emitters do not cast light on surfaces. That is deliberate at rest (`docs/prototypes/bioluminescent/01-research.md`:
  real emitters do not light their surroundings). The collective light of the drop comes from the propagation field
  (ADR-1201), which surfaces and the medium can sample as a field.
- Tests: `tests/unit/test_ecosystem.cpp` (JSON, templates, parameters reaching the scene),
  `tests/rendering/test_ecosystem_gpu.cpp` (projection, occlusion, emission target, sprite path, response and
  excited colour, the gate).
