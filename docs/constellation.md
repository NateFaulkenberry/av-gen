# The Living Constellation

A particle sculpture built entirely from scene data: fields, splines, GPU particles and the post
chain. No C++ was written for it. `examples/constellation/constellation.json`.

**Status: phases 1–4 built, plus a 90-second cut.** Structure, field topology, core, a second
orbital system, the veil, colour depth, a thirteen-route audio mapping with propagating shockwaves,
and a nine-key camera journey. Morphing between macro states and the shard motif are not built; the
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

## Audio

Thirteen routes. The rule is that each musical feature drives a different *physical* phenomenon on
its own time constant; nothing is routed to global brightness.

| feature | drives | attack / decay |
|---|---|---|
| `audio.rms` | `corePull` strength, veil emission — the organism's grip and the air's glow | 1400 / 3000 ms |
| `audio.bass` | core extent and emission, `coreVoid` strength — the core swells and the void it pushes against widens, deforming the inner disc | 90–180 / 620–900 ms |
| `audio.onset` | **`shock` strength** and a core burst | 5–10 / 240–900 ms |
| `audio.mid` | bridge spawn rate, `mesoWarp` strength — the tendrils writhe | 220–320 / 1100–1400 ms |
| `audio.treble` | arm spawn rate, core turbulence — micro activity only | 70–90 / 600–800 ms |
| `audio.spectralCentroid` | `post/grade/hueShift`, bipolar — where the palette sits | 2200 / 3600 ms |
| `beat.pulse` | `discSpin` strength — the rotation itself pulses | 60 / 520 ms |

**The shockwave is the point.** An onset raises the strength of a radial travelling `wave` field
centred on the core. The wave moves outward at 19 m/s with a 12 m front, and the populations sample
it as a force. A beat therefore reaches the core first and the rim of the disc about a second and a
half later: the sound is seen crossing the sculpture rather than flashing it.

Proof the routes are live rather than silent: the same frame rendered with `routes` emptied differs
in 612,814 of 1,024,000 pixels.

## Camera

Nine keys over ninety seconds, smooth-interpolated: a wide hold at 110 m, a slow approach, a
descent into the disc's plane, a pass through the inner region beside the core, then a climb out
and away to a far hold. Measured across the cut, mean frame luminance runs 0.022 → 0.039 → 0.153 →
0.438 → 0.081, which is the shape the brief asks for: near-empty opening, build, climax, release.

## Measured

M2 Max, realtime tier, minimum gpu frame median over three interleaved rounds (system load ~3.6
during measurement, so these are conservative):

| resolution | gpu frame | FPS |
|---|---:|---:|
| 1280 × 800 | 4.39 ms | 228 |
| 1920 × 1200 | 6.88 ms | 145 |
| 2880 × 1800 | 12.78 ms | 78 |

Fits **t = 2.3 ms + 2.0 ms per megapixel**. Full retina is 12.8 ms against a 16.67 ms budget, so the
complete scene — every population, audio, camera — holds 60 FPS at native resolution.

The 90-second cut renders at 1920×1080 in 32 s (84 fps), audio muxed. At full retina the frame is 10.9 ms against a 16.67 ms
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

- **Morphing between macro states** (brief §22) is not built. The sculpture is one topology
  throughout; sphere / flower / vortex / collapse transitions would need the field strengths
  animated on the timeline, which is authorable today and simply has not been authored.
- **Shards** (motif 6) are not in. The core is a particle knot rather than an SDF, and so far that
  reads better than a raymarched primitive would — worth leaving unless a close pass exposes it.
- **Trails could be longer on the bridge** and shorter on the arms; the current single trail length
  per population is a blunt instrument.
- **The node knot is still slightly diffuse** at close range. It reads as an orbital system in the
  wide shots and as a smudge when the camera is inside 15 m.
- **The palette is narrow.** Indigo through cyan to white with one violet organ, which is coherent,
  but the brief's plasma and alien palettes are unexplored and nothing yet drives palette *state*
  from musical section.
