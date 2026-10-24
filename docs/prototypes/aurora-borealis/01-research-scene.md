# Aurora Borealis 01: scene architecture and visual rendering (Research Agent A)

- **Scope:** the brief's Subagent A (`00-brief.md`): Glass Caldera dissected, the engine features to reuse, measured
  costs of the candidate building blocks, and a proposed scene architecture. Research only: no production code changed.
- **Branch:** `scene/aurora-borealis` at `d6f4f07a` (main `544254d1` plus the brief). Release build of that commit in
  `build/release` of the worktree.
- **Media:** `~/Desktop/av-gen-review/aurora-borealis/research-a/` (the stills named in §3).
- **Probe projects:** in the session scratchpad (`probe/make_variants.py`, `make_sky.py`, `make_water.py`, `run.sh`,
  `summarize.py`). They are not committed: each one is Glass Caldera's project with one thing changed, and they link
  its meshes. §3.1 says how to rebuild them.

## 0. Summary for the lead

**What the measurements say:**

1. **The aurora ribbons are the scene's one large cost.** At 1080p native with about half the frame sky, ribbons add
   about **4.4 ms (16 planes) to 6.6 ms (48 planes)** to the scene pass.
   - The plane count is cheap, about 0.07 ms a plane. Most of the cost is per sky pixel, whatever the plane count.
   - **16 planes are grainy at native resolution** (still 03); 48 planes are clean (still 04).
   - Do not buy speed with planes. Use 40-48 planes on the upper tiers, and let the render scale carry the ladder.
2. **The faceted land is cheap.** Glass Caldera's whole landscape is about 11.5k triangles plus 11.5k twins, drawn
   with three unlit programs of about 30 ops. Its GPU frame is **8.1 ms** at 1080p Ultra, and turning its programs off
   changes nothing measurable.
3. **Neither existing reflection path shows the aurora in the water.** This is the scene's one real engine gap.
   - Cinematic water's SSR marches against linear depth, and the sky's depth is 1e7, so a ray that should see the sky
     never hits. It falls back to the environment cube, which has no aurora in it (still 05).
   - Glass Caldera's mirror twins reflect land only. Its sky mirror is a dome program, which hides the atmosphere
     layer entirely (still 06).
4. **The engine water is cheap; the default terrain under it is not.**
   - A 3.2 km terrain at chunk 100, resolution 24, with ground mottle costs **+6.7 ms** dry.
   - A coarse terrain (chunk 200, resolution 12, no mottle) costs **+1.5 ms** dry, and **+2.7 ms** with cinematic
     water and SSR (16 steps, 1 ray).
   - Most of the cost Living Prism's aurora valley paid for "water" was ground.
5. **Keep the depth prepass.** Turning off AO also removed the prepass, and the frame got slower by **0.6-2.9 ms**:
   the layered ranges and twins overdraw. Set `depthPrepass: true`, `ambientOcclusion: false` (the scene is unlit), and
   save only the AO's 0.6-0.9 ms.
6. **Live, the proxy scene holds 1080p30 at Ultra 100% of frames.** The proxy is 24 ribbon planes over half the
   frame, Caldera's faceted land with twins, and stars, at a fixed camera with no routes.
   - 3-4 misses per 900 frames, frame p99 38.5-38.6 ms.
   - GPU p50 15.5-15.9 ms, CPU p50 6.4 ms.
   - The arm with terrain water held Ultra too: 1 miss, p99 39.1-39.8 ms, GPU p50 17.7 ms.

**What I recommend:**

- **Architecture:** Glass Caldera's construction pipeline (generated faceted GLB land, unlit painter's programs, no
  lights, no shadows) around a winding river valley, with these on top:
  - the ADR-1641 ribbon aurora as the hero;
  - a real river surface: a coarse terrain node carrying only the river and cinematic water (SSR 16x1, flow streaks);
  - one small engine fix (E1 below): on an SSR miss, the water reads the already-rendered sky and aurora from the
    frame copy at the reflected direction's vanishing point. The aurora is then in the river for about one texture
    tap per water pixel, and the reflection agrees with the sky by construction (it is the same pixels).
- **Budget:** about 20 ms GPU at Ultra, against the controller's 29.3 ms (§5.2).
- **Engine changes**, all small and gated:
  - E1: the aurora seen in the water;
  - E2: a ladder lever for the ribbon planes;
  - E3 (optional): a travelling pulse along the ribbons for the kick;
  - E4 (no code): do not route the aurora's speeds. They jump, because the flow is elapsed time times speed.
- **Fallback for the river,** if E1 or the terrain fit disappoints: Caldera's twins plus a mirror flag on the sky and
  atmosphere layers. It costs a second aurora march over the water pixels (estimated 2.5-3.5 ms, not measured).

## 1. Glass Caldera: what makes it read and how it is built

**Files read:**
- `examples/sonic-abstract/digital-alpine.json` (project: 97 routes, post, the camera timeline);
- `examples/sonic-abstract/digital-alpine.scene.json` (41 nodes, 3 material programs);
- `tools/sonic_vfx/scenes/digital_alpine.py` (the generator), `tools/sonic_vfx/meshes.py` (the faceted geometry);
- `docs/prototypes/sonic-garden/ABSTRACT-PLAN.md` §6, `PROGRESS-abstract.md`;
- `examples/sonic-abstract/meshes/da-*.glb`.

