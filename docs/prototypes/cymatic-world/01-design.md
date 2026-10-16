# Cymatic World: design proposal

*Branch `proto/cymatic-world` (from main `4421568b`). Brief: `00-brief.md`. Progress log: `PROGRESS.md`.*

## 1. What the research found

| Need | What AV Gen already has | Verdict |
|---|---|---|
| Audio in, response-shaped | `spatial::AudioHistory` (ADR-1116): a 64-bin log spectrogram 32 Hz-16 kHz at 93.75 rows/s plus low/mid/high/beat onsets, the whole track offline (pure in t), a 32 s ring live; heard through the Live panel's Response controls (ADR-1361) | **excitation source**, no new FFT |
| A spatial quantity every subsystem can read, CPU and GPU in step | `spatial::FieldSpec` / `FieldSet` (ADR-025): packed per frame into one `FieldBlock` uniform that the procedural vertex stage (Field deformer), the effector pass (instance records), material programs (`field` op), particle forces, fibre streamlines (ADR-1181) and SDF marching all bind already | **the seam**: the resonance field is one new field *kind* |
| Persistent state | grids (ADR-032/1201) step a simulation; spectrum/onset fields are pure functions of the history | a modal bank can be a pure function of the history: no state, seek = play |
| Deformable ground | `NodeKind::Terrain` (ADR-046/090) builds chunked CPU meshes from a `WorldMap` (noise + ridge/valley/flat features); the entity path it draws through has **no vertex deformers** | the **WorldMap** sculpts the base relief (probed through `avgen_world_preview`, `kit.terrain_heights`); the ground is drawn as a procedural mesh so the GPU deformer stack can move it (the Digital Ocean sea's screen-density mesh is the precedent) |
| Sculpture | procedural instancing (grammar, hierarchy), effectors (per-instance offset/scale/rotation/emission from a field), bevels, thin-film and anisotropy in `pbr_shade` (ADR-1143) | reuse |
| Matter | GPU particles with field forces (Force / Velocity / Kill), drag, attractor; fibres with streamline deformers | reuse |
| The Cymatic Plate | a material program evaluating a Chladni function whose mode numbers are routed op constants; sand particles; unlit top-down | keep its idea (sand gathers on nodes; low notes simple figures, high notes intricate; a chord is two figures at war); not its architecture (the figure lives only in one shader, so nothing else can read it) |

## 2. The resonance field (`FieldKind::Resonance`, ADR-1400)

**Conceptual model.** `D(x, t) = sum_i a_i(t) cos(Omega_i t + psi_i) phi_i(x)`.

**Modes.** `N` modes (`modes`, 4-24, fractional = the last one fades in). Mode `i` has a position `u_i = i / (N - 1)`
on two log axes, which is the field's *dispersion relation*:

- its eigenfrequency `f_i = fLow (fHigh / fLow)^u_i` (what it hears);
- its wavelength `lambda_i = lambdaMax (lambdaMin / lambdaMax)^u_i` (what it draws).

So a low note excites broad, simple figures and a high note fine, intricate ones (the plate's rule, now physical: a
bending plate's wavenumber grows with the square root of frequency).

**Shapes.** Each mode belongs to one *source* (at most four per resonator, each claiming a band of `u`):

- `point`: a standing wave about a centre, `R_m(k r) cos(m (alpha - theta_i))`, with `R_m` the asymptotic Bessel form
  `cos(k r - (2m + 1) pi / 4) / sqrt(1 + k r)` given a regular core (it is smooth and finite at `r = 0`); `r` is the
  3D distance, so a source above the ground draws rings on it and nodal shells around itself; `m` is the angular
  order (petals), chosen per mode from the seed, never above the source's `order`.
- `lattice`: a Faraday-wave standing pattern, `n^-1/2 sum_j cos(k (x . d_j))` over `n` directions half a turn apart
  (`order` = n: 2 squares, 3 hexagons, 4/5/6 quasicrystals): a carpet of cells that fills ground far from any centre.

**Excitation, persistence, damping.** For every analysed row `r`, mode `i` hears the spectrogram through its own
resonance curve `G_i(f) = 1 / (1 + Q^2 (f / f_i - f_i / f)^2)` (frequency-selective; `Q` = `selectivity`). Its
envelope is that drive convolved with a rise-and-decay kernel `K_i(tau) = (e^{-tau/tau_d,i} - e^{-tau/tau_a,i}) / (tau_d,i - tau_a,i)`
(unit area: a sustained tone builds up to its level over `tau_a` and rings down over `tau_d`); onsets add an impulse
`e^{-tau/tau_d,i}` with a 30 ms rise into the modes their band reaches (low/mid/high). `tau_d,i = persistence (f_0 / f_i)^0.35`:
low modes ring longest, as on a real plate. A soft limiter on the total energy turns dense music into *more modes*,
not more amplitude. A small `idle` level keeps the lowest modes breathing in silence, so quiet passages keep a figure.

Everything above is a **pure function of the audio history at the clock**: the same second gives the same field,
live or offline, played or sought, after any reset. The per-row drive is cached (a few new rows a frame), so the CPU
cost is a few tens of microseconds. `Omega_i` (`breath`, Hz, rising slowly with the mode) is a phase that only ever
advances with `t`: phase continuity is free.

**Queries** (`channel`, one resonator, many readers: a field whose `reference` names a resonance field shares its
modes). All are evaluated identically on the CPU (`spatial::sample*`) and in `shaders/fields.wgsl`:

| channel | value | used by |
|---|---|---|
| `displacement` | `D` | live motion of a surface |
| `amplitude` | `sqrt(sum e_i^2 phi_i^2)`, the local excitation | emission, effectors |
| `sand` | `sqrt(Ebar) exp(-E / (s^2 Ebar))`: where Chladni sand gathers (nodal lines of the energized area) | relief, line shading |
| `force` (vector) | `-grad E`: points towards the nodes (the acoustic radiation force) | particles |
| `gradient` (vector) | `grad D` | fibres, flexing |

A reader can listen to a band of the modes only (`bandLow..bandHigh` of `u`, optionally laid along an axis: a tuned
structure whose tiers hear different modes).

**GPU data.** The modes are packed once a frame into the `FieldBlock` (a new `resonance` array, 3 vec4 per mode, 48
modes in all): no new binding, no storage buffer, no readback; every existing consumer can sample it.

## 3. The three prototypes

**A. Resonant terrain.** A basin of dark stone, sculpted by the `WorldMap` (a crater floor, terraces, rim ridges and
gullies), drawn as one screen-density mesh. Two Field deformers move it on the GPU: the `sand` channel raises ridges
along the nodal lines and hollows the antinodes (the figure is in the silhouette), the `displacement` channel makes
the hollows breathe. Its material program reads the same `sand`/`amplitude` channels for a restrained, physically
placed glint on the crests.

**B. Resonant sculpture.** A deliberately composed crystal structure (hopper-crystal terraces: stepped, layered,
thin-film metal) whose tiers are tuned (low tiers hear low modes): per-instance effectors lift and tilt each tier by
the field's displacement at its place, and its thin-film colour follows the local strain.

**C. Resonant matter.** A floating body of particles inside a bell: high drag (overdamped, like sand) and the field's
`force` channel pull them onto the nodal shells around the field's elevated source; when the modes change, the matter
migrates into the new arrangement. Filaments hang from it along the field's gradient lines.

**Coupling (optional, later).** One causal link, the simplest: TBD after the three work (candidate: the matter is the
elevated source the ground hears).

## 4. Scene

`examples/cymatic-world/` (generator `tools/cymatic_world/build.py`, reusing the Sonic VFX kit), registered in
`examples/index.json` as **Cymatic World**, `"live": true`, a `sonic.live` project whose routes read `response.*`.
Owner rules held by design: a world that runs to the horizon in every direction (ground mesh to 2 km, spires and haze
beyond), no vortex hero, sculpted ground, offline look first.

## 5. Risks

- Vertex cost: every terrain vertex evaluates the modes three times (finite-difference normal). If it is expensive,
  bake the field into a texture once a frame (measure first).
- Particles are stateful: an offline render warms up from its start (the existing pre-roll), not exact under seek.
