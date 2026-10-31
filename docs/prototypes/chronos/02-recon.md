# CHRONOS Gate 0: reconnaissance

Branch `scene/chronos` from main 214dafd8. This is the brief's Phase 0 (§9): what the renderer already has, where
the temporal system plugs in, what it lacks against §3, and the risks. The plan is in `03-plan.md`.

**The headline.** AV Gen already has most of the brief's pipeline. A GPU frame-history ring, a capture of clean HDR
radiance before the post chain, a slit scan, an unrolled feedback, a data mosh and an echo all exist, with GPU timers
and tests (ADR-410, ADR-1049, ADR-1066). CHRONOS does not need a new subsystem. It needs that one generalised:

- a longer ring whose size does not move when audio modulates it;
- a strip history for true slit photography;
- a single composed sampling pass;
- a decision about real feedback.

## 1. The renderer, in frame order

`SceneRenderer::render` (`src/rendering/scene_renderer.cpp`) encodes one command buffer per frame. Pass ordering is
explicit code, not a declared graph. Scratch textures come from `gpu::TransientPool` (ADR-016: acquire by size,
format and usage; reused within and across frames). Long-lived targets are members.

The frame, as encoded:

1. Shadows and the depth prepass, then the lit scene pass into `hdr_` (RGBA16Float). The velocity, emission, normal
   and id targets are written alongside.
2. The Environment seam (ADR-1200): specialised renderers draw into the lit HDR.
3. The volumetric atmosphere (ADR-032).
4. **The debug drawing pass (ADR-031)**, into `hdr_`.
5. The screen-space distortion (Effect Library Wave 1).
6. Post shader layers (ISF `LayerStage::Post`), ping-ponging HDR.
7. **The temporal stage**: `TemporalEffects::run` (around line 5520).
8. The built-in post chain: DoF, motion blur, bloom, grading, FXAA.
9. The tone map into the target, then aux debug views, then ImGui (on the swapchain, outside the renderer).

**The HDR capture point is step 7.** It already captures scene-linear radiance before bloom, grade and tone map, and
after fog, distortion and post layers. That is what brief §3.2 asks for. The UI can never reach the ring: ImGui
draws on the swapchain after the renderer.

One leak exists. The debug geometry (step 4) draws into `hdr_` before the capture, so bounds, light and path
overlays enabled with `--debug-draw` would be written into history. It is opt-in and off in renders, but the brief
names it (§3.2), so it is fixed in Phase 1: the capture reads the HDR before that pass, or the pass moves after the
temporal stage.

The temporal stage runs only when `toggles_.post` is on, so `--disable post` also removes it. That is acceptable,
and it is documented.

## 2. The existing temporal family (what exists)

### Files

| File | What it holds |
|---|---|
| `src/rendering/temporal_history.{hpp,cpp}` | The ring. One `texture_2d_array` per channel (Colour; Motion is optional), per-layer render views, and the frame bookkeeping copied from `AoRenderer` (a repeat restores; a jump drops). `bytes()` is measured, not estimated. |
| `src/rendering/temporal_effects.{hpp,cpp}` | The passes: echo, feedback, slit, mosh, capture, in that order. Each is a full-screen draw into a pooled full-resolution RGBA16Float. |
| `shaders/temporal.wgsl` | The capture (a 4-tap box downsample into RG11B10Ufloat), `fs_echo`, `fs_feedback`, `fs_slit`, `fs_mosh`, and `fs_debug_history` (the ring tiled newest to oldest, invalid layers tinted red). |
| `src/scene/temporal_settings.{hpp,cpp}` | Settings and the `temporal/<effect>/<leaf>` parameters. `historyFrames()` is each effect's declared bound. JSON round-trip. |
| `tests/rendering/test_temporal_gpu.cpp` (12 cases) | Covers the measured ring bytes, zero bytes when off, the FIR guarantee, and per-effect pixels. Also `tests/unit/test_temporal_conformance.cpp` (10), `tests/app/test_temporal_engine.cpp`. |

### What is already right, and is kept

- **The capture is clean.** Every effect reads the ring, which holds unprocessed frames, so no effect reads its own
  output. Each is an FIR filter of K frames, and K frames of play rebuild it exactly (ADR-410). That is the reason
  the family is scrub-safe and render-reproducible.