It is one of the Sonic Abstract set, not a section of a larger project. It is registered in `examples/index.json` as
"Sonic Abstract - Digital Alpine" with `live: true`.

### 1.1 Why it reads

| Element | How it is done | What it buys |
|---|---|---|
| **Facets** | `meshes.py`:<br>- `polar_range` (a jittered polar grid, quads split on random diagonals);<br>- `faceted()` (unshared vertices, one normal per triangle);<br>- `clip_above(y0=0)` at the waterline;<br>- `mirrored()` (the twin: y negated, winding reversed);<br>- `write_faceted` to GLB. | Exact flat facets that no procedural primitive can draw. Four rings (shore, main, far, distant), each farther, higher and paler, plus an island and rocks. About 11.5k triangles in all, plus the twins. |
| **Palette** | Painter's colours by facet normal, in an unlit program (`daRock`, 34 ops):<br>- warm where a facet faces the low sun;<br>- lilac where it faces the sky;<br>- indigo in shadow;<br>- the sky's own colour with distance (aerial haze in the program, not in a fog pass).<br>Zenith `#0e1840`, violet `#4d4386`, horizon `#f4c0a0` and `#a985b4`, shadow `#160f38`, sky-lit `#2f2a6e`, sun-lit `#e0785e`. | Depth by value and hue steps per range, with no lights, shadows or volume. Distances are in decametres, because material-op constants clamp to ±1000. |
| **"Glass" water** | It is not a water surface:<br>- every landform has a twin below y = 0, drawn darker and bluer by the same program;<br>- a dome program (`daSky`, 28 ops) paints the sky above the horizon and its mirror below it;<br>- a 30 km disc (`lakeSky`, `daWater`, 32 ops) draws ripples and the sun's broken path. | An exact mirror of mountains and sky, for one extra draw of cheap geometry. |
| **Post** | Bloom 0.25 (threshold 1.6), AgX-type tonemap (operator 4) with chroma retention 0.6, vignette 0.3, grain 0.012. | Painterly finish at about 0.4 ms. |
| **Camera** | Timeline tracks:<br>- `camera/position` and `camera/target`, 64 keys, a 170 s loop;<br>- `camera/roll`, banking into the turn.<br>A low glide (9 m) round the lake, radius 320 m. The view swings from ahead to across the water twice a circuit. | A seamless loop, deterministic (seek equals play). The light turns from backlit dusk to front-lit alpenglow round the circuit. |
| **Music** | A travelling sine field (`swell`) through every landform's field deformer; band-lit ranges; a kick flare in `daRock`; splash-ring particles; pitch-class lanterns. | Not needed for Aurora Borealis. Its signals are the mix's (`response.*`, `audio.*`), not stems. |

### 1.2 Lessons to keep and to avoid

**Keep:**
- the generator plus `meshes.py` pipeline;
- unlit programs coloured by normal, with distance haze in the program;
- the unshadowed key;
- a timeline camera on a closed loop.

**Avoid:**
- **The `lakeSky` disc.** It is opaque and covers the whole sky, so an aurora effect added to Caldera is invisible. My
  first four aurora arms measured a hidden aurora because of it (§3.2).
- **The dome program for the sky.** It is opaque geometry, so it also hides the atmosphere layer. Use the procedural
  sky with `background: true`.
- **AO in an unlit scene.** Caldera pays 0.7-0.9 ms of AO that nothing reads. Keep the prepass, drop the AO (§3.4).
- **Mix-driven routes.** The new scene uses stem signals (Agent B).

## 2. Engine features to reuse: what exists and what it costs

**Files read:**
- ADRs 1600-1613 and 1640-1643 in `docs/decisions/`;
- `src/world/atmospherics.hpp/.cpp` (Aurora, AuroraShape, ground term), `src/world/effects/kinds/aurora_effect.cpp`
  (the parameters);
