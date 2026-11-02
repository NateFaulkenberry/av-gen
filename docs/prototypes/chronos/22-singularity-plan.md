# CHRONOS, Temporal Singularity: inspection and plan (Gate S0)

## §11 inspection: what exists

| Need | What the engine already has | Use |
|---|---|---|
| Slit scan, stretching, duplication, fragmentation | ADR-1931 sampler (directional, radial or luminance map; strips; a moving scan); ADR-1934 identity gating, with a per-slot trail (echo spacing, fade) and fracture | the figures' transformation (S1). The full-frame map is reserved for moments of chaos (S2), never left at maximum |
| RGB temporal displacement | not yet: one age per pixel | S1: three ages, one per channel, in the gated sampler (one ADR) |
| Temporal feedback | ADR-1066 feedback (zoom, rotate, drift, hue) as an FIR over the clean ring; user shader layers have persistent passes (a true IIR) | S2: the ring's FIR first, because it is scrub-safe; a persistent IIR only if the FIR cannot do it |
| Full-screen procedural imagery | ISF-style user shader layers (`src/shaders/shader_layers.hpp`): a Background stage behind the geometry and a Post stage on the HDR scene before the temporal stage; multi-pass; half-resolution and persistent targets; audio, beat and time uniforms; inputs as routable parameters (`shader/<name>/<input>`) | **the singularity and the background (S0)**; the gravitational warp of everything drawn (Post, S1) |
| Particles, ribbons | the particle system (Event Horizon uses ten emitters); procedural geometry | fine sparks and ejecta, only if the shader's own fine scale is not enough (S2) |
| Figures | the eight MPFB CC0 adults (`build_souls.py`), static GLBs posed by node transforms that the timeline keys, as in D2 | S1: 6-10 figures, re-posed and reclothed as needed |
| Audio | the drum stem as a source (`stem.drums.*`), the `response` and `beat` signals, routes to any parameter | S2: routes to the shader's inputs (turbulence, expansion, rotation, scale, fragmentation) |

## How the singularity is built

**One Background shader layer** (`examples/chronos/singularity/singularity.wgsl`), stateless.
- It is a pure function of render time, so it is scrub-safe, exact in a render of a range, and has no
  feedback-accumulator flicker.
- It owns the luminous structures and the full-screen background; nothing else draws them.

**The field.** Every structure is drawn in the coordinates of one gravitational field:
- the main mass, whose centre drifts and whose size breathes;
- two eddies that orbit it eccentrically, spin the other way, and swell and fade;
- the whole frame's slow turn and zoom.

**The structures**, each drawn in the field's lensed, swirled coordinates:
- **the core:** near black, with a noisy, breathing edge;
- **a broken rim:** white-hot to gold, fragmented;
- **three accretion bands:** different radii, pitches, speeds and directions (inner clockwise and fast, middle
  counter-clockwise, outer clockwise and slow), each a ridged spiral noise on an inclined, tilting disk, so the
  rings are elliptical and asymmetric;
- **radial streaks:** falling inward faster near the core, in sectors that come and go;
- **cellular fragmentation:** whole cells of the disk displaced, strongest during instability;
- **secondary spirals:** around the eddies;
- **the background:** lensed nebular flow noise, filaments that follow the swirl, and stars stretched into arcs
  along the flow.

**Instability.** At irregular times (about every 17-23 s, from a sum of pulses), the field's turbulence and
fragmentation surge and the bands are displaced. The singularity tears, then reforms.

**Inputs.** The layer's inputs are parameters, so audio routes (S2) and timeline keys can drive them:
- turbulence;
- expansion;
- rotation;
- scale;
- fragmentation;
- the gains of each structure.

## One field for everything (§6)

The field is a few analytic functions of time: the mass centres, masses and spins, and the frame's turn and zoom. Each
consumer evaluates the same functions.

- **The background layer** lenses and swirls its own imagery with it (S0).
- **A Post layer** (S1) warps the rendered scene (the figures and their trails) with the same deflection and swirl. So
  figures, trails, light and background bend together, with no unrelated filters. The gated sampler reads the
  identifier target, which is not warped, so the Post warp must run on the composite, after the temporal stage. If
  the layer order cannot put it there, S1 adds one ADR (a post layer after temporal, or the same warp applied inside
  the gated pass).
- **The figures' trajectories** (S1) are integrated on the CPU by the scene generator through the same field: pulled
  in, slung around, curved. They are written as timeline keys.
- **One source of truth.** The scene generator (`build_singularity.py`) writes the shared field code into each WGSL
  file from one template, and evaluates the same functions in Python. The three copies cannot drift apart.

## Slots and figures (S1)

- **One figure, one ADR-1934 slot.** Six to ten figures, so at most 16 slots.
- **Stage follows the field.** A figure's stage is a function of its potential depth in the field, not of a path
  fraction:
  - intact far out;
  - echoes and stretch as it accelerates;
  - fracture, RGB displacement and loss of form near the rim;
  - squeezed to a ribbon and gone at the core.
- **New sampler terms**, in the gated pass:
  - RGB temporal offsets per slot;
  - a fracture whose strips follow the local swirl direction, not one screen angle.

## Budgets (1080p, 30 fps, M2 Max)

| Item | Budget |
|---|---|
| Singularity and background layer | ≤ 4 ms, full resolution. If it is over, the smooth bands move to a half-resolution pass and the fine structures stay at full resolution |
| Figures | 6-10 × about 16k triangles, static |
| Temporal | ring of 64 layers at half resolution (133 MB) plus the slot ring (33 MB); gated pass ≤ 1 ms |
| Post warp | one full-screen pass, ≤ 0.5 ms |
| Frame | ≤ 20 ms GPU at the High tier, profiled per pass (Test G) |

## Gate S0 deliverable

Test A: the singularity alone, with no figures. Stills, and a 45 s clip with sound (Feline Footwear; the drum stem as
the beat, with no routes yet). It must carry the scene with audio off.
