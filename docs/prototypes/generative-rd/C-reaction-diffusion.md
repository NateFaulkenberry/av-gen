# Experiment C: reaction-diffusion and emergent pattern

Generator: `tools/generative_rd/reaction_diffusion.py`. Scenes: `examples/experiments/rd-relief/`,
`rd-bloom/` and `rd-pins/`. Engine: ADR-1451 (an RD plane at speed with a regime field), on the existing
`GridMode::ReactionDiffusion` (ADR-032). Media: `~/Desktop/av-gen-review/63-generative-rd/C-reaction-diffusion/`.

## 1. Model and numerics

Gray-Scott, two fields A (U) and B (V), explicit Euler:

    A' = A + (D_A ∇²A − AB² + f(1 − A)) dt,    B' = B + (D_B ∇²B + AB² − (k + f) B) dt

- **Grid and stencil.** A plane of 384²-512² on the GPU, using the isotropic 9-point Laplacian
  `(4Σadjacent + Σdiagonal − 20c)/6`. The 5-point stencil draws patterns that align to the axes.
- **Constants.** D_A = 0.3, D_B = 0.15 in cell units; these are Sims' Da = 1, Db = 0.5 in this stencil's units.
- **Step size.** One model unit per iteration: `speed = 60 × iterations`, with 16 iterations per sub-step, so 960
  model steps a second. A pattern forms in 5-15 s. dt·D_A = 0.3 is under the plane's stability limit (0.375).
- **Volume.** The closed form uses a 100³ volume with the 6-neighbour stencil, D_A = 0.16 and dt = 0.5 (10 model
  units in 20 iterations a sub-step).
  - At dt = 1 the volume sits at its stability edge, D·dt·12 = 1.92, and grew a fine checkerboard. That was
    measured; halving dt removed it.
- **Boundaries.** The plane wraps (periodic), so the pattern tiles across any surface larger than the grid. The
  volume clamps.
- **Seeding.** fbm blobs (`seedAmount`), plus injection into B from fields: a wandering brush autonomously, and
  onsets, seeds and MIDI with music.
  - A 5 × 5 seed dies; 12 × 12 grows. Measured in numpy and pinned by a unit test.
- **Regions.** The regime field blends each cell's (f, k) toward a second pair (ADR-1451). Two pattern families
  then share a surface with a moving frontier.

The simulation never reads back to the CPU. Every reader samples the grid in place.

## 2. Regimes (numpy survey, 160² × 8000 steps, same stencil and constants)

| name | (f, k) | result |
|---|---|---|
| coral | (0.055, 0.062) | Thick worms and rings into a labyrinth (fills about 60%), settles in about 4000 steps |
| maze | (0.029, 0.057) | Soft fine labyrinth, settles fastest |
| cells | (0.0367, 0.0649) | Mitosis: spots divide until they tile the plane (about 25%) |
| worms | (0.046, 0.063) | Broken worms and loops |
| holes | (0.039, 0.058) | The negative: a filled plane with holes |
| spots | (0.030, 0.062) | Small dense hexagonal spots |
| fingers | (0.022, 0.051) | Branching, never quite settles |
| erase | (0.010, 0.075) | B dies everywhere (used to clear) |

**Unusable:**
- (0.026, 0.051) collapses to a uniform grey within 4000 steps.
- (0.014, 0.045) dies at this seed size.

Every useful regime **freezes** once it has filled its space, typically within 10 s. Left alone, the pattern goes
static. Autonomous life comes from three things:
1. **A wandering regime frontier.** A radial "pool" of cells inside coral, moved by a 96 s Lissajous track, plus a
   drifting fbm. Worms break into cells, and cells fuse into worms, where the frontier passes.
2. **A brush.** A small injection sphere on a 61 s path scars the pattern, and the pattern heals behind it.
3. **Music** (below).

## 3. Visual mappings

| config | mapping | verdict |
|---|---|---|
| **relief** (RD Relief) | A valley terrain (rises quadratically to a rim about 60 m up, so it fills the frame with no horizon) displaced along its normal by B × 1.6 m. Albedo is mineral where B is low and ivory where it is raised. A narrow boundary band (B ≈ 0.17-0.27) can carry light. Raked by a low key with shadows. | **The strongest C result and the strongest overall.** It reads as sculpted material. The coral-to-cells frontier is legible. It holds across five palettes. |
| **pins** (RD Pins) | 40,000 thin boxes on a 200 × 200 grid. A scale effector lifts each pin by the B under it; the program lights only pins standing on the pattern's boundary (tips only). Floor in the same program. | Attractive and distinct ("crowns" of lit pins around each spot). It risks busy, uniform glow when the whole field is spots. |
| **bloom** (RD Bloom) | A sphere reading a 100³ RD volume, displaced along the normal (3.2 m), garnet and indigo, with boundary light. Close camera: the pod fills the frame. | Weakest of C. It reads as a seed pod with cell craters, but the neon ring look is close to the familiar "organic texture" the brief warns about. The volume is also the most expensive RD here. |

**Palettes are separate from the pattern.** Each config's program reads roles (ground, ridge, line) from a palette:
- ivory (warm ivory and mineral, non-luminous);
- garnet (red and indigo);
- violet (violet and gold);
- ember (black with emissive boundaries);
- celadon (low saturation, controlled light).

The `--palette` flag puts any palette on any config. The relief in four palettes is in the gallery.

## 4. Audio and MIDI (`rd_music()`)

The music writes into the pattern's history; it never resets it. Feed and kill are never driven by audio.

| input | target | effect |
|---|---|---|
| kick (low onset) | A ring of B injected from the focal point (onset shell, 10 m/s) | A ring of new growth that the existing pattern absorbs. Reads as the music seeding growth |
| snare (mid onset) | A faster, thinner ring elsewhere | A second growth front |
| response.sustain | Regime pool strength +0.5 (4 s attack, 8 s release) | Held passages widen the region of stable cells |
| response.bass | Relief depth +0.9 | The relief swells with the low end |
| response.hat | Boundary light ×(1 + 3·hat) | The top end lights the edges; light only |
| MIDI notes 36-39 | Seed spheres at four places (release 3 s) | A held note grows pattern at a place; the release sets how long it keeps seeding |

**Controls (CC):**

| CC | Control | What it moves |
|---|---|---|
| 1 | Pattern mode | Toward cells |
| 2 | Growth | Speed |
| 3 | Relief | Relief depth |
| 4 | Audio Influence | The depth of every audio route |
| 5 | Erase | Feed falls: regions starve and collapse, then regrow when released |
| 6 | Boundary light | Light on the pattern's edges |

**Observed:**
- Kick rings are the clearest musical correspondence. They read as growth pulses, not flashes, and the pattern
  remembers them.
- Bass on the relief depth is effective but must stay modest, or the relief "breathes" like a speaker cone.
- The sustain-to-regime route is slow by design and visible only over tens of seconds.

## 5. Cost

See `performance.md`. A 512² plane at 16 iterations, and the 100³ volume at 20 iterations, are the expensive grids
of the whole investigation.

## 6. Known issues

- **Start-up.** A live start shows about 5 s of flat mesas while the seeds grow. A pre-rolled initial state would
  fix it.
- **The bloom volume.** It is sensitive to dt and resolution: 72³ gave fine noise and 56³ huge rings. The pattern
  scale in a volume is less predictable than on a plane.
- **Pins.** Every pin pays a per-pixel field read in its program; that is acceptable at 40k.