- `shaders/atmosphere_fx.wgsl` (`atmosAuroraRibbons`), `shaders/atmosphere.wgsl`, `shaders/water.wgsl` (`waterSsr`, the
  reflection fallback), `shaders/linear_depth.wgsl`, `shaders/pbr_shade.wgsl` (the sky's ground term);
- `src/rendering/scene_renderer.cpp`:
  - the skybox, atmosphere and water order;
  - the refraction copy, l. 4670-4760;
  - the depth prepass condition, l. 4148;
- `src/rendering/water_renderer.cpp`, `src/scene/water_surface.hpp`, `src/scene/composition.cpp` (water keys and
  parameters);
- `src/scene/material_program.cpp` (ops and inputs);
- `src/app/interactive_resolution.{hpp,cpp}` (rungs and ladders), `src/app/live_profile.cpp`;
- `tools/live_perf_baseline.py`, `tools/ci/live-perf.sh`, `tools/gpu-lock.sh`;
- Living Prism `01-rendering-audit.md` (§0, §1, §2.4-2.6, §4, §5), `03-integrated-report.md` §14, `10-`, `12-`,
  `13-`;
- from `origin/archive/living-prism-aurora`: `15-aurora-phase1.md`, `16-aurora-phase3.md`, `17-offline-pivot.md`;
- `examples/sonic-vfx/aurora-tundra.json`, `examples/labs/character/river-crossing.scene.json`,
  `examples/biovalley/valley.py`.

| Feature | Status | Cost (measured here unless cited) | Notes for this scene |
|---|---|---|---|
| **Aurora ribbons** (ADR-1641) | `shape/ribbons` on the aurora effect | **+4.4 / 5.1 / 5.4 / 6.6 ms** scene pass for 16 / 24 / 32 / 48 planes, about 50% sky (§3.2). ADR: +7-10 ms on a mostly-sky frame. | Only sky pixels pay: the atmosphere layer draws behind depth, and rays below the horizon return early. The footprint is in world space, so a moving camera gets parallax. Grain at native with few planes. There is no ladder lever. |
| Aurora curtains (ADR-230) | default shape | +1.0 ms scene pass, 3 shells (§3.2) | Columns round the horizon. The brief wants ribbons. |
| Aurora ground term (ADR-230 §6) | `ground/*` on the effect | ~0 | A flat hemispheric colour × intensity, applied in `pbr_shade` and `water.wgsl` **only**. Unlit programs do not receive it, so couple the facets in the program instead (§4.4). |
| Aurora's own audio | `audio/*`, master `audio/sensitivity` | ~0 | Reads the frame's analyser bands (the mix). Set the master to 0 and drive everything through stem routes. |
| **Terrain textures** (ADR-1642) | `textureTriplanar` ops | Present-code residual +0.6 ms at 4K; **+8.2 ms used** on a frame-filling 4K ground (ADR) | Not wanted: the art direction is flat facets. Skip. |
| **Cinematic water** (ADR-1286) + SSR + refraction copy (ADR-1603) | terrain `water` keys | **+1.2 ms** (cinematic water, SSR 16x1) on a coarse terrain (w08 − w07); SSR 48x4 +1.3 ms over plain on the fine terrain | The surface itself is cheap at these settings. **Reflects no aurora** (§3.3). Ladder lever `waterSsrScale` exists. |
| **Flow streaks** (ADR-1643) | `water/streaks/*` | **+0.06-0.1 ms** (ADR); +0.2 ms here (v10 − v08, within noise) | "Bands of colour on the water": three palette colours along the flow. Needs a flowing river. |
| **Terrain node** (needed by any engine water) | `kind: terrain` | **+6.7 ms** at chunk 100 / resolution 24 / mottle; **+1.5 ms** at chunk 200 / resolution 12 / no mottle (§3.3) | Use the coarse settings, and hide the ground under the faceted banks. |
| Refraction (ADR-1603) | procedural `refraction` | 1.2-2.5 ms with the depth lay-down; Living Prism heroes 2.8-4.2 ms | Not needed: no glass in this brief. |
| Dispersion (ADR-1640) | refraction option | +0.04 ms at 1080p over 40% of the frame (ADR) | Not needed. |
| **Faceted land** | generated GLB (`meshes.py`) | Caldera landscape and twins in an 8.1 ms frame; programs off: no change (8.45 vs 8.14) | The cheapest look in the engine for this style. |
| Material programs | compiled WGSL (ADR-1493) | ~3 ms + 0.09 ms/op for a frame-filling program (profiler's model); Living Prism ground at 41 ops ≈ 6 ms | Keep each program at or under 30 ops. Unlit on low-poly with a prepass measured ~free here. |
| Procedural sky | `environment.sky` | ~0 (v02 ≈ v00) | **Do not route sky colours continuously:** a change rebuilds the IBL chain (01 §2.6). Change them on phrase boundaries at most. |
| Stars (ADR-1350) | `stars` effect | small (inside s07) | Keep dim. |
| Fog / volume | `VolumeRenderer` | 7 ms at Ultra, ~1.1 ms at quarter resolution (01 §4.2) | **Do not use.** Do the haze in the programs, as Caldera does. |
| Bloom, grade, tonemap, FXAA | post chain | 0.35-0.8 ms all post (§3) | Reuse. Bloom threshold above the facets, so only the aurora blooms. |
| **Upscaler** (ADR-1607/1613) | `post/output/upscale-sharpness` | tonemap 0.19-0.32 ms vs 0.08 bilinear (ADR) | Below Ultra it is on by quality. Tune the sharpness against the ribbons' grain. |
| **Live ladder** | `kBalanced`: High 0.85, Medium 0.71, Low 0.5, Emergency 0.42 + `waterSsrScale`, refraction | — | No aurora lever (E2). The controller's GPU budget is 29.33 ms. Owner: Medium or better on ≥90% of frames. |
| Depth prepass | on when AO, water or `depthPrepass` | 1.0-1.5 ms (+ linear depth) | It **saves** 0.6-2.9 ms of overdraw here (§3.4). Keep it. |

**What was too expensive in Living Prism** (01 §4, 13, the archive's 15-17). None of it is needed here.

| Item | Cost |
|---|---|
| A frame-filling ground program, 41 ops | ~6 ms |
| Haze steps | 3.9-5 ms |
| Light ranges and wash lights | 4.3 ms |
| Refraction on several heroes | 3 ms |
| Scan shadows and depth | 1.7 ms |
| Many crystals at 30-38% of the frame | — |

Its aurora valley reached 30.5 ms of live GPU **before** the aurora, the river reflection and textures were added. That
is why it went offline.

## 3. Measurements

### 3.1 Conditions and method

| Item | Value |
|---|---|
| Machine and code | M2 Max, macOS 26.7.1, Release build of `d6f4f07a` (the worktree's `build/release`, configured and built for this) |
| Power and lock | AC power, checked before every run. Every run under `tools/gpu-lock.sh`, one at a time; Agent B shared the lock |
| Command | `avgen --live-profile --mode headless --quality ultra --warmup-frames 150 --measure 10 --size 1920x1080 --target-fps 30 --start 60 --audio assets/audio/feline-footwear.wav --no-audio --json/--trace/--capture` |
| Frames and reps | 300 measured frames per run. **Two interleaved reps** per arm (all arms in rep 1, then all in rep 2) unless marked |
| Live runs | `--mode live --quality auto --warmup 5 --measure 30`: 900 frames, a projection window, Fifo, the ladder deciding. Two reps |
| Probe projects | Glass Caldera with one change each:<br>- the `lantern*` nodes removed;<br>- the camera fixed (timeline camera tracks removed);<br>- in the sky and water sets, Caldera's routes removed and `lakeSky` removed;<br>- a dark dusk sky;<br>- a camera pitched up so about 50% of the frame is sky. |
| Aurora settings | ribbons from ADR-1641's `demo-aurora-shapes.json`: altitude 800, fold size 4500, width 0.08, swirl 1.8, edge 0.7, height 2600, fade 20 km; violet, blue and pink colours; intensity 4 |

**Limits:**
- **The Apple clock governor makes light frames noisy.** The no-aurora control `s00` read 6.38, 10.43, 10.36 and
  10.41 ms in four reps. Arms under ~12 ms GPU therefore scatter by ±2-3 ms. Two consequences:
  - I compare **scene-pass label medians** within interleaved sets, and do not compare total frames across sets;
  - the ladder-pinned headless arms (High 13.3, **Medium 8.4, Low 10.0** ms) say more about the governor than the
    work, so they are not used as costs.
- Timestamps are honest (ADR-1600).
- No stems, no routes, and a static camera, so these numbers do not include music-driven state. The ribbons' cost
  grows with how much of the sky is lit (fewer gap early-outs). §5.2 keeps a margin for it.

### 3.2 The aurora (sky-heavy camera, about 50% sky; headless Ultra; median [range])

| Arm | GPU p50 (ms) | Scene pass (ms) | Scene Δ vs none |
|---|---|---|---|
| s00 no aurora (4 reps, 2 batches) | 10.4 (6.4-10.4) | 6.8 (4.0-6.8) | — |
| s01 curtains, 3 shells | 11.42 [11.40-11.45] | 7.82 | **+1.0** |
| s02 ribbons, 16 planes | 14.04 [13.71-14.37] | 11.16 | **+4.4** |
| s03 ribbons, 24 planes | 14.53 [14.50-14.56] | 11.87 | **+5.1** |
| s04 ribbons, 32 planes | 14.45 [14.13-14.77] | 12.18 | **+5.4** |
| s05 ribbons, 48 planes (1 rep) | 15.31 | 13.40 | **+6.6** |

- **Per plane** about 0.07 ms. **Fixed per sky pixel** about 3.3 ms at 50% sky (≈ 3.2 ms per million sky pixels plus
  0.07 ms a plane).
- **Look** (stills 03, 04): 16 planes show a coarse static grain over every sheet. It is locked to the sky, so it will
  crawl when the camera moves. 48 planes read as clean sheets with rays.
- Glass Caldera with the aurora added (v03-v06): the aurora was **hidden by the `lakeSky` disc** (verified by removing
  the disc), so those arms are not aurora costs.

### 3.3 The river (no aurora; headless Ultra)

| Arm | GPU p50 (ms) | Scene pass (ms) | Note |
|---|---|---|---|
| w01 no water, no twins | 6.93-7.04 | 4.1 | control, 4 reps |
| w00 Caldera twins (no water surface) | 9.14 [7.92-10.37] | 5.9 | twins +1 ms in the Caldera-camera set (v00 8.14 vs v07 7.17). This pair is noisy |
| w06 terrain (chunk 100, res 24, mottle), dry | 13.67 [13.40-13.95] | 11.2 | **the terrain is the cost: +6.7 ms** |
| w02 the same terrain + plain water (environment reflection) | 13.87 | 11.6 | +0.2 over dry |
| w03 + cinematic, no SSR (tint patches) | 14.29 | 11.9 | +0.6 |
| w04 + cinematic, SSR 16 steps x 1 ray | 14.56 | 12.3 | +0.9 |
| w05 + cinematic, SSR 48 steps x 4 rays, 1500 m reach | 14.96 | 13.4 | +1.3 |
| **w07 coarse terrain (chunk 200, res 12, no mottle), dry** | 8.59 [8.41-8.78] | 6.5 | **+1.5** |
| w09 coarse terrain + plain water | 9.11 | 7.0 | +2.1 |
| **w08 coarse terrain + cinematic SSR 16x1** | 9.78 [9.77-9.78] | 7.8 | **+2.7** |
| v10 fine terrain + SSR 48x4 + flow streaks (200 lanes) | 15.99 | 14.6 | streaks +0.2 over v08 (within the noise) |

**The aurora is not in the water on either path:**
- still 05: engine water with SSR shows the island, but none of the aurora overhead;
- still 06: Caldera twins with the procedural sky show a flat ground colour where the aurora's mirror should be.

The cause, from the code:
- `waterSsr` hits only where the march passes behind written depth;
- `linear_depth.wgsl` writes 1e7 for the sky, so a ray toward the sky misses;
- the miss falls back to `prefilteredMap`, the procedural sky cube, which carries no atmosphere effects.

The refraction copy taken before the water **does** contain the sky and aurora (sky, then atmosphere, then the copy:
`scene_renderer.cpp` l. 4679-4750). It is just never read for the sky.

### 3.4 Prepass and AO

| Arm (interleaved, 2 reps) | GPU p50 | Scene pass | Depth | AO |
|---|---|---|---|---|
| s07 full (24 planes + twins + stars), AO on | 14.46 [14.21-14.70] | 11.9 | 1.1 | 0.6 |
| s09 the same, AO and contact shadows off (so no prepass) | 15.03 | 14.4 | 0 | 0 |
| s00 no aurora, AO on | 10.38 | 6.8 | 1.5 | 0.9 |
| s10 the same, AO off | 13.23 [12.38-14.09] | 12.6 | 0 | 0 |

Without the prepass, every overlapping range and twin shades its program on hidden pixels. **Keep the prepass**
(`environment.depthPrepass: true`, ADR-1406) and turn AO off. The AO's own 0.6-0.9 ms is the saving.

### 3.5 Live, auto quality (1080p30, 900 frames, 2 reps, fixed camera, no routes)

| Arm | Levels | Misses / 900 | Frame p50 / p95 / p99 / max (ms) | GPU p50 / p95 (ms) | CPU p50 / p95 (ms) | Critical path |
|---|---|---|---|---|---|---|
| s07 ribbons 24 + Caldera land and twins + stars | Ultra 100%, 100% | **3, 4** | 33.35 / 36.3 / 38.5-38.6 / 51.6-53.6 | 15.5-15.9 / 21.2-22.5 | 6.4 / 7.5 | sync/present |
| s06 ribbons 24 + fine terrain + SSR 48x4 water + stars | Ultra 100%, 100% | **1, 1** | 33.35 / 35.4-35.6 / 39.1-39.8 / 58-59 | 17.7 / 26.0-26.3 | 6.4 / 7.7 | GPU, then sync/present |

Both meet the owner's live thresholds as the baseline tool states them:
- T1: at most 5 misses per 900;
- T2: p99 at most 40 ms;
- T3: Medium or better on at least 90% of frames.

The live GPU p95 is stretched by the paced clock (ADR-1362), so it is not a headless cost.

## 4. Proposed scene architecture

### 4.1 Composition: distinct from Glass Caldera

| | Glass Caldera | Aurora Borealis |
|---|---|---|
| Hero | the low sun behind a peak | the aurora overhead, upper half of the frame |
| Water | a round lake, island at the centre | a winding river corridor leading the eye to the horizon |
| Land | rings of ranges round a central island | two converging valley walls (a V), hero peaks on alternate banks, 3-4 receding ranges beyond |
| Light | backlit dusk to alpenglow (peach, coral) | night-dusk indigo, lit by the aurora: violet, electric blue, magenta, pale pink |
| Camera | circles the lake, looking in at the island | travels down the river, looking ahead along it |

**The world** is a closed loop so the route is seamless. Everything is generated by one generator on `meshes.py`.

- **River:** its centreline is `r(θ) = R0 (1 + a sin 3θ + b sin 5θ)`, with R0 ≈ 900 m and a, b chosen so the river
  swings through alternating S-bends.
  - About 70-110 m wide near the camera.
  - Ahead, it narrows by perspective to a vanishing point at about 52-58% of frame height.
- **Inner bank and outer bank:** `polar_range` rings with an explicit height function:
  - a dark, low, faceted bank (5-25 m) at the water;
  - then the valley wall;
  - then the ranges.
- **Hero peaks:** 5-7, art-directed. Each is a listed (θ, r, height, ridge azimuth, ridge length) entry, added to the
  height function as an elongated ridged cone, so each has a deliberate silhouette, alternating bank to bank. The
  noise only breaks up the facets. These are the brief's "deliberate controls over peak placement, ridge shape,
  valley width and silhouette".
- **Receding ranges:**
  - 3 closed outer rings at about 2.5, 4 and 6 km;
  - each higher, smoother and paler toward the sky colour (the program's distance haze);
  - their crest line sits below the lower aurora rim, so the aurora is never cropped.
- **Triangle budget:** about 30k for land; twins are not needed with real water. A pixel-sized facet is noise, so the
  facet size grows with distance (`nr`, `nth` per ring).
- **Banks:** clipped at the waterline (`clip_above`), extending below the terrain ground so they hide it (§4.3).

### 4.2 Layers and per-layer budget (headless Ultra, 1080p native, GPU)

| Layer | Basis (measured) | Budget (ms) |
|---|---|---|
| Frame base: depth prepass + linear depth, post (bloom, grade, vignette, grain, FXAA), tonemap; AO off | depth 0.9-1.5, post 0.35-0.8 | 2.5 |
| Faceted land: walls, hero peaks, 3 outer ranges; one unlit painter's program ≤ 30 ops, ≈30k tris | Caldera's land + twins + sky in a 4-6 ms scene pass; programs ~free | 4.5 |
| Sky gradient (procedural) + stars | v02 ≈ v00; stars inside s07 | 0.5 |
| **Aurora ribbons, 40-48 planes, ≈50% sky** | +5.1-6.6 scene pass | 7.0 |
| **River:** coarse terrain node + cinematic water (SSR 16x1, tint patches) + E1 | w08 − w01 = +2.7; E1 about one tap (estimate) | 3.5 |
| Flow streaks on the river | +0.06-0.2 | 0.3 |
| Music-driven state (a fuller, brighter aurora early-outs less; bloom spread) | not measured; margin | 2.0 |
| **Total** | | **≈ 20** (60% of 33.3; ≈ 9 ms under the controller's 29.3) |

- The nearest measured proxy, s06 (24 planes, the **fine** terrain at +6.7 ms, SSR 48x4, stars), is 16.0-17.7 ms
  headless and 17.7 ms live GPU p50, at Ultra 100%.
- The recommended scene swaps the fine terrain for the coarse (−5 ms) and adds 48 planes (+1.5 ms) and denser land.
  I expect 15-18 ms. That is to be confirmed on the blockout.

### 4.3 The river, its reflection, and E1

**Recommended:**
- **One terrain node** carrying only the river:
  - `world.features`: one `river` feature on the loop centreline, `water: true`;
  - coarse ground (chunk 200, resolution 12, mottle off);
  - its ground set below the GLB banks, so the depth prepass rejects it.
- **Cinematic water:**
  - dark deep colour, low roughness, `ripple` about 0.2, `streak` about 0.1;
  - SSR on: 16 steps, 1 ray, reach about 600-800 m (the near banks);
  - `tintPatches` bound to the aurora's mid colour;
  - **flow streaks** (ADR-1643) in the three aurora colours, at low amount, for the brief's "bands of colour".

**E1 (engine, small):** the sky seen in the water. In `waterSsr` (`shaders/water.wgsl`), on a miss, when the
reflected ray points up:
1. Project the direction at infinity: `frame.viewProj * vec4(r, 0.0)` gives the reflection's vanishing point.
2. If that point is on screen and its linear depth is beyond about half the SSR reach (sky, or a distant range),
   return the refraction copy there (`waterSceneAt`, blurred by roughness), with the existing edge fade as trust.

About 12 lines of WGSL and no new binding. Its properties:
- **The reflection is the rendered aurora itself**, so it is coherent by construction: same shape, same colours, same
  frame.
- The far ranges come out right as well, because at distance D the angular error is about 2h/D (h is the camera
  height). Near banks are SSR hits, as now.
- The ripples and view-stretched rays already perturb `r`, so the aurora's reflection breaks up the way the water does.
- **Make it independent of the SSR switch.** `waterSsr` returns early when SSR is off; E1 should not, so that the
  ladder's `waterSsrScale = 0` at Emergency keeps the aurora in the river.
- **Framing makes it complete.** With the horizon above the frame centre, every water pixel's mirror direction is on
  screen: water reaches about 25° below the horizon, the sky about 30° above.

**Fallback (if E1 or fitting the banks to the terrain fails):** Caldera's twins, plus a **mirror flag on the sky and
atmosphere layers**. For `dir.y < 0`, reflect about the water plane, perturb by a small ripple noise and apply a tint,
in `fs_atmosphere` and in the skybox fragment.
- Its cost is a second aurora march over the water pixels: about 0.45 x the sky's 5-7 ms, so **2.5-3.5 ms
  (estimate)**, plus the twins (~1 ms).
- It needs no terrain, but it loses the engine water's ripples, streaks and glints. The programs would have to paint
  them.

### 4.4 Lighting coherence: aurora, river and facets

The scene has **no lights and no shadows**. Everything is unlit programs, emission and bloom: the Caldera method. It
costs nothing in the lighting categories.

**One palette state drives all three.** Palette roles `auroraLow`, `auroraMid`, `auroraTop`, `skyZenith`,
`skyHorizon`, `facetShadow` and `facetLit` (the project `palette` block, ADR-1420) bind to:
- the aurora's `lowColor`, `midColor` and `topColor`;
- the rock program's aurora-lit facet colour and its shadow colour;
- the water's `tintPatches` colour, `water/glowColor` and `water/streaks/color1..3`;
- bloom.

The water's reflection of the aurora needs no binding: E1 reads the aurora's own pixels.

**The facets' light:** the rock program replaces Caldera's sun term with an **aurora term**:
`facetLit * auroraGlow * saturate(dot(n, auroraDir) * 0.5 + 0.5) * saturate(n.y + 0.3)`.
- `auroraDir` points up and toward the ribbons' mean bearing.
- `auroraGlow` is a program constant driven by **the same route value** that drives `fx/aurora/intensity`, so when the
  aurora brightens, the facets facing it brighten on the same frame.
- The aurora's `ground/*` term reaches only lit PBR and water. In this scene it would only tint the river slightly;
  keep it low, or off.

**Optional spatial coupling** (E3 territory): a `Wave` field (the field bus, 01 §1.3) started on the kick, sampled by the rock
program's `field` op. A kick front then crosses the valley walls in step with the aurora's pulse (E3). Without E3, the
kick is a global brightness and edge flash on the aurora with a matching facet flash.

### 4.5 Camera

- **A timeline loop**, built like Caldera's `camera_track`:
  - `camera/position` and `camera/target` keys along the river centreline at 12-20 m;
  - 150-200 s a circuit at about 15-25 m/s;
  - a gentle `camera/roll` into the bends;
  - target about 400-600 m ahead on the centreline, pitched up 3-6°;
  - vertical fov 50-55°.
- **Deterministic** (ADR-091), so seek equals play and renders match live.
- **The world fills every frame:**
  - the closed outer rings stand above the horizon in every bearing out to about 6 km;
  - below the horizon there is water or bank everywhere;
  - no ground edge is visible.
- **No vortex or funnel hero:** the ribbons' footprint is world-space folds that cross overhead and recede. They are
  not a ring round the viewer.
- The route must never put the camera over land or let a hero peak cover the aurora. The generator can check both, as
  Living Prism's layering check did.

### 4.6 The live ladder: which tier drops what

The scene uses the project `live` block (ADR-1100): balanced ladder, no minimum level.

| Level | Scale | What drops |
|---|---|---|
| Ultra | 1.0 | nothing: 48 planes, SSR 16x1, streaks, stars, bloom |
| High | 0.85 | resolution only. The ribbons scale with pixels, about −1.5 ms |
| Medium | 0.71 | + **E2** ribbon planes x0.75 (36). The upscaler hides the grain |
| Low | 0.5 | + E2 x0.5 (24 planes); `waterSsrScale` 0.5 (existing) |
| Emergency | 0.42 | + `waterSsrScale` 0 (existing). E1 keeps the aurora in the river |

**E2 (engine, small):** `LiveQualityRung::auroraStepScale`, a ceiling on `ribbonSteps`, applied like `waterSsrScale`.
Count it in `effectChanges`. Without E2, the ladder can only lower the scale, which still works because the ribbons
are per-pixel. E2 is a safety margin, not a prerequisite.

## 5. For Agent B: the parameters stem signals should drive

**Routes should not touch these:**
- `audio/sensitivity`: set it to **0**, or the aurora's own mix-band terms bypass the stems;
- `fx/aurora/flowSpeed`, `driftSpeed`, `verticalSpeed` (E4 below);
- `ribbonSteps`, `curtainCount` (quality and structure);
- the procedural sky colours (an IBL rebuild per change).

The aurora effect's id is `aurora`, so its parameters are `fx/aurora/<field>` (the field ids are in
`aurora_effect.cpp`).

| Parameter | Range (rest → peak) | Suggested driver | Character |
|---|---|---|---|
| `fx/aurora/intensity` | 2.5 → 5 (0-8) | synth level (slow) + kick (fast attack, 300-500 ms decay) | overall luminance |
| `fx/aurora/edgeBrightness` | 2.4 → 4.5 | snare (sharp, 200-300 ms) | bright lower rims, the "fold" accent |
| `fx/aurora/filaments` | 0.9 → 1.6 | hats (small depth) | fine rays shimmer |
| `fx/aurora/sparkle` | 0.2 → 0.5 | hats, restrained | glints in the sheets |
| `fx/aurora/ribbonSwirl` | 1.4 → 2.2 | bass (slow, smoothed) | broad fold depth, continuous |
| `fx/aurora/ribbonEdge` | 0.5 → 0.9 | bass | rim undulation (the lower curtains' energy) |
| `fx/aurora/ribbonWidth` | 0.07 → 0.12 | synth | sheet width and persistence |
| `fx/aurora/curtainHeight` | 2200 → 3200 m | synth or section energy (slow) | how tall the curtains stand |
| `fx/aurora/horizonGlow` | 0.2 → 0.6 | synth (slow) | atmospheric luminance |
| `fx/aurora/opacity` | 0.75 → 0.9 | — (authoring) | layering |
| `fx/aurora/lowColor`, `midColor`, `topColor` | palette states | synth context, phrase-gated | palette (with the facets and water) |
| `material/<rock>/op/<n>/constant/constant` (aurora glow) | 0.3 → 1.2 | the same value as `fx/aurora/intensity` | facets lit by the aurora |
| `<river>/water/reflection` | 1.0 → 1.6 | bass | reflection intensity |
| `<river>/water/streaks/amount` | 0.5 → 1.5 | kick or bass, restrained | colour bands on the water |
| `<river>/water/streaks/color1..3`, `<river>/water/glowColor` | palette | palette roles | river colour response |
| `fx/stars/brightness` | 1.0 → 0.6 | synth (inverse) | stars recede when the aurora is full |
| `post/bloom/intensity` | 0.25 → 0.4 | kick, tiny | luminous accent |

The ribbon positional parameters (swirl, edge, width, height) change shape without a phase jump, so smoothed bass and
synth values are safe on them. Keep their slew slow. A large step in `ribbonSwirl` re-folds the whole sky.

## 6. Engine gaps and the smallest changes

| # | Gap | Smallest change | Size | Needed? |
|---|---|---|---|---|
| **E1** | The water reflects no sky effect: SSR cannot hit the sky (depth 1e7), and the fallback cube has no aurora | On an SSR miss for an upward ray, read the refraction copy at the reflected direction's vanishing point (§4.3). Independent of the SSR switch. One GPU test: aurora on, the river's mean colour moves toward the aurora's; aurora off, the shader is byte-identical (ADR-1643 pattern) | S (~12 lines WGSL + test) | **Yes:** the brief's hero coupling |
| E2 | No ladder lever for the ribbons | `LiveQualityRung::auroraStepScale` (ceiling on `ribbonSteps`) in the three ladders; Medium 0.75, Low 0.5 | S | Recommended (a margin) |
| E3 | Aurora parameters are global: no travelling wave through the curtains | `pulseAmount`, `pulsePosition` (0-1 along height or bearing) in `atmosAuroraRibbons`: brightness x (1 + amount · bump(hn − position)), with an envelope (ADR-1601) sweeping the position | S (~15 lines + 2 fields) | Optional. Fallback: a global kick flash |
| E4 | Modulating `flowSpeed`, `driftSpeed` or `verticalSpeed` jumps the pattern: the flow is `elapsed × speed` (`atmospherics.cpp` l. 708, 743) | **No code:** do not route them. Drive motion through swirl, edge and width, or through the effect's `flow.field` influence. If tempo-locked speed is ever needed, integrate the phase per frame (a live-only history) | none | No |
| E5 | 16-24 planes are grainy at native | Use 40-48 planes (cheap). If it is still visible in motion: march the atmosphere at half resolution and upsample (ADR-1641's revisit trigger) | M | Only if the blockout shows grain in motion |

Not needed: textures (ADR-1642), refraction or dispersion, volumes, extra lights, a new pass, a new renderer path.

## 7. Risks

1. **E1 fails in practice.** Ripples push mirror lookups off screen near the frame edges (fade to the cube there), or
   a near bank's reflection looks wrong where SSR misses. Mitigations: SSR covers the near banks, keep the horizon
   above centre, and the fallback in §4.3.
2. **Fitting the faceted banks to the terrain river:** seams or gaps at the waterline, or the smooth terrain ground
   showing. Mitigations: banks extend below the terrain ground, and the generator samples the engine's heights
   (Living Prism's `avgen_world_preview --heights` method).
3. **Ribbon cost in the brightest states:** more of the sky lit means fewer gap early-outs. Measure the brightest
   passage on the blockout before polish (§4.2 keeps 2 ms for it).
4. **Grain crawling with camera motion** at fewer than 40 planes, or at reduced rungs before the upscaler. Check on a
   moving clip, not a still.
5. **The light-frame clock governor** makes small A/Bs unreliable. Judge with interleaved reps and scene-pass labels,
   and confirm in live mode.
6. **Sky colour routes rebuild the IBL** (01 §2.6). Palette changes that touch the sky should be rare and
   phrase-gated.
7. **CPU:** the live CPU was 6.4 ms with no routes; the critical path was present or GPU. Stem routes are cheap, but
   watch the UI's cost while projecting (Rainbow World: 7.8 ms UI, 01 §4.3).

## 8. Open questions (for the lead or owner)

1. Is the **E1 path** (engine water plus the copy lookup) accepted over the twins-plus-mirror-flag fallback? I
   recommend E1. Both need an engine change.
2. Is the **aurora's travelling pulse (E3)** worth an engine change, or is a global kick flash on the ribbons with a
   facet flash enough for the first pass?
3. **Camera loop length and speed:** a 150-200 s circuit means Feline Footwear (about 3.5 min) sees a little over one
   loop. Should sections line up with bends?
4. Should the scene be **registered under Sonic Abstract** (beside Digital Alpine), or as its own live category?
   Either way it needs `index.json` `live: true` and `sonic.live`.
5. **Stars:** keep a dim layer, or leave the sky to the aurora alone?

## Appendix: rebuilding the probes

The generators expect to sit beside a `meshes` symlink to `examples/sonic-abstract/meshes`.

1. `make_variants.py` writes the v-set (the Caldera camera).
2. `make_sky.py` writes the s-set. It imports the v-set's helpers.
3. `make_water.py` writes the w-set. The coarse terrain arms are the w-set with `chunkSize 200, resolution 12,
   lodLevels 3, lodDistance 300, groundMottle false` patched in.
4. `run.sh <headless|live> <rep> <arm...>` runs each arm under the GPU lock, with `QUALITY` and `TAG` for pinned
   levels.
5. `summarize.py headless|live` prints medians and ranges.

Raw JSON, traces and captures are in the scratchpad's `probe/out/`. The key stills are in the review folder.
