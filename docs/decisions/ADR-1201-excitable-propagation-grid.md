# ADR-1201: An excitable propagation grid

- Status: Accepted (proto/bioluminescent-prop)
- Extends ADR-032 (simulated grids), ADR-1119 (per-step inputs, GPU checkpoints), ADR-1120 (plane grids up to
  1024 x 1 x 1024), ADR-1122 (a grid's behaviour is parameters) and ADR-1163 (`ceiling`).
- Found by: The Rift (`docs/prototypes/bioluminescent/00-brief.md` §9). The brief wants a musical event to be an
  impulse placed in the world that propagates through the ecosystem. Organisms respond by distance, threshold,
  local density, accumulated energy and refractory behaviour, and the response decays naturally: "music traveling
  through a living ecosystem", not every plant lighting at once.

## Problem

No grid mode propagates. A scalar grid spreads by diffusion, which smears out and has no speed. Its front widens as
√t and fades with distance. Reaction-diffusion makes standing patterns. A `wave` or `onset` field travels at a set
speed, but it is analytic: it crosses barren rock as readily as a moss bank, it has no memory, and it cannot refuse
to fire twice. The brief needs the following:

- fronts that travel at a speed an artist sets, in metres per second;
- a medium that follows the organism density, carrying along banks and ledges and stopping at rock;
- refractoriness: cells behind a front cannot be relit at once, so fronts are rings, not floods;
- slow accumulators ("energy", "wake") that something else, such as a canopy, can respond to later.

## Decision

`GridMode::Excitable` (`"mode": "excitable"`) is a plane grid (`[x, 1, z]`, 2 ≤ x, z ≤ 1024). Each cell holds
4 floats:

| ch | name | range | meaning |
|---|---|---|---|
| 0 | `u` | 0..1 (≤ `ceiling`) | excitation, the visible activation of a front |
| 1 | `r` | 0..1 | refractory: 1 when the cell fires, then `exp(-age / refractoryTime)`; 0 = never fired |
| 2 | `e` | 0..1 | energy: `u` low-passed with `energyTime` |
| 3 | `w` | 0..1 | wake: `u` low-passed with `wakeTime` (tens of seconds) |

### The step (dt = 1 / simRate), per cell, gather-only

`r` is also the medium's **clock**. A neighbour's time since it fired is `age_j = -refractoryTime · ln r_j`.

```
S        = injectRate · max(0, injectField(centre))                  (a level, not a rate)
c        = conductivityField empty ? 1 : max(0, conductivityField(centre))
jitter   = 1 + noise · (2 · hash(cell, ⌊step / round(0.25 · simRate)⌋, seed) - 1)
for each neighbour j in the 5 x 5 disk (0 < dx² + dz² ≤ 5; 20 cells), skipped past a clamp edge:
    d        = |(dx · cellSize.x, dz · cellSize.z)|                          metres
    arrived  = age_j + dt - (d / waveSpeed) · jitter                         how long ago j's front reached here
    if 0 ≤ arrived < 2·dt:  best = max(best, arrived)
age      = r > 0 ? -refractoryTime · ln r : ∞
front    = best ≥ 0  and  age + dt > best + 1e-5                    (one front never fires a cell twice)
fire     = S + coupling · c · [front]  >  threshold · (1 + refractoryStrength · r)
if fire:   ageNow = front ? best : 0;   r' = exp(-ageNow / refractoryTime)
else:      ageNow = age + dt;           r' = r · exp(-dt / refractoryTime)
u'       = (ageNow - dt < 1 / riseRate) ? max(u, min(1, riseRate · ageNow))   (rising: u reaches 1 in 1/riseRate s)
                                        : u · exp(-dt / excitationDecay)
u'       = ceiling > 0 ? min(u', ceiling) : u'
e'       = e + (u' - e) · (1 - exp(-dt / energyTime))
w'       = w + (u' - w) · (1 - exp(-dt / wakeTime))
every channel below 1e-20 is 0       (Metal flushes denormals; the CPU matches)
```

### Calibration of the wave speed

A front from j reaches a cell `d / waveSpeed` seconds after j fired, and the cell's own `r` is set from that exact
arrival (`ageNow = best`), not from the step boundary. So no step quantises the front. The front's arrival time at
any cell is the shortest path over the stencil graph, with edge weight `d / waveSpeed`. The window is two steps
wide because a neighbour that fired during the previous step is only visible one step later. A cell accepted a step
late gets the same sub-step arrival, so even a flip at the window's edge leaves no trace (measured below).

As a result the speed does not depend on cell size or simRate. The remaining error is the stencil's anisotropy:
along the axes and the (2, 1) and (1, 1) diagonals the path is exact, and between them it is at most 2.7% long.
Measured with the CPU mirror, from one impulse over 4 s:

| cells | cell size | simRate | waveSpeed | fitted speed | ring radius (expected) |
|---|---|---|---|---|---|
| 128² over 32 m | 0.25 m | 60 Hz | 3 | 2.958 (-1.4%) | 12.0 m (12) |
| 96² over 32 m | 0.33 m | 120 Hz | 3 | 2.958 (-1.4%) | 11.93 m (12) |
| 64² over 32 m | 0.5 m | 30 Hz | 3 | 2.958 (-1.4%) | 12.0 m (12) |
| 128² over 64 m | 0.5 m | 60 Hz | 7 | 6.903 (-1.4%) | 28.0 m (28) |

By direction, speed is between 0.973 and 1.000 of `waveSpeed`. A numpy prototype measured that range, and the
test bounds it to [0.96, 1.01].

- **Fastest front:** at most 2 cells per step on an axis, so `2 · cellSize · simRate` (30 m/s at 0.25 m and
  60 Hz). Above that the front is capped.
- **Slowest front:** `r` must still hold the age when the front arrives:
  `√5 · cellSize / waveSpeed < 46 · refractoryTime` (r flushes at 1e-20). At 0.25 m and τ = 1.5 that is
  0.008 m/s.
- **Lowering simRate does not change the speed.** It is therefore the cost knob, down to the cap above.

### What is behaviour (parameters) and what is layout

Live parameters, in `grid/<name>/<leaf>` (ADR-1122). They reach scene JSON, routes, MIDI, the CLI (any scene
load) and the editor's generic parameter panel:

| leaf | default | hard range | soft range | |
|---|---|---|---|---|
| `injectRate` | 1 | 0..1000 | 0..10 | the stimulus gain |
| `threshold` | 0.5 | 0.0001..100 | 0.05..2 | the drive a rested cell needs |
| `coupling` | 1 | 0..100 | 0..3 | the drive a passing front gives (× conductivity) |
| `waveSpeed` | 4 | 0.001..1000 | 0.25..30 | m/s |
| `riseRate` | 8 | 0.01..1000 | 0.5..60 | 1/s; u reaches 1 in 1/riseRate s |
| `excitationDecay` | 0.6 | 0.001..100 | 0.05..5 | s |
| `refractoryTime` | 1.5 | 0.001..100 | 0.1..10 | s, also the clock |
| `refractoryStrength` | 4 | 0..1000 | 0..20 | |
| `energyTime` | 3 | 0.001..1000 | 0.25..20 | s |
| `wakeTime` | 30 | 0.001..10000 | 1..120 | s |
| `noise` | 0 | 0..1 | 0..1 | front-delay jitter |
| `ceiling` | 0 | 0..1000 | 0..4 | caps u (0 = none) |

The advect, diffusion and dissipation leaves are registered for every grid and are ignored by this mode, as Rd
ignores `advect`. Layout (it re-seeds the state): mode, wrap, resolution, bounds, simRate, seed, `injectField`
and **`conductivityField`**. JSON writes and hashes the excitable keys only for an excitable grid, so every other
grid keeps its bytes and hashes. `seedAmount` must be 0, because `r` is a clock and noise in it would be fronts
that fired at random moments.

```json
"grids": [{
  "name": "prop", "mode": "excitable", "wrap": "clamp",
  "resolution": [1024, 1, 1024], "boundsMin": [-128, -1, -128], "boundsMax": [128, 1, 128],
  "injectField": "kick", "injectRate": 1.0, "conductivityField": "density",
  "threshold": 0.5, "coupling": 1.0, "waveSpeed": 4.0, "riseRate": 8.0, "excitationDecay": 0.6,
  "refractoryTime": 1.5, "refractoryStrength": 4.0, "energyTime": 3.0, "wakeTime": 30.0, "noise": 0.2,
  "ceiling": 0.0, "simRate": 60, "maxSubSteps": 4, "seed": 11, "checkpointInterval": 5
}]
```

### Reading a channel

`FieldSpec::channel` (`"channel": 0..3`, integer) makes a Grid field read that component of its grid's cells as a
scalar. It works for any grid mode: 0..3 of an agents grid are the three trails and their sum. The default is -1,
which keeps the grid's own reading. For an excitable grid that reading is `u`, so
`{"kind": "grid", "reference": "prop"}` still means "the excitation".

```json
{ "name": "propWake", "kind": "field", "field": { "kind": "grid", "reference": "prop", "channel": 3 } }
```

On the GPU the channel rides in `FieldGpu::gridRes.w`, which already held 1 (bound) + 2 (wrap). It is now
`+ 4 · (channel + 1)`. `FieldGpu` has no spare lane (ADR-1163), and the default leaves the lane as it was, so no
layout, block size or other shader changed. The channel is written to JSON and hashed only when it is set.

### GPU

`cs_excite` in `shaders/simulate.wgsl` is the whole step, one dispatch. Each workgroup handles an 8 x 8 tile and
loads the tile's `r` and a 2-cell halo into workgroup memory once. `SimUniforms` grows from 160 to 224 bytes
(`excite0..2`, `exciteSlots`), still inside its 256-byte dynamic-offset slot.

The step reads the ADR-1119 per-step field block (its own second, and the step index in `pad0` for the noise). It
checkpoints and restores like every other grid, so a seek equals play, byte for byte.

## Cost

GPU time of the simulate pass, measured with `gpu::FrameTimeline`. Each frame runs 8 steps, and the figure is the
median of 60 frames on the M2 Max under `tools/gpu-lock.sh` (2026-10-06 22:41). Hidden case:
`avgen_render_tests "Excitable grid cost per step"`.

| grid | quiet (no activity) | busy (every cell firing, stimulus, noise) | busy + an fbm `noise` field as conductivity |
|---|---|---|---|
| 512 x 1 x 1024 | 0.29 ms/step | 0.24 ms/step | 0.43 ms/step |
| 1024 x 1 x 1024 | 0.41 ms/step | 0.46 ms/step | 0.85 ms/step |

- **The conductivity field's own evaluation is half the busy cost.** An fbm `noise` field is evaluated per cell
  per step. A cheap field (a sphere, a plane, a grid) costs far less.
- **The quiet 512 figure is the first case run.** It is noisy (1.6..2.9 ms per 8 steps) while the GPU clocks up.
- **Before tiling,** with per-neighbour global loads and integer modulos, the 1024² quiet case was 0.69 ms/step.
- **Steps per frame scale the cost.** At simRate 60 and 60 fps a frame takes one step. At 30 fps it takes two.
  simRate 30 halves the cost without changing the speed (see the cap above).
- **State:** 16 MB (4M floats) at 1024², the whole grid table. One 1024² excitable grid leaves no room for another
  grid. 512 x 1024 uses half.

## Tests

- **`avgen_tests [excitable]`:**
  - JSON round trip of the mode and every leaf, with the hash split;
  - validation (plane, ranges, `seedAmount`);
  - the impulse ring at waveSpeed within 10% across four cell-size and simRate combinations, with isotropy bounds;
  - refractory blocking a second stimulus 0.5 s behind a front, and the control (recovered, the same stimulus
    fires);
  - conductivity 0 stopping a front, with an unbarred control;
  - energy and wake rising to 1 - 1/e and decaying to 1/e in their time constants;
  - noise determinism;
  - the channel selector on the CPU, in JSON and in packing;
  - a scene JSON registering every `grid/prop/<leaf>`, a live `waveSpeed` change reaching the grid without
    re-seeding it, and a grid field with `"channel": 3`.
- **`avgen_render_tests [excitable]`:**
  - **GPU against the CPU mirror** after 180 steps of a pacing stimulus behind a conductivity barrier. Without
    noise the worst difference is 6.6e-5 and every float is within 1e-3. With noise 0.4, 18 of 65,536 floats are
    beyond 1e-3 (worst 0.025); see the limitations.
  - **Seek equals play:** an onset-driven medium with noise, played at 30 fps. A fresh seek to 7 s and a
    checkpoint restore after scrubbing back from 11 s both match the played state with 0 differing floats.
  - **Channels on the GPU:** a Grid field with channel -1, 0, 1, 2 and 3 matches the CPU sampling of the GPU's own
    cells within 1e-5, and the default reading equals channel 0.

## Where the spec was changed, and why

- **The neighbour drive is "a front arrived", not "the neighbours' u".** If u drove the neighbours, the speed would
  depend on threshold, coupling, riseRate and the cell size together, and could not be calibrated. Here
  `threshold`, `coupling`, conductivity and the refractory state decide **whether** a front fires a cell, and
  `waveSpeed` alone decides **when**.
- **`r` jumps to 1 when the cell fires**, rather than rising with u. Its exponential decay is the clock that makes
  the speed exact.
- **e and w are low-passes of u** (bounded 0..1, "the fraction of recent time this region was lit"), not max-hold.
  They are linear and their time constants are directly testable.
- **The noise is re-drawn every 0.25 s, not every step.** Per-step draws would make the front's roughness, and its
  bias, depend on simRate. The 0.25 s period is a whole number of steps, so it is deterministic and seekable.
- **`injectRate` is a level gain** (S is compared against the threshold), not `× dt`.
- **Conductivity scales the front's drive only.** It does not scale the stimulus or the speed. To keep stimulus
  off the rock, multiply the inject field by the density with a compound field.
- **`ceiling` caps u only.** Capping r would break the clock, and e and w follow u.
- **`channel` is an integer only, with no `"u"`/`"r"`/`"e"`/`"w"` names.** Two spellings would be an alias (no
  compat shims, ADR-441).

## Limitations

- **With `noise` > 0 the CPU mirror is not exact.** A front accepted a step later on one side, because two `log()`
  results differ by an ulp at the window's edge, can land in the next noise draw. That cell's arrival then differs
  by the jitter: about 1 float in 3,600 after 3 s. GPU against GPU (play, seek, restore) stays bit-exact.
- **Barriers must be wider than the reach.** The stencil reaches √5 cells, so a gap of zero conductivity
  narrower than about 2.3 cells is jumped.
- **Refractoriness is what stops a front echoing back.** With `refractoryStrength` 0 or a very short
  `refractoryTime`, a front can re-fire the cells behind it and the medium rings. A constantly stimulated cell with
  no refractory resistance re-fires every step and never emits a front.
- **`refractoryTime` is also the clock.** Changing it live re-times fronts already in flight; their ages rescale.
  Like every ADR-1122 behaviour leaf, a modulated value is sampled at the frame, not the sub-step.
- **Fronts all travel at one speed.** Conductivity does not slow them.
- **The CPU mirror is O(cells x 20) per step**, for tests and tools only.
- **Not reached:** the node-graph field builder (`graph/builtin_nodes.cpp`) does not expose `channel`. Scene JSON
  and code do.
