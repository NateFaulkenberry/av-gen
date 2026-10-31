# CHRONOS Gate 0: plan

The recon is `02-recon.md`. This document covers:

- the temporal imaging architecture, as an extension of the existing ADR-410 family;
- where it plugs in;
- the budgets;
- the phase plan against Gates 1-3;
- the music;
- what the owner needs to decide.

ADR range 1930-1969.

## 1. The architecture

### 1.1 The principle

The existing family is right about the one thing that matters most: **effects read a ring of clean frames, never
their own output.** That makes every effect an FIR filter. Play rebuilds it exactly, and two renders of a range agree.

CHRONOS keeps that rule and generalises the parts that are too small or too fixed:

- the ring's length and how it is sized;
- what is stored per frame;
- how effects address the past.

There is **one** new store (the strip ring) and **one** new pass (the composed sampler). The existing `fs_slit` becomes
the sampler, and no example uses it today, so nothing migrates. Echo and mosh are untouched. Feedback gains an
accumulate mode, which needs the owner's decision (§6).

Everything is engine code in `src/rendering/temporal_*`, `shaders/temporal.wgsl` and `src/scene/temporal_settings.*`,
reachable as `temporal/<group>/<leaf>` parameters from any scene. CHRONOS owns only its project: values, routes,
timeline and camera.

```
lit HDR ─► [capture, last in the stage] ──► colour ring (2D array, capacity K, stride s) + per-layer metadata
       │                               └─► strip ring  (one slit line per frame, N columns)      [Phase 3]
       │
       ├─► echo (unchanged)
       ├─► SAMPLER: one full-screen pass. Per pixel it composes an ADDRESS into the past:
       │      age(uv)    = slit map (axis at any angle | radial | luminance | depth*)
       │                   quantised into strips, scan offset moving at a velocity
       │                 + fracture: per-region age offset and tear (bands | warped cells), evolving
       │      age_rgb    = age + displacement offsets (R, G, B), with a spatial offset per channel
       │      source     = colour ring | strip ring (slit photography)
       │   then fetches each channel once (two layers, blended), and mixes with the current frame
       ├─► feedback: FIR (today) | accumulate (ping-pong, bounded), reading the sampler's output
       ├─► mosh (unchanged)
       └─► post chain ─► tone map
```

`*` A depth-keyed slit (far = older: time recedes into the distance) needs a small depth channel. It is optional, in
Phase 3, and only if it earns its memory.

### 1.2 Why one composed sampler, not one pass per effect

Slit scan, displacement and fracture are all the same operation: **choose which moment each pixel shows, then fetch
it.** They differ only in how the moment is chosen. As separate passes they would cost three full-screen 1080p
RGBA16F round trips, each resampling the last's output: resampling a resample blurs, and the order matters. Composed
as one address, they cost one pass and at most six ring fetches per pixel. They combine without conflicts (brief §9,
Phase 3 exit criterion) because they add into one address rather than stacking images.

Each term is gated, so a term at zero costs nothing, and the pass is not encoded at all when every term is zero:
byte-identical frames, as today. Feedback stays a separate pass, because it is the only stage that reads its own
past.

### 1.3 The ring: capacity is not span

Today the ring's depth is the max of the effects' `frames`. A route on `frames` reallocates the texture whenever it
crosses an integer, and dropping the history at that moment is exactly the musical one. The fix:

- **`temporal/history/capacity`** (frames, 1..256): structural, like a world-generation knob. It allocates; changing
  it resets. The tier caps it (`QualitySettings::temporalHistoryCapacity`, a live-ladder lever).
- **The effects' spans** (`temporal/slit/span` and so on) are floats in frames, ≤ capacity, and free to route every
  frame. A fractional age blends the two nearest layers.
- **`temporal/history/stride`** (1..8): capture every s-th frame, anchored to the absolute frame index, so it stays a
  pure function. 128 layers at stride 2 is 8.5 s at 30 fps.
- **Per-layer metadata**: a small storage buffer holding each layer's frame index, render time and double-precision
  camera basis. The sampler uses the frame index for stride-correct ages. §11's future reprojection and depth-aware
  work needs the camera.
- **The bound stays declared.** `historyFrames()` returns capacity × stride, the conformance test still asks every
  kind, and the badge counts it.

### 1.4 The strip ring (slit photography, Phase 3)

