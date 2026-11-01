# CHRONOS Gate 1: the ring, the slit scan and the canyon

Phases 1 and 2 of `03-plan.md`. Media is in `~/Desktop/av-gen-review/104-chronos/gate1/`. ADRs 1930-1933.

## What landed

| ADR | What |
|---|---|
| 1930 | The RTIN mesher moved into a shared module (`scene::rtin`). It is behaviour-neutral: the pinned mesh hashes match, and Strange Attractor's flythrough is byte-identical over 960 frames. Handed to the coordinator early, as 3d73b32c, with both suites at exit 0. |
| 1931 | **The ring:** a fixed capacity (up to 256 layers) separate from the routable span; a capture stride anchored to the timeline frame; per-layer metadata. **The sampler:** the slit scan as a time map, with direction, radial or luminance modes, strips, edge, blend, and a moving scan. **The leak fix:** debug geometry now draws after the capture. |
| 1932 | The render job's temporal pre-roll, which ADR-410 specified and nobody built. |
| 1933 | The `chronosCanyon` block: a canyon carved along an authored river. |

## The proof the engine reaches proof B's structure

- **Tests** (`[gpu][temporal][adr1931]`, `[adr1932]`, `[temporal][adr1931]`, `[chronos-canyon][adr1933]`,
  `[terrain-rtin][adr1930]`):
  - **Amount 0:** a slit at amount 0 on a 64-layer ring is byte-identical to no slit.
  - **FIR at 64 frames:** a renderer that starts 64 frames before frame 80 draws frame 80 identically, and one that
    starts 20 frames before does not. That second check proves the first could fail.
  - **The edges:** the vertical slit's left column is the current frame; the horizontal slit's top row is; reversed,
    the right column is.
  - **Strips:** quantising into strips changes the picture.
  - **No reallocation:** a span routed every frame at a fixed capacity allocates once and never drops the history.
    At capacity 0 it reallocates more than 10 times (the old behaviour, kept).
  - **Stride:** a stride-4 ring lands where a play does, and differs from stride 1.
  - **Repeat:** a repeated strided frame reproduces.
  - **The leak:** a debug box drawn on every frame but the last leaves the last frame's slit byte-identical to a run
    with no box, so the overlay is not in the history.
  - **Pre-roll:** a render of 1.5-2.0 s agrees with a render of 0-2.0 s at 1.5 s within the control's margin. The
    control is the same pair with no temporal effect, which differs by up to 3 LSB in a few pixels for reasons outside
    the ring. With the pre-roll switched off (`AVGEN_NO_TEMPORAL_PREROLL=1`), the two differ by more than 16.
- **The clips** (40-100 s of the flight, with sound, offline tier, 128-layer full-resolution ring, pre-rolled):

  | Clip | Settings | What it shows |
  |---|---|---|
  | `chronos-g1-slit-horizontal.mp4` | each row a moment, now at the top, 3 s down the frame | The floor, the river and the shadows fold and stretch under the walls. The clearest of the set. |
  | `chronos-g1-slit-strips.mp4` | the vertical map quantised into 24 hard strips | Crisp temporal steps, strongest where something moves across the frame: the spires' shadows break into a saw of stepped copies. |
  | `chronos-g1-slit-vertical.mp4` | each column a moment, now at the right, 4 s across | Subtle on this stretch. The camera dollies straight down the canyon's axis, so neighbouring columns differ little. |
  | `chronos-g1-slit-vertical-long.mp4` | stride 2, 250 frames (8.3 s) across | The same map over twice the time; turns and lateral passes make the walls shear. |

**The honest gap against proof B.** Proof B's flight banked and swung through Strange Attractor's turns, and a
vertical slit scan is made of lateral motion. CHRONOS's river pace is mostly a forward dolly, so the vertical map has
less to work with than in proof B. The horizontal map and the strips already read as structure. The vertical map will
come alive where the Phase 5 camera turns, banks and passes things sideways, and with the directional slit and the
moving scan (Gate 2). This is a camera note, not a renderer one: the sampler is the same.

## The canyon

`examples/chronos/build_scene.py` writes the scene, the project and the audition projects. The scene is not in
`examples/index.json`. To open it:

```
tools/gpu-lock.sh build/release/src/avgen --project examples/chronos/chronos.json
```

**Layout.**
- A 13 km river meanders from south-west to north-east.
- The canyon is 320 m deep and up to about 1 km across at the rim.
- Each 48 m tier is mostly cliff and bench (riser share 0.16), with uneven tier thicknesses.
- The walls have broad bays and a smaller buttress rhythm, and gullies.
- Sandy banks run beside the river.
- Seven spires stand beside the flight.
- The far ranges (to 1.3 km) close every horizon.

**Light.**
- A 14° warm sun from the side.
- Violet fills, a magenta-gold horizon and a cirrus sky.
- The walls use `chronosRock`: Strange Attractor's `canyonRock`, copied as scene art, in the CHRONOS palette with
  softer laminations.

**Flight.** One authored spline of 25 points along the river's centreline, mapped to the song's acts by
`camera/splineT` keys:
- I: above the rim, 0-20 s;
- II: the descent, 20-48 s;
- III and IV: 12-20 m above the water, through the walls and past the spires, 48-167 s;
- V: lifting and carrying on.

**Numbers.** 1.90M triangles, 51 MiB, generated in 7.5 s. The mesh is watertight (tested).

### Camera jitter (the coordinator's frame-difference metric)

`chronos-g1-jitter.txt`.
- **The first render** had a periodic stepping: the spline camera's arc-length table is piecewise linear, about 11 m
  per sample at the default density, so the aim changed every ~8 frames. Max 0.398, p99 0.156.
- **Fix, scene-side:** `samplesPerSegment` 256.
- **The final render:** median 0.005, p90 0.016, **p99 0.042, max 0.104** (11.4 s). Every act is under 0.11.
  Strange Attractor's accepted canyon clips ran p99 0.03-0.11 and max 0.06-0.25.

### Memory and GPU (`chronos-g1-memory-and-gpu.txt`)

**The temporal system costs 0.10 ms at 1080p:** sample 0.09 ms and capture 0.01 ms (High tier, half-resolution ring,
128 layers).

| Ring | Memory |
|---|---|
| Live (half resolution) | 265 MB |
| Offline (full resolution) | 1.06 GB, as approved |

**The canyon frame is the live problem:** 60.6 ms median at High. Of that:
- 39.8 ms is the volumetric fog march;
- 19.3 ms is the scene pass.

Live 1080p30 is Gate 3's job, and the fog march is the first lever.

## Known weaknesses (for Phase 5's art pass)

- **The benches' edges are still a little blocky** in the high establishing shots (the tier quantisation meets the
  buttress noise).
- **The sandy floor is flat in colour.**
- **There is a sawtooth line along some wall feet,** where the shadow meets the bank.
- **No arches or suspended formations yet.** A height field cannot make them. They are separate meshes, a later phase.
- **The river is a flat sheet with no flow.** It reflects well.
- **The establishing shot's canyon reads small** against the plateau.
