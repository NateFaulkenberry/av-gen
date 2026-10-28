# Canyon Phase 2: geometric revelation

This implements the canyon brief's §5 (the hidden mathematics) and §6 (musical behaviour), following Phase 1c without
a stop, as the coordinator asked. Media is in `~/Desktop/av-gen-review/101-strange-attractor/canyon-p2/`.

| File | What it is |
|---|---|
| `00-audio-reactive-46s-1080p30-with-sound.mp4` | The 46 s flythrough on Feline Footwear (0–46 s), 2x supersampled, with sound |
| `01-mode-C-*.png` | The revelation front after the accent at 20.13 s, four moments |
| `02-mode-B-*.png` | The topology rising on the bass phrase, 10–13 s |
| `03-mode-A-*.png` | Contours on the high band, 33–38 s |
| `04-full-frame-24.5s.png` | One full frame |
| `05-signals-and-route-finals.png` | Each signal against the parameter it drives |
| `06-perf-flicker-latency.txt` | Frame times, the flicker metric, and latency |

## Start of phase: the ray-march is gone (ADR-1861)

1b and 1c are the accepted foundation, so these are deleted:
- the renderer and its shader;
- the `"mesh"` switch;
- the ray-march-only look keys and bake products;
- the vista scene.

The gate2e clip stays as the reference.

## Research: what the engine already had

ADR-1073's wire lines draw a procedural node's edges as screen-space quads from a CPU-extracted edge list. Mesh
entities were explicitly left out. Its edge vertex format is 224 bytes per edge, which would be about 3 GB for this
terrain's 12.4M edges.

Three options were weighed (brief §5.2):

| Option | Verdict |
|---|---|
| A barycentric attribute | Needs a de-indexed copy, 3x the vertices. A 3-colouring of shared vertices does not exist for RTIN's odd-degree vertices. |
| A screen-space post effect | The brief warns against it, and it would not be the real topology. |
| Reading the triangles in place | Chosen. One instance per triangle; the vertex shader reads the mesh's own index and vertex buffers as storage. |

## The system (ADR-1862, generic engine; ADR-1863, op budget)

**`meshWire`** is a scene-level block that targets entities by name prefix. One system gives the three modes of brief
§5.4:
- **Mode A, contour tracing.** Iso-height lines every 15 m, with every fifth a major line, drawn in the rock program's
  emission. They follow the real surface per pixel and fade out by 900 m so they cannot alias. Their strength is
  `attractorWorld/contour`, carried to the program on the chunks' material emission. This needed the 96-op budget
  (ADR-1863): the program is 85 ops.
- **Mode B, triangular revelation.** `meshWire/intensity` with `reveal` 1 shows the mesh's real triangles, 2 m on the
  near walls.
  - A distance fade, 120–1400 m.
  - A screen-density fade: a triangle smaller than 14 px fades out.
  - A 3 m shore fade above the water line.
  - Together these keep it a lattice, never a luminous mass.
- **Mode C, geometric dissolution.** `meshWire/wave` is an envelope. A ring 250 m ahead of the camera expands to
  1100 m as the envelope falls, and lights every edge it crosses, then the rock returns. It is a pure function of the
  parameter, with no renderer state, so it is seekable and independent of frame rate. The geometry is never touched.

Structure-only edges are still available with `reveal` 0: an edge's weight is how much the surface turns there. I
tried it as the default (crease 1–8° and 2–12°). On this terrain it picks out the shoreline zigzag and rubble rather
than the strata, and it read as scattered straw. The shipped mapping uses the full topology, gated by bass, instead.

The brief's restraint list (§5.3), item by item:

| Item | How it is met |
|---|---|
| Thin, stable lines | 1 px at 1080 lines, antialiased |
| Distance fade | Yes |
| No luminous mass | The density fade |
| Behind the material | Lines are added light at modest strength |
| No z-fighting | ADR-1073's pull toward the eye |
| No full-screen post | It is a geometry pass |
| No chunk seams | Each edge has one owner, in world-space order |
| No white lines | Warm orange (1, 0.45, 0.15) |
| Legible on lit and dark rock | Checked in the 01/02 sheets |

## Musical behaviour (ADR-1862's routes)

The project is `examples/strange-attractor/strange-attractor.json`, written by `build_project.py`. It uses:
- Feline Footwear, plus its drums, bass and synth stems with their roles;
- the beat clock from the drums stem (`feeds`);
- the current signals only. There is no drum classifier.

| Signal | Kind | Target | Shaping |
|---|---|---|---|
| `response.onsetLow` | event | `meshWire/wave` | Binary at 0.8, linear fall over 2 s, refractory 3 s, re-arms below 0.3. That gives 7 fronts in 46 s. |
| `response.bass` | continuous | `meshWire/intensity` | Top quarter only, because this track's bass averages 0.78. 0–0.45, attack 1.2 s, release 3 s. |
| `response.high` | continuous | `attractorWorld/contour` | Above 0.4, mapped to 0–0.7; attack 250 ms, release 1.2 s. |

Things deliberately not done:
- **No pulse on every transient.** The front fires only on strong low onsets, at most every 3 s.
- **No routes on beat.\*.** The clock is plotted for reference only, to keep the scene from locking to the grid.
- **No wall glow route.** The `glow` lift washed the shadows out, so it rests at 0. The parameter is kept as a live
  knob.

**Latency is 0 frames.** The wave's final value jumps on the same frame `response.onsetLow` first crosses 0.8, for all
seven events. This was checked with `--sonic-trace --trace-finals`.

All shaping is in the route chain (threshold, envelope, refractory, attack and release, all in milliseconds), so it is
tunable without recompiling and independent of frame rate. There is no scene-side buffering.

## Measurements

- **Flicker** on the audio-reactive clip (coordinator's metric): median 0.007, p90 0.026, p99 0.082, max 0.18. The
  fronts and the topology fade in and out without a frame-level pop.
- **Frame time**, 1080p ultra on an M2 Max:
  - At rest: GPU 78.8 ms; the scene pass is 45.0 ms, clouds 19.6 ms, fog 10.4 ms.
  - Fully on (wire + wave + contours): GPU 83.2 ms; the wire adds about 3.4 ms.
  - The 85-op program costs about 10 ms more than 1c's 63 ops even at rest.
  - None of this is live-ready (art first). The levers are a gated contour layer, cheaper clouds (ADR-level), and a
    lower `fadeFar` for the wire.

## Open

- **Structure-only mode** needs a better measure of structure on this terrain. Candidates are the strata ledges'
  band lines, or the RTIN level.
- **Wide shots (40 s on).** The wire is invisible there by the density fade: far triangles are small. A front there
  would need a coarser view of the topology, for example the RTIN's larger triangles only.
- **Skinned meshes** get no wire: it reads the bind pose.