The classic moving-slit image needs only one line of each frame: the slit, at a position and angle that may move.
Each line is written into column `frame mod N` of a 2D texture. The sampler lays the columns out by age across the
frame. At full resolution RGBA16F that is 16.6 MB for 1,920 frames (64 s); a time map would need about 16 GB for
that span. It is the right store for the long, iconic streaked-landscape look, and it is still an FIR with K = N.

### 1.5 Feedback, accumulate mode (subject to §6)

A ping-pong pair at history resolution (2 × 4.1 MB at half resolution):

```
F_n = C'_n + d · T(F_{n-1})
```

- `C'` is the sampler's output.
- `T` is zoom, turn and drift, plus the fracture tear.
- The decay `d` is clamped to ≤ 0.98.
- A soft knee caps the maximum accumulation, NaN and Inf are scrubbed to 0, and the buffer resets on a discontinuity.

Because `d < 1` and `T` does not amplify, the loop is a **contraction**. After `K = ceil(ln ε / ln d)` frames it agrees
with any other start to within ε. At ε = 1/1024 (below an 8-bit step after the tone map) and d = 0.95, K = 136.

So a render that pre-rolls K frames reproduces a play-through to within ε. ADR-410's guarantee survives in a measured,
declared form instead of an exact one.

### 1.6 Determinism, seek and offline

- **Play:** exact.
- **Offline render:** build the render-job pre-roll that ADR-410 specified and nobody built. It renders the declared K
  frames before `start` on the real renderer, so `--range 60:90` is exact. This is generic, and fixes an existing gap
  for every temporal scene.
- **Interactive seek:** a 128-frame ring cannot warm up on settle (about 4 s of scene renders). The existing
  "Temporal: settling, n of K" badge declares it, and the picture converges as playback runs. It is not hidden.

### 1.7 Debug and inspection (brief §7)

- **`temporal/debug/solo`:** 0 normal, 1 original, 2 slit only, 3 displacement only, 4 fracture only, 5 feedback
  only, 6 combined. It masks the terms inside `TemporalEffects`, so a scene's own settings are untouched, and each
  per-effect audition render differs from the full one by a single project parameter.
- **The temporal section of the World Effects panel** shows the write index, capacity, stride, spans and offsets,
  live intensities, bytes, history resolution and `temporal/*` GPU times. All of these are values the renderer
  already holds; there is no GPU readback.
- **The existing ring debug view** (`--debug-target`, the history tiles) is kept.

### 1.8 Integration points (all existing)

| Point | Change |
|---|---|
| `SceneRenderer::render`, temporal stage | Unchanged position. The capture reads the HDR *before* the debug-geometry pass (the one leak, `02-recon.md` §1). |
| `TemporalHistory` | Capacity, stride, metadata buffer, optional depth channel; strip ring beside it. |
| `TemporalEffects::run` | `fs_slit` becomes `fs_sample`; feedback mode; solo masking; timers `temporal/{capture, strip, sample, feedback}`. |
| `TemporalSettings` and parameters | New groups `temporal/history`, `slit` (extended), `displace`, `fracture`, `feedback` (mode), `debug`. JSON round-trip; conformance tables. |
| `QualitySettings` and the live ladder | `temporalHistoryCapacity`, alongside `temporalHistoryScale`. |
| `RenderJob` | Temporal pre-roll after the renderer is constructed. |
| World Effects panel | Rows and readouts. `temporal/` is already a beginner prefix. |

## 2. The CHRONOS environment (Phase 2)

- **Terrain.** Extract Strange Attractor's RTIN mesher into `src/scene/terrain_rtin.{hpp,cpp}`: a height grid and a
  tolerance field in, crack-free chunks out. Its watertightness test comes with it. `attractor_world.cpp` calls it,
  with no behaviour change, proved by a mesh-hash test.
