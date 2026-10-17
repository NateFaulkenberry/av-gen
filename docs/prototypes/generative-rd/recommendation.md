# Generative R&D: findings and recommendation (brief §7E)

2026-10-17, branch `proto/generative-rd`.
- Evidence: the gallery in `~/Desktop/av-gen-review/63-generative-rd/`. The `compare/` folders hold the same camera
  and window, autonomous next to Feline Footwear.
- Costs: `performance.md`.
- Implementation notes: `A-oscillators.md`, `B-flow.md`, `C-reaction-diffusion.md`.

## Ranking, on observed results

1. **C, reaction-diffusion as relief** (RD Relief). The one result that reads as a new addition to AV Gen's
   vocabulary, not a variant of an existing effect. A living labyrinth grown in mineral relief, with a frontier
   between pattern families wandering across it. It reads as sculpted material, raked light does all the work, and it
   holds across five palettes, from non-luminous ivory to black with emissive boundaries.
2. **A, coupled oscillators** (Phase Membrane, Phase Wall). Genuinely different from the resonator. The resonator
   draws standing figures; this draws relationships: target waves from pacemakers collide in cusps, islands of
   synchrony are separated by glowing phase defects, and coupling sweeps make the field fall into step and break
   apart. The defect light is its signature. The wall shows the synchronisation transition more legibly than the
   membrane; the membrane is the stronger image.
3. **B, flow fields** (Current Lines). The weakest, and I would not invest further on this path. A designed current
   is a powerful composition tool, and the streamlines finally make it legible. But the images are an amber line
   stroke on a dark valley, not a world, and the representations that would fill the frame (the strand meadow, the
   ink) were murky or smeared. What the flow work did produce is reusable infrastructure: designed vector grids,
   scalar and vector planes, and the shed-vortex wake.

## The comparison the brief asks for (§7E)

| | C, reaction-diffusion | A, oscillators | B, flow |
|---|---|---|---|
| **Visual distinctiveness** | High. Grown relief is not something AV Gen had. The bloom variant is the familiar neon cell look (weak) | High. Phase defects, cusps and target waves are a new look; unison alone is blank, fixed with a frequency tilt | Medium-low. Streamlines resemble the existing fibre fields (Chorus Field); the meadow is a texture |
| **Musical expressiveness** | Medium. Kick rings of growth absorbed into the pattern are clear and have memory; the rest is slow by design | **Highest.** Sustain-to-coupling visibly unifies the field; kicks launch fronts that the coupling carries; the section (intensity-to-lag) reorganises it | Medium. Bass-to-river-strength surges read physically; kicks and snares too strong at first, now subtle |
| **Autonomous behaviour** | Good only with help: every useful regime freezes within about 10 s, so the frontier and a brush keep it alive | **Best.** The network lives by itself; islands coarsen, defects wander, pacemakers keep emitting | Good: eddies shed, drift and fade; the current never repeats |
| **Artistic potential** | Highest: materials, palettes and relief; pattern on any surface | High: kinetic sculpture, architecture, a driver for other geometry | Medium: a composition and motion tool, not an image |
| **Reusability** | High. The RD plane is a field any reader samples (deformers, materials, effectors); a pattern skin for any scene | High. A phase field is a general driver (an effector on instances, a deformer, light) | High as infrastructure (designed vector grids, the wake); low as a look |
| **GPU cost** (A/B, `performance.md`) | 6-7.5 ms per 320-384² plane at 12 iterations; a volume is 33 ms | **0.2-2.6 ms** | about 9 ms for the wake with its fibre readers |
| **CPU cost** | Under 1 ms | Under 1 ms | Under 3 ms |
| **Implementation complexity** | Low. An extension of an existing mode (ADR-1451) | Medium. A new grid mode with a CPU reference (ADR-1450) | Low engine (ADR-1452, a limit); high authoring (designing a current is real work) |
| **Live (1080p30, Auto)** | Relief at High, 0 misses; pins at Low; bloom no | Membrane at Medium (0-2 misses); wall at Ultra | Lines at High, 0 misses; meadow no |
| **Offline** | All, including the bloom volume | All | All |