- **The ring is cheap by design.** Colour is RG11B10Ufloat at `QualitySettings::temporalHistoryScale`: 0.25 Preview,
  0.5 Realtime and High, 1.0 Offline. At 0.5 that is 2.07 MB a layer at 1080p.
- **Off is free.** No temporal effect means no allocation and no pass. With every effect at amount 0 the frame is
  byte-identical (ADR-368).
- **Timers exist.** Each pass marks `temporal/<name>` on the frame timeline (ADR-1066), and `TemporalStats` carries
  the frames valid and needed, the bytes, the size and `temporalMs`. The debug view of the ring exists.
- **Measured** (ADR-1066, 1080p, M2 Max): slit 0.066 ms; feedback 0.66 ms at 8 taps.

### The existing slit scan, precisely

`fs_slit` is a **time-displacement map**. Each pixel shows the frame `d(uv) × K` back, blended linearly between the two
nearest layers. `d` is chosen by mode: rows, columns, radial distance, or luminance; `reverse` flips it.

With K capped at 32, a full-width column scan has 32 ages over 1,920 px. That is 60 px per frame step, cross-faded,
which reads as a gentle smear rather than temporal structure. The proof clip shows this (§8).

## 3. What is missing against brief §3

| Brief item | State | Gap |
|---|---|---|
| §3.2 ring, GPU-resident, no per-frame allocation | exists | — |
| §3.2 configurable duration | **partial** | Capped at 32 frames (about 1 s). Slit scan needs about 4-8 s. |
| §3.2 duration modulated by audio | **broken by design** | The ring's depth is `historyFrames()`, the max of the live effects' `frames`. A route on `frames` therefore *reallocates the texture array* whenever the value crosses an integer, and every reallocation drops the history. Capacity must be decoupled from the read span. |
| §3.2 configurable sampling resolution | exists (tier) | Per-scene override missing. |
| §3.2 sampling interval | missing | Every frame is captured. A capture stride (every Nth frame, anchored to the frame index so it stays deterministic) is needed to stretch the span. |
| §3.2 frame metadata | missing | No per-layer record of the capture time or camera. Needed for a stride, and for §11's future depth-aware work. |
| §3.2 depth history | missing | There is only an unused Motion channel. A small linear-depth channel is optional for Phase 3. |
| §3.2 reset, invalid history | exists | Taps clamp to `framesValid`, so the ring never reads a pre-seek layer. |
| §3.3 vertical, horizontal slit | exists, weak | It needs a long span and crisp strips (quantised ages) to read as structure. |
| §3.3 directional slit | missing | Only axis, radial and luminance modes exist. It needs an arbitrary angle. |
| §3.3 moving slit, scan position and velocity | missing | True slit photography (a narrow strip from each frame, laid out side by side) is not a time map at all. It needs a different store: a **strip ring**. |
| §3.3 slit width, temporal blending | missing | Quantisation width and the nearest-versus-linear blend are not exposed. |
| §3.4 RGB temporal offsets | missing | `fs_mosh`'s `shift` is spatial chromatic separation, not temporal. |
| §3.5 temporal fracture | partial | The mosh is hashed square blocks on an epoch: a codec look. Bands, irregular regions, organic evolution and a coherent-to-fractured transition are missing. |
| §3.6 feedback, ping-pong | **conflicts with ADR-410** | Feedback exists, but as an unrolled FIR over the clean ring: K taps per pixel (0.66 ms at 8), and at most 32 frames of tail. A real ping-pong accumulator is an IIR filter, which ADR-410 forbids. This is an owner decision (`03-plan.md` §6). |
| §6 per-effect timings, memory | exists | — |
| §7 debug comparison modes | partial | Per-effect enables exist. A one-switch A/B mode and on-screen readouts (buffer index, offsets) are missing. |
| Offline render from mid-song | **gap from ADR-410** | ADR-410 says the render job must pre-roll K frames before `start`. That was never built (`render_job.cpp` has only the pipeline warm-up). A `--range 60:90` render starts with a cold ring. Harmless for full-song renders from 0; wrong for clips. |

## 4. Resource management and synchronisation

- **The ring** reallocates only when its size, format or depth changes (`ensureRing`), so calling `configure` every
  frame is free. That is correct for a fixed depth, and is what modulation breaks (§3).
