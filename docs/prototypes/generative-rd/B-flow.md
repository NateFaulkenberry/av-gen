# Experiment B: flow fields and advection

Generator: `tools/generative_rd/flow.py`. Scenes: `examples/experiments/flow-streams/`, `flow-meadow/` and
`flow-storm/`. Engine: ADR-1452 (scalar and vector grids may be planes). Otherwise the work is built from existing
pieces: vector grids with authored values (ADR-1282), semi-Lagrangian advection (ADR-032), fibres along streamlines
(ADR-1180/1181), particles with field forces and trails (ADR-015/040), and the onset field (ADR-1116). Media:
`~/Desktop/av-gen-review/63-generative-rd/B-flow/`.

## 1. The field

One designed current drives every representation. It is the sum of two grids and, optionally, weather:

- **base.** A static vector grid of 96 × 3 × 96 with authored values, computed in numpy:
  - the river follows a meandering channel, z = 18 sin(2πx/110);
  - its profile is parabolic across the channel and slower near the bed (3 m/s mid-stream);
  - it parts round three boulders by 2D potential flow round a cylinder.

  It is a designed composition: the brief's "directed currents" and "attractor-driven flow".
- **wake.** A simulated vector plane of 256² (ADR-1452):
  - three vortex sources shed behind the boulders on small Lissajous paths;
  - a kick's sideways push is injected in the same way;
  - the plane is advected by the whole current, itself included, diffused slightly and dissipated at 0.35/s.

  Eddies are born, drift downstream and fade.
- **gust + chop** (storm only). Two curl-noise fields: a large slow one and a fine fast one. Divergence-free
  turbulence at two scales, over the river.

**Why not a fluid solver.** There is no pressure projection. A designed analytic current plus a dissipating wake
gave the composition control the brief wanted at a fraction of the cost, as the brief predicted. The cost of that
choice: no true vortex streets. The wake's eddies are smooth blobs, not alternating vortices.

## 2. Representations

| config | representation | verdict |
|---|---|---|
| **streams** (Current Lines) | Four rakes of long fibres (70 m, 64 segments) seeded across the channel follow the current's streamlines (the existing RK2 streamline deformer). They bunch where the river narrows, part round the boulders and curl in their wakes. Light pulses travel along them downstream (a cosine palette op over arc length minus time). Amber on black basalt. | The one that makes the flow legible: direction and structure read in a still. Added after review. |
| meadow (Current Meadow) | 64,000 short strands rooted in the riverbed (lifted to the bed by a scalar grid and masked to the channel by another), combed by the current with stiffness and a dead zone, plus motes riding it (particles, velocity mode, trails). | A fur carpet. The current reads only as a texture direction, and the eddies only as small whorls behind the rocks. Weak. |
| storm | The meadow with two-scale curl turbulence. | More visible motion in the strands' tips. Still a texture. |
| ink (dropped) | A 512² dye plane advected by the current, fed by three pulsing springs; petals on the surface. | Dropped after review. The dye did move (a GPU test pins it: a spring's dye is found 27 m downstream after 10 s), but semi-Lagrangian advection at these Courant numbers smears it into a smooth plume. There is no marbling without a higher-order scheme (BFECC or MacCormack). |

## 3. Audio and MIDI (`flow_music()`)

| input | target | effect |
|---|---|---|
| response.bass | The river's strength ×(0.7 + 0.9·bass) | The current speeds and slows as one body |
| kick (low onset) | A ring of sideways push, from upstream into the wake | A travelling disturbance the current carries downstream |
| snare | Shedding strength +0.6 | Eddies spin up behind the boulders |
| response.sustain | Gust strength +1.2 (storm) | Held passages bring weather |
| response.hat | Chop +0.8 (storm); pulse light ×(1 + 2·hat) (streams) | The top end in the fine scale and the carried light |
| MIDI notes 36-38 | The eddy behind boulder 1-3, strength +6 (release 2.5 s) | A held note spins up a vortex at a place |

**Controls (CC):**

| CC | Control | What it moves |
|---|---|---|
| 1 | Flow type | The gusts |
| 2 | Strength | The river's speed |
| 3 | Spatial scale | The gusts' frequency |
| 4 | Audio Influence | The depth of every audio route |
| 5 | Persistence | Lower wake dissipation |
| 6 | Turbulence | The chop |

**Observed:**
- The first amounts (kick push 6, snare shedding +2) tore the streamlines into short tufts. The wake grew so strong
  that every line curled on itself. They are now 1.5 and 0.6.
- The bass-to-strength route is the most physical mapping: the river surges.

## 4. Gaps found (engine)

- **A compound field is typed scalar.** A particle force (which branches on the field's type) reads a compound of
  vector fields as `scalar × axis`, which is 0, so the motes sat still in a pillar. The workaround is that particles
  read the two grids as two forces. Fibres and advection are unaffected (they call `fieldVector` regardless). This is
  not fixed: it is in `spatial::fieldTypeOf` and the GPU type lane, both shared.
- **A field's position.** A node position is folded on top of the field's own frame. A track writing absolute
  positions into `field/<name>/position` doubled them while the node also sat there. The generator now keeps field
  nodes at the origin.
- **Field names containing "."** (for example "a.phase") left the reader fields silently reading 0. Found in A; avoid.
- **A material `field` op on a vector field** returns (x, y, z, 0). It must be squared and dotted for a speed; read as
  a scalar it paints RGB.

## 5. Known issues

- **The meadow and storm** are weak as images (low contrast, small boulders, a narrow channel in a big valley).
  They are kept as hidden harnesses, not as scenes.
- **The streams' composition** depends on the camera. Seen from most places along the valley, the banks hide the
  river. The authored camera is above it.
