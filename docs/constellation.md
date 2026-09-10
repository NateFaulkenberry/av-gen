# The Living Constellation

A particle sculpture built entirely from scene data: fields, splines, GPU particles and the post
chain. No C++ was written for it. `examples/constellation/constellation.json`.

**Status: phase 1 of six.** The structure, the field topology and the core exist and the still
holds up. Audio, camera choreography, morphing and the remaining motifs are not built yet; the
open list is at the end.

## The idea

Particles do not make a sculpture. A *field* makes a sculpture, and particles are how you see it.
Everything below follows from that: the fields are designed as a topology — a spinning disc, a void,
attractors, warps — and the populations exist to reveal it.

## What the first attempt got wrong

Worth recording, because it is the default failure and it took two rebuilds to escape.

**Attempt 1** was an attractor, a vortex and curl noise at comparable strengths, with particles
emitted from one big sphere. It produced a uniform ellipsoid of dots: the exact "generic glowing
point cloud" the brief warns about. The diagnosis was not "not enough particles" — it was that
*the particles had nothing to reveal*. Three forces of similar magnitude average into a ball.

**Attempt 2** made the structural fields dominant and the noise an order of magnitude weaker, and
switched them to `velocity` mode so a particle traces a streamline instead of being nudged off one.
That produced a disc — but a perfectly uniform one, because **shearing something uniform leaves it
uniform**. Differential rotation cannot invent spiral arms; it can only wind up a density variation
that already exists.

**Attempt 3** authors the arms as logarithmic-spiral splines and lets the field animate them. That
is the brief's "clean mathematical structure, then warp the space it lives in", and it is what
finally read as a sculpture.

## The field topology

| field | kind | role |
|---|---|---|
| `discSpin` | vortex about a tilted axis | Differential rotation: full speed at 2.5 m, nothing by 32 m, so adjacent radii shear past one another and wind the arms. |
| `corePull` | attractor at the origin | The inward drift that makes the arms spirals rather than closed rings. |
| `coreVoid` | repulsor, exponential falloff over 4.5 m | The hole. The core reads as an object with space around it instead of as the dense end of a gradient. |
| `nodeA` / `nodeASpin` | attractor + vortex, off-axis | A second orbital system whose plane is deliberately unrelated to the disc's. |
| `macroWarp` | curl noise, f 0.021, speed 0.030 | Tens of seconds. Where the whole organism leans. |
| `mesoWarp` | curl noise, f 0.095, speed 0.20 | Seconds. Bends the streamlines without deciding where they go. |
| `shock` | radial travelling wave | Dormant at strength 0. Phase 4 drives it from onsets: a pulse that *propagates* at 19 m/s rather than a global flash. |

Nothing sits on a world axis. The disc's normal is `[0.24, 0.93, 0.28]`.

**Advection is domain warping.** A particle displaced by the macro field samples the meso field at
its displaced position, so the composition of the two happens through time rather than through
nested sampling. The engine gets recursive coordinate warping for free from the simulation.

## The populations

Five, each with a different visual job and a different representation.

| population | count | representation | job |
|---|---:|---|---|
| `armMajor/Minor/StubP` | 13k / 8k / 3.6k | trails, 12 points, tapered | The arms. Narrow jitter (0.26–0.34 m) and short lives (5.5–9 s, under half a turn) so an arm stays an arm instead of wrapping into an annulus. |
| `discDust` | 26k | point sprites | The substrate the arms sit in. Alpha 0.085 — a suggestion of a disc, not the disc. |
| `coreKnot` | 7k | tiny, emissive 16, `volumeGlow` 1.0 | The focal point. Held inside the repulsor so it churns rather than orbits. The only population near white. |

The arms are unequal on purpose: 27.6 m, 17.2 m and 9.6 m outer radius, at three brightnesses. One
dominant, one counterpart, one that never completes a turn.

## Measured

M2 Max, realtime tier, gpu frame median:

| resolution | gpu frame | FPS |
|---|---:|---:|
| 1280 × 800 | 3.87 ms | 258 |
| 1920 × 1200 | 5.64 ms | 177 |
| 2880 × 1800 | 10.88 ms | 92 |

Fits **t = 2.1 ms + 1.7 ms per megapixel**. At full retina the frame is 10.9 ms against a 16.67 ms
budget, so the scene meets 60 FPS at native resolution with room for the phases still to come. For
comparison, Glowmere is 42.3 ms at the same size: this scene is about four times cheaper because
its complexity is particles and fill rather than 15 million triangles.

Pass breakdown at 1280×800: volume 2.10, scene 1.18, particles 0.46, bloom 0.13, fxaa 0.07,
tonemap 0.07. The volumetric march is the largest single cost and is the first place to look if the
budget tightens.

**Deterministic**: two runs of frame 900 differ by 0 of 1,024,000 pixels. The scene round-trips
through save/load with all 16 nodes and its post block intact.

## Reproduce

```sh
cmake --build build/release -j8
./build/release/src/avgen --headless --project examples/constellation/constellation.json \
    --frames 900 --fps 30 --size 1280x800 --tier realtime --capture out/constellation.png
```

## Not built yet

- **Phase 2** — orbital population around `nodeA`, the veil layer, colour depth. The palette is
  currently one blue-white family; the brief asks for indigo → violet → cyan → white with magenta
  used sparingly, and for colour to encode distance.
- **Phase 3** — camera choreography, bursts.
- **Phase 4** — audio. `shock` is authored and dormant, waiting for an onset route.
- **Phase 5** — the timeline, morphing between macro states, the 60–90 second piece.
- **Phase 6** — a measured performance pass with populations at their final counts.
- **Shards** (motif 6) and the SDF core are not in yet; the core is currently a particle knot,
  which may prove sufficient.
- Composition: the major arm exits the frame at bottom right in the current camera. Fine for a
  test still, wrong for a final frame.