- **Each effect pass acquires a full-resolution RGBA16Float from the pool** (16.6 MB at 1080p, reused). Four chained
  effects mean four full-screen passes and four reads and writes of 16.6 MB. Brief §6.6 asks to avoid redundant
  full-screen passes. The plan composes the sampling effects into one pass.
- **Uniforms.** One `WriteBuffer` per buffer per submit is what lands, so every pass has its own uniform buffer. A
  comment in the code records the defect that taught this. New passes must follow it, or use dynamic offsets.
- **Bind groups** are created every frame for every pass. That is cheap at this count, but it is one more reason to
  merge passes.
- **Synchronisation.** WebGPU orders passes within a command encoder, and the ring is written (capture) after it is
  read (effects) in the same encoder. No explicit barrier work exists or is needed. The capture is last, so the
  effects read `writeLayer - k` for k ≥ 1 and never the layer being written.
- **Limits.** The device requests the adapter's full limits (`gpu/context.cpp`). Metal's array-layer ceiling on Apple
  silicon is 2048, so a 256-layer ring should be legal; Phase 1 asserts the device's `maxTextureArrayLayers` rather
  than trusting this. The 32 cap is a policy (ADR-410's warm-up bound), not a hardware limit.

## 5. Audio and modulation

There is nothing new to build. Everything the brief's §4 table needs is on the signal bus, and routes have envelope,
threshold, remap, clamp and attack/decay stages (`docs/help/audio-signals.md`, `modulation-routes.md`).

| Brief signal | What to use (no kick/snare/hat classifier exists) |
|---|---|
| Kick / transient | `stem.drums.onset`, or `role.drums.onset`; `response.low` / `response.onsetLow` from the mix |
| Bass | `stem.bass.level`, `role.bass.*`; `response.bass`, `audio.bassLevel` |
| Synth | `stem.synth.level`, `.centroid` |
| Hi-hat / highs | `stem.drums.onsetHigh`, `response.high`, `audio.trebleLevel` |
| Crash / major transient | `music.impact`, `music.drop`, `beat.phrasePulse`, a thresholded `response.onset` |
| Overall amplitude | `response.level`, `audio.energy` |

- **Progression** is the project `timeline` (keyed tracks on any parameter; routes add on top) plus `section.*`
  signals.
- **Integrating routes can be bounded** (ADR-1161). That is the tool for a scan position that moves at a velocity.
- **MIDI** maps onto any parameter (ADR-1700..1705).
- **The constraint routes impose on the engine:** a modulated parameter must be safe to change every frame. Today
  `temporal/*/frames` is not (§3).

**Music.** Feline Footwear (3:30.5, 121 BPM, F minor) has Drums, Bass and Synth stems in `assets/audio/stems/`, and
a bar-accurate structure map (`docs/prototypes/rainbow-world/30-feline-structure-and-plan.md`). Trench (3:27.6) has
no stems. See `03-plan.md` §5.

## 6. Strange Attractor's terrain: lessons and reuse

The canyon is `src/scene/attractor_world.{hpp,cpp}` (1,718 lines), a composition block `attractorWorld`.

**What it does:**

- Bakes a height field (layout, erosion-front field, metric cliffs, terraces, gorge, gullies, masks).
- Triangulates the rendered surface as one **RTIN** (right-triangulated irregular network). The tolerance grows with
  distance from the flight path. It uses level-order error propagation, so it is crack-free (ADR-1857, ADR-1867):
  2.92M triangles, about 80 MiB, a 5 s bake, 256 culling chunks.
- Draws the chunks as ordinary lit entities with a material program (strata in the program, ADR-1859/1860).
- Draws the river as a flat water sheet through the engine's water pipeline (ADR-1868..1871).
- Uses a shadow-casting sun and a precomputed, smoothed flight.

**The lessons that matter to CHRONOS:**

1. **Engine triangles, not a ray march.** The march path drew moiré, dashes and streaks that the mesh does not.
2. **Crack-free by construction.** Depth-first RTIN error propagation left 269k open edges; level order leaves 0.
   There is a test that every interior edge is shared by exactly two triangles. CHRONOS reuses the mesher and that
   test.
3. **Never rebuild rays or depth through float `invViewProj` with a huge far plane** (ADR-1807, 1855, 1866, 1870).
   This caused 8 px whole-frame jitter. Use the double-precision camera basis. **This matters doubly here:**
   slit scan turns every frame of camera jitter into a visible tear between strips.
4. **Camera smoothness is a scene problem.** Per-frame clearance clamps after smoothing gave 19 px peaks (ADR-1854).
   CHRONOS uses an authored spline (camera mode 2, `splineBank`, ADR-1166): a pure function of time, with no clamps.
5. **GTAO drew fins on long wall triangles.** It was off for that scene.
6. **The cost to watch:** `Scene::bounds()` re-validates every mesh index every frame (5 ms CPU at 8.27M triangles).
7. **The sun:** a grazing sun stair-steps crest silhouettes on a 2 m grid. 1 m sampling fixed it.

**What is reusable versus scene-specific:**

- **Reusable:** the RTIN mesher (grid plus tolerance in, chunks out). It is entangled with `AttractorWorld`'s masks
  and uv packing today, but the core can be extracted.
- **Reusable:** the smoothing helpers, and the material-program strata approach.
- **Scene-specific:** the layout, the masses, the trunk, the flight builder and the portals (ADR-1872..1884, which
  CHRONOS does not need).

**The conflict risk.** Strange Attractor Art Pass 2 has just started in `../av-gen-attractor` and will edit
`attractor_world.cpp`. Extracting the mesher touches the same file, so the merge order needs agreeing (`03-plan.md`
§6).

## 7. Other reuse

- **Strange Attractor's world-space corruption** (ADR-1873) bends geometry in the vertex shader. It is a world-space
  cousin of fracture and a §11 candidate, but out of scope for the POC.
- **Digital Mosh** (ADR-1160..1166) supplies the bounded integrating route, timed state triggers, and the banking
  spline camera.
- **The live ladder** (`rendering/quality_policy.hpp`, `render_quality.hpp`). `temporalHistoryScale` is already a
  tier value; the ring scale and capacity become ladder levers in Gate 3.
- **The conformance test** asks every temporal kind for its bound and fails by name. New kinds must join its tables
  (the `-Wswitch` guard plus the test).

## 8. The proof clips (Gate 0)

See `03-plan.md` §7 for what each clip shows. The short version:

- The existing slit (32 frames) on a moving camera is a soft gradient smear. It is not yet the look.
- An offline preview of a 90-150 frame ring with quantised strips, a directional map and a moving-slit strip
  photograph does produce legible temporal structure. That preview is what Phase 1 builds.

## 9. Risks

| Risk | Mitigation |
|---|---|
| **Ring memory.** 128 layers at half resolution is 265 MB; at full resolution, 1.06 GB. | Half-resolution default, a capacity cap per tier, the strip ring for long spans (8 MB for 64 s), `bytes()` on screen. |
| **Scrub and seek with a long ring.** A 256-frame history cannot warm up on seek settle (about 8 s of scene renders). | Declare it: the existing settling badge counts up. Build the missing render-job pre-roll, so offline ranges are exact. |
| **Real feedback is an IIR.** | Owner decision (`03-plan.md` §6). The recommendation keeps renders reproducible. |
| **Camera jitter becomes visible tearing** under slit scan. | The double-precision basis and an authored spline. Measure jitter with the feature tracker before any effect goes on. |
| **Stationary geometry smearing when an effect is "off"** (Test C). | Off means not encoded (byte-identical, existing ADR-368 test); modulated to 0 means bypassed. A GPU test at amount 0 under a moving camera. |
| **Flicker** (owner sensitivity) from per-frame random fracture epochs. | Fracture regions evolve on smooth noise, and hash epochs change only on events. Measure frame-difference on video. |
| **Merge conflict with Strange Attractor Art Pass 2** over `attractor_world.cpp`. | Extract the mesher in one small, mechanical commit, early, and tell the coordinator. |
| **The full-song camera path is 3:30 long** and the canyon must fill every frame for that time. | The terrain extent and tolerance budget follow the path. Act lengths are an owner choice. |
| **Shared GPU.** Perf numbers are noisy while Art Pass 2 renders. | Every run goes under `tools/gpu-lock.sh`; repeat measurements; trust only CONDITIONS SAME. |
