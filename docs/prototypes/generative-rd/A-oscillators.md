# Experiment A: coupled oscillators and phase fields

Generator: `tools/generative_rd/oscillators.py`. Scenes: `examples/experiments/osc-membrane/` and
`examples/experiments/osc-flaps/`. Engine: ADR-1450 (`GridMode::Oscillator`). Media:
`~/Desktop/av-gen-review/63-generative-rd/A-oscillators/`.

## 1. Model

The network is a plane of phase oscillators on the GPU, up to 1024² (ADR-1450). It is Kuramoto with the Sakaguchi
phase lag:

    dθ_i/dt = 2π ω_i + K m_i Σ_j w_ij x_ij sin(θ_j − θ_i − α) / Σ_j w_ij

- **The disk:** w_ij is a Gaussian over a disk of radius r (1-4 cells), plus up to four hashed long-range links per
  cell. That is the brief's local lattice, spatially weighted and hybrid networks in one kernel.
- **x_ij:** the cross-coupling between two hashed populations (the layered network).
- **m_i:** a coupling-mask field (waveguides, regions).
- **ω_i:** a triangular spread about the mean natural frequency, plus a detune for the second population, plus a
  frequency field applied in a separate kick pass (pacemakers, a tilt, music).
- **α:** the lag.
- **Kicks:** a per-sub-step pass adds phase from an injection field (onsets) and noise.
- **Readouts:** a cell stores cos θ, sin θ, the local order R (low-passed) and the strain
  4(1 − ⟨cos(θ_j − θ_i)⟩) plus kicks (low-passed). Readers interpolate all four smoothly.

**Numerics.** Explicit Euler at the sub-step (1/60 s × `speed`), with `iterations` to split it. K·dt stays below
about 0.2 at every setting used. The work is gather-only and the hashes are deterministic, so seek and checkpoints
work as for every grid. The CPU reference agrees with the GPU to 2e-3 over 32 sub-steps (test).

A fully connected Kuramoto network was not built. It has no spatial structure: it collapses to one global phase.

## 2. Regimes (surveyed in numpy with the kernel's own model, then in the engine)

| name | K | α | r | spread (Hz) | start | what it does |
|---|---|---|---|---|---|---|
| targets | 10 | 1.0 | 3 | 0.03 | ordered | Pacemakers (cells tuned faster) send concentric waves; trains collide in cusps |
| spirals | 8 | 0.6 | 4 | 0.05 | random | Rotating phase vortices with curled arms, a few dozen cores |
| islands | 4 | 0.3 | 4 | 0.15 | random | Partial synchrony: patches in step, phase defects between them that slowly annihilate |
| unison | 12 | 0 | 4 | 0.03 | half-random | Global synchrony. A blank, breathing sheet unless something tilts it |

**Not useful:**
- α ≥ 1.4 with local coupling gives turbulence, a fine noise with no structure.
- Weak coupling with a large spread is just noise.
- Local coupling from random phases always freezes vortex defects (the 2D XY model). Order stalls near 0.7. In the
  scenes this is a feature: the defects glow.

**After review:**
- The unison state now carries a 0.02 Hz tilt of natural frequency across the field, so full synchrony reads as one
  long slow wave instead of a blank sheet.
- The lead scene uses islands, where the music pulls the field toward synchrony and breaks it.

## 3. Visual mappings

| config | mapping | verdict |
|---|---|---|
| **membrane** (Phase Membrane) | An amphitheatre floor displaced along its normal by sin θ (1.1 m). Albedo is bronze on crests and teal-black in troughs. Emission is cyan where the strain is high (kicked fronts, cusps) and amber at defects (1 − R). The bowl closes the view: no horizon. | **The strongest A result.** Rings, spirals and islands all read as phase relationships, and the defects are a distinctive, legible feature. |
| **flaps** (Phase Wall) | 9,600 metal flaps on a 150 × 64 m wall, each turned about its vertical axis by 0.9 sin θ (rotation effector). The grid's plane is turned upright by the reader fields' frame. A timeline sweeps K 1.5 → 14 → 1.5 over 96 s: disorder, islands, unison and back. | A good synchronisation sculpture: islands turn together and flash, seams glow. It reads as a kinetic facade. Its weakness is that it is a flat rectangle, now framed to fill the screen. |
| loom (dropped) | Nine ribbons on two networks (lift by A, sway by B, light where sA·sB > 0.55). | Dropped after review. It read as a noisy white terrain strip with striations, and never showed groups synchronising. A weak representation, not a weak network. Removed from the generator. |

## 4. Audio and MIDI (`music()` in the generator)

The music changes the network's dynamics:

| input | target | why |
|---|---|---|
| kick (low onset) | A ring of phase advance from the focal point (an onset field as the grid's injection), 2.5 rad/s per unit | A new front that the coupling then carries: the brief's "onsets inject localised perturbations" |
| snare (mid onset) | A faster, thinner ring from elsewhere | A second source of fronts, distinguishable by speed |
| response.sustain | K +6 (4 s attack, 6 s release) | Held sound pulls the field toward unison; sparse passages loosen it into islands |
| response.bass | natural frequency +0.25 Hz; relief depth +0.6 | The whole field speeds up with the low end |
| response.hat | phase noise +0.5 rad/√s | Fine-scale disorder from the top end |
| response.intensity | α +0.35 (6 s attack, 12 s release) | A build slowly reorganises the field (rings toward spirals): a bifurcation driven by the section |
| MIDI notes 36-38 | Pacemakers 0-2 strength +0.35 (release 2.5 s) | A held note makes a source of target waves; velocity sets its tuning; the release sets how long it lingers |

**Controls (CC):**

| CC | Control | What it moves |
|---|---|---|
| 1 | Coupling | K |
| 2 | Complexity | spread |
| 3 | Motion intensity | speed and relief |
| 4 | Audio Influence | the depth of every audio route, 0.5 at rest |
| 5 | Persistence | the readouts' time constant |
| 6 | Visual mode | α |

**Observed (Feline Footwear 40-60 s, Trench 60-80 s, Hyperborea 20-40 s):**
- Kicks at the first rate tried (6) overwhelmed the self-organised structure with ring trains on a dense mix. At 2.5
  the rings and the islands coexist.
- The sustain-to-coupling route is the most musical. Held passages visibly calm and unify the field.
- The hat noise is subtle.
- MIDI could not be exercised in an offline render. The routes are flagged live-only by the engine (correctly). They
  are wired, and documented as untested with a controller.

## 5. Cost

See `performance.md`. The network itself is cheap (~0.1-0.3 ms at 384²). The membrane's cost is its mesh.

## 6. Known issues

- **The membrane's crests grain** where the spread is high (cell-scale phase noise reads as surface texture).
- **The flaps wall** is a flat panel; curving it would need a bend deformer pass on instances.
- **Strain glow can cover large areas** on very dense music; the accent gain is set low (0.45) to compensate.