## What worked, what did not

- **Simple beat sophisticated, as the brief warned.**
  - The best C mapping is the oldest one, displacement plus raking light; the fancy closed-form volume lost.
  - The oscillator's best readout is the simplest derived quantity, phase strain (defects light).
  - The flow's best representation is a few rakes of streamlines, not 64,000 strands.
- **Music should write into history, not parameters.**
  - Onset fields as the grids' injection (growth rings in C, phase kicks in A) give a correspondence the audience
    can see and that persists.
  - Routing audio straight to feed and kill was never needed. Routing it to coupling and lag works because those
    are the network's own bifurcation parameters, moved slowly (seconds).
- **Each needed a structural fix to stay alive.**
  - Gray-Scott freezes once it fills its space. Life comes from a wandering regime frontier and a brush.
  - Full synchrony is blank, so the unison state carries a frequency tilt.
- **Failures, candidly:**
  - The loom (oscillators on ribbons, the Resonant Silk extension) was dropped: noisy striations, no ribbon
    identity.
  - The ink river was dropped: first-order semi-Lagrangian advection smears ink into plumes.
  - The meadow and storm are kept only as hidden harnesses.
  - The bloom is hidden; it reads as the generic organic neon look.
  - None of the MIDI note mappings could be tried with a controller. They are wired, and the engine correctly flags
    them as live-only in renders.

## Recommendation

**The strongest next step: develop C into a standalone live scene, "Grown Relief", and extract its pattern grid as a
reusable surface skin.** Concretely:

1. **A scene.** Build it from `rd-relief`: the valley, the regime frontier, kick growth rings, palette selection
   (ADR-1420, with the five palettes as states), and a pre-rolled initial state so a live start does not show 10 s
   of bare ground.
2. **Combine with the resonator.** Make the resonance field's sand channel (ADR-1405) the regime field. The
   cymatic figure would then decide where the pattern is cells and where it is labyrinth, so the music's standing
   figure is written into a surface that remembers it. This is a direct, cheap combination: a `grid` reader of a
   resonance bake (ADR-1407) as `regimeField`, with no new engine code. It is the most promising way to give the
   resonator scenes memory.
3. **A, second.** Develop it as a reusable phase-field driver, not first as a scene. Its two strongest uses are a
   kinetic architecture (the wall's flaps as a facade in another scene) and defect light over any surface. It is
   cheap enough (under 3 ms) to sit beside anything. A standalone Phase Membrane scene is worth making after C.
4. **B: retain as a specialised tool.** Keep the designed vector grid (authored values, ADR-1282) plus the
   shed-vortex wake as a pattern for scenes that need coherent motion. Abandon the meadow and ink representations.
   - If ink or marbling is wanted later, it needs a higher-order advection scheme (MacCormack/BFECC) in
     `cs_advect`. That is an engine investment the current results do not justify.
   - B was also the experiment that found three engine gaps (`B-flow.md` §4), the most useful being that a compound
     field is typed scalar, so particles cannot read a compound current.

## Repository changes (for the merge)

**Engine** (each with unit and GPU tests):
- ADR-1450, the oscillator grid;
- ADR-1451, an RD plane at speed with a regime field;
- ADR-1452, scalar and vector planes.

Shared files touched:
- `src/spatial/grid_field.*`, `src/rendering/simulation.cpp`, `shaders/simulate.wgsl`, `src/scene/grid_params.cpp`;
- `docs/volumetrics-and-simulation.md`, `docs/decisions/README.md`.

Not touched: `src/spatial/resonance*`, `src/params/palette*`, `shaders/fields.wgsl`, `src/scene/material_program.*`,
`shaders/material.wgsl`.

**Scenes.** The `tools/generative_rd/` generators and `examples/experiments/` (21 MB of meshes; one riverbed mesh
shared by the flow scenes). Under the index category **Experiments**:
- **shown:** Phase Membrane, Phase Wall, RD Relief, RD Pins, Current Lines;
- **hidden** (harnesses kept for comparison and measurement): RD Bloom, Current Meadow, Current Meadow Storm.

No Resonator scene was modified.