- **The generator.** A new composition block (`chronosCanyon`) generates a distinct landscape on a deterministic
  seed:
  - a broad, **winding** river canyon (meanders, not Strange Attractor's straight trunk);
  - stepped sandstone with alcoves and fins;
  - foreground hoodoos;
  - distant ranges, so no edge is ever visible.
- **The river** follows the generator's own centreline, and the water sheet is cut from the same field, so it cannot
  misalign.
- **Hero formations** a height field cannot make are separate meshes, instanced and shared, from a committed
  generator script (Blender headless where it helps). Any free scans are logged in `assets.md` and kept out of git:
  - an arch;
  - overhangs;
  - the "suspended geological formations" (brief §2.1): a few slow-drifting monoliths. Not random floating shapes,
    and not a vortex.
- **Look.**
  - Strata in a material program (Strange Attractor's approach), restricted to the brief's palette: indigo and violet
    shadow, amber and red rock, magenta and gold air, cool water highlights.
  - A low raking sun and the volumetric atmosphere.
  - A sky whose light evolves across the acts.
- **Camera.** An authored spline (mode 2, `splineBank`), a pure function of time, with no per-frame clamps. The rule
  from ADR-1854: jitter is measured with the feature tracker before any effect goes on, because slit scan turns
  0.5 px of jitter into visible tearing.
- **Budgets.**

  | Item | Budget |
  |---|---|
  | Terrain | ≤ 3.0M triangles, ≤ 100 MiB |
  | Hero formations | ≤ 0.5M triangles, instanced |
  | Draw calls | ≤ 600 |
  | Generation | ≤ 6 s |

## 3. Budgets (1080p, M2 Max)

### Memory, live tier (half-resolution ring)

| Item | MB |
|---|---|
| Colour ring, 128 layers × 2.07 MB | 265 |
| Strip ring, 1,920 columns | 16.6 |
| Feedback pair (half resolution) | 8.3 |
| Metadata | < 0.1 |
| **Temporal total** | **≈ 290** |

The sampler's output is the pool's existing 16.6 MB target. The offline tier (full-resolution ring) is 1.06 GB at 128
layers, or capacity 64 at 531 MB.

### GPU time targets

| Pass | Target |
|---|---|
| Capture | ≤ 0.10 ms |
| Strip capture | ≤ 0.05 ms |
| Sampler, worst case (six fetches) | ≤ 0.60 ms |
| Feedback | ≤ 0.30 ms |
| **Temporal total** | **≤ 1.2 ms** |

The scene gets ≤ 25 ms, which keeps the frame p99 under 33 ms with margin. Each pass is timed separately and in
combination (brief §6.9), under the lock, repeated.

## 4. Phase plan against the gates

### Gate 1: Phases 1 and 2

**Phase 1 code:**

- ADR-1930: ring capacity, stride and metadata (amends ADR-410's 32 cap).
- ADR-1931: the sampler with vertical and horizontal slit, span, strips (quantised width), blend, scan position and
  velocity.
- ADR-1932: the render-job temporal pre-roll.
- The debug-geometry capture fix.

**Phase 1 tests:**

- wraparound and stride indexing;
- reset and invalid history;
- parameter bounds;
- a capacity change versus a span change (a span change must not reallocate: the texture identity is kept);
- resolution change;
- byte-identical when off;
- the FIR seek test at 128 frames;
- the pre-roll reproduces a play-through.

**Phase 1 media:** Test A (a stationary camera with a mover) and Test C (stationary geometry, effect off and at
amount 0 under a moving camera).

**Phase 2 code:**

- ADR-1933: the extracted mesher.
- ADR-1934: the `chronosCanyon` block, river, light, sky and spline camera.

**Phase 2 tests:** deterministic generation (hash), watertightness, river alignment.

**Phase 2 media:** a clean flythrough with **no effects**, stills per act, and a jitter measurement.

STOP.

### Gate 2: Phases 3 and 4

**Phase 3 code:**

- the directional slit and the strip ring with a moving slit (ADR-1935);
- displacement (ADR-1936);
- fracture with bands and warped cells, evolving on smooth noise, the epoch changing only on events (ADR-1937);
- feedback accumulate (ADR-1938, if approved);
- the solo modes and readouts (ADR-1939);
- timers.

**Phase 3 tests:** feedback decay and bounds (no NaN, Inf or runaway after 10,000 frames of maximum settings), solo
masking, combined-without-conflict.

**Phase 4:** the project's routes:

| Source | Target |
|---|---|
| drums stem onset | fracture |
| bass stem level | span |
| synth stem level | displacement |
| highs | strip count |
| `music.impact` / thresholded onset | feedback rupture, with a recovery envelope |
| `response.level` | overall mix |

All of these are bounded and smoothed.

**Gate 2 media:**

- per-effect audition clips, made with the solo modes;
- Test B (moving camera);
- Test D (transients) and Test E (sustained), each with sound;
- Test F (feedback stability, 3 minutes at maximum).

STOP.

### Gate 3: Phase 5

- The five acts on the project timeline, mapped to the song (§5).
- Tuning of colour, camera and composition.
- The 1080p30 profile with `--live-frame` and per-pass timers.
- Live-ladder levers for capacity and scale (ADR-1940).
- Flicker measured on video.
- A full render with sound.
- Both suites at exit 0.
- A rebased, fast-forwardable head.

STOP, ready for merge.

## 5. Music

**Recommendation: Feline Footwear** (210.5 s, 121 BPM). It is the only candidate with stems (Drums, Bass and Synth),
which is exactly what brief §4.1 asks for in place of a stereo-mix classifier. Its sections fall onto the five acts
almost one for one:

| Act | Song | Time |
|---|---|---|
| I The Present | intro and bass pedal (drums alone, then the F pedal) | 0-20.1 s |
| II Temporal Awakening | the falling synth calls and the build | 20.1-47.9 s |
| III Temporal Displacement | the drop, groove A, the tresillo bass | 47.9-101.4 s |
| IV Temporal Collapse | the fills, groove B (16th hats: granularity), the lift; the rupture at the song's peak, 2:39 | 101.4-166.9 s |
| V Reconstruction | the pedal, the drums stop, the synth alone fading | 166.9-210.5 s |

Trench (207.6 s) has no stems, so it would run on `response.*` alone. It is darker and might suit the indigo palette,
but it loses the separation between the drums, the bass and the synth that the mapping depends on.

## 6. Decisions for the owner

1. **Real feedback.** Recommended: an accumulate mode as a convergent, bounded IIR (§1.5). This amends ADR-410: renders
   reproduce to within 1/1024 after a declared pre-roll, rather than exactly. The alternative keeps feedback FIR-only:
   exact, but each frame of tail is a texture fetch per pixel, so the tail is short and expensive, and it gives weaker
   recursion.
2. **The history budget.** Recommended: capacity 128 at half resolution live (≈ 265 MB, 4.3 s at stride 1 or 8.5 s at
   stride 2), and full resolution offline (1.06 GB). Smaller is possible; the slit scan's structure needs at least about
   90 frames.
3. **Music.** Feline Footwear, the full 3:30 with its stems (recommended); Trench; or a shorter edit of Feline (for
   example, the first 1:51, Acts I-IV compressed).
4. **The mesher extraction against Strange Attractor Art Pass 2.** Both touch `attractor_world.cpp`. Recommended:
   CHRONOS lands the extraction as one small, behaviour-neutral commit early in Gate 1, and Art Pass 2 rebases onto it
   (a mechanical conflict). The alternative waits for Art Pass 2 to merge first, which blocks Phase 2.
5. **Live or offline first.** The brief targets 1080p30 live. Recommended: build and judge it as an offline render
   first (art before optimisation), measure live with `--live-frame` in Gate 3, and keep it out of the index and live
   switcher until you approve it.

## 7. The Gate 0 proof clips

Both clips are throwaway and silent. Both use Strange Attractor's canyon flythrough from main (the first 20 s, before
its glyphs and portal), because CHRONOS has no canyon yet. Media: `~/Desktop/av-gen-review/104-chronos/gate0/`.

- **`chronos-g0-proof-A-existing-engine-slit.mp4`.** The engine's own slit scan, as it ships today, rendered by
  `avgen`: 32 frames, amount 1. Columns first, then rows; the clean frame is on the left and the effect on the right.
  - **What it shows:** with a 32-frame cap, a full-width scan has about 60 px per frame step, cross-faded. On a moving
    camera it reads as a lean and a soft smear, not as temporal structure. That is the case for the longer ring.
- **`chronos-g0-proof-B-long-ring-preview.mp4`.** An offline numpy preview, run over the clean frames, of what Phase 1
  and Phase 3 will build:
  - a 150-frame vertical slit, continuous and then quantised into 24 strips;
  - a 90-frame horizontal slit;
  - a 120-frame directional (diagonal) slit;
  - an 8 s strip photograph with a moving slit.

  **What it shows:**
  - The long span is what makes canyon walls fold, shear and step into readable temporal shapes.
  - The quantised strips read as deliberate structure rather than smear.
  - The strip photograph is a different, calmer look.

  **The caveats:**
  - It works on tone-mapped 8-bit PNGs, where the engine works on HDR radiance.
  - It uses nearest-frame strips with no blending.
  - It is a look preview only. It is not the engine, and it was not timed.
