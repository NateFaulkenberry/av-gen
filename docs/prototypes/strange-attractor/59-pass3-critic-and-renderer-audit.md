# Pass 3, Phase A (Agent B): the Critic audit, the renderer audit and the baseline

The brief is `55-environment-art-pass-brief.md` (§2, §8, §9, §12 "Agent B"); the split is `56-`. This is the SA
agent's half of Phase A. The hero agent's half (the environment and asset audit, and the art direction) is separate.
Nothing here changes the scene: Phase A only measures.

## 1. The Cinematic Critic: what it is and how it runs

**There is one evaluator:** the Creative Critic, a separate local service at `~/Documents/GitHub/creative-critic`.
"Cinematic Critic" in the brief means it. AV Gen has no evaluator of its own.
- Its CLI is `~/Documents/GitHub/creative-critic/.venv/bin/critic` (not on PATH). The daemon was already up
  (`critic health`: ok, light and deep workers).
- The docs are INTEGRATION_GUIDE.md, EVALUATION_SCHEMA.md, SPEC.md and MODEL_BENCHMARKS.md.

**How it is invoked:**
- `critic submit --mode fast|preview|deep ... --wait --json`, then `critic shot`, `critic findings`,
  `critic frame`, `critic compare a b`, and `critic session history`.
- Inputs:
  - pixels: `--video` (with `--video-start` for a clip in film time), `--sequence`, or `--images`;
  - optional: `--scene`, `--shots`, `--intent`, `--audio`, `--ground`, `--events`, `--regions` and AOVs.
- The AV Gen adapter (`adapters/avgen/avgen_adapter.py`) builds the scene, shots and cast inputs from a project and
  a cast trace. It was written for Glowmere Valley 3's characters.
  - Strange Attractor has never used it, and the pass does not need it. Its scene-side checks (framing, projected
    subject, grounding) are about characters and set pieces, while this pass is about the ground they stand on.
  - So the scene-only dimensions (depth, character staging, effects, the scene-camera half of cinematography)
    stay "not evaluated" here, and the report says so.

**Which cameras it uses:** none of its own. It judges the pixels it is given.
- For this pass, the "original camera positions" are the film's own camera. The regression set renders exactly
  those frames.
- The supplementary cameras are the establishing and hero cameras in
  `examples/strange-attractor/pass3/cameras.json`. They are added beside the original ones, never in place of them.

**How the reference renders are made:** by AV Gen. The Critic never renders.
- Stills: `pass3/render_cameras.py`, at 1920x1080, supersample 2, Offline tier, through gpu-lock.
- Clips: `avgen --render leg-<leg>.mp4 --range a:b` at 1280x720 with the song's audio.

**What it evaluates:**
- 16 dimensions, each scored `exp(-sum of severity x confidence / norm)` over its findings.
- `environment` is built from `image.metrics` and `scene.grounding` only.
- The measured inputs per shot are luma (mean, p01, p99), RMS contrast, dynamic range, clipped and crushed
  fractions, saturation, colourfulness, warmth, sharpness (Laplacian variance) and the dark channel.
- Over time it adds optical flow and shimmer, flicker, stability, novelty, repetition, flashes, and audio
  reactivity.
- In deep mode it can also run learned image models. The pyiqa aesthetic and quality models are installed but
  "experimental".

**What it produces:**
- `report.json`: dimensions, findings with stable keys, measurements and model outputs.
- `report.md`.
- `evidence/` frames and `contact_sheet.jpg`.
- `critic compare`: dimension deltas, findings resolved/new/persisting, and pixel deltas (luma, contrast, Lab
  shift, ΔE).
- A session history with trends and plateau notes.

**How to read it (the important part for an art pass):**
1. **It cannot see geology or materials.**
   - Nothing in it measures what the brief's §9 asks: tiling, UV stretch, believable structure, material
     sophistication, silhouettes or landmarks.
   - `environment` is a tonal and grounding score.
   - The hero and establishing questions are therefore judged by eye on the before/after pairs. The Critic is
     evidence for what it does measure.
2. **Its dimension scores saturate.** A clean shot scores 1.00. The M3e history shows that: "No dimension moved
   beyond the noise band" between real art changes. A rising score means fewer findings, not a better picture.
   The useful signals are:
   - the **measurements**: contrast, dynamic range, crush, clipping, saturation and warmth per world (the brief's
     §8.2 "distance-dependent contrast" and §8.4 "distinct per world" can be checked this way);
   - **shimmer and flicker** in the clips (aliasing and swimming detail at traversal distance, which is where
     added micro-detail usually breaks);
   - the **findings**: flat image, near-black, clipping, pale for night, oversaturated;
   - `compare --pixels` between identical cameras.
3. **The learned models are weak evidence.**
   - Its own benchmark against a production's preferences put topiq_iaa and clipiqa+ at 0.65 agreement and
     laion_aes at 0.575 (chance is 0.5; 40 pairs, ±0.15).
   - They are run on every still (`--options '{"models": [...]}'`; "auto" selects none, because none is
     "recommended").
   - Read them as a trend over a world's 6 to 10 frames, never as a verdict on one frame.
4. **It works on sections.** A clip with `--video-start` scores only the shots it covers, and `only_shots` runs a
   single shot. `--images` treats each still as a shot (img01, img02 and so on). `critic_run.py` maps them back to
   camera ids.
5. **Existing baselines:**
   - session `strange-attractor-m3e` holds the M3e and M3f ocean, Mars, dusk and night clips. These are older
     versions on different ranges, so they are not comparable.
   - Pass 3 therefore starts its own session, `sa-pass3`, with tracks `stills-<world>` and `clip-<leg>`, so every
     later run compares to the previous one in the same track.
6. **No integration problem was found.** The Critic is unchanged.

**The loop each phase uses (brief §2.2):**
1. Re-render the phase's world: `render_cameras.py --only '<world cameras>'` and the leg clip.
2. `critic_run.py ... --worlds <world> --label <phase-iter>`.
3. Compare the frames side by side with the baseline, plus the Critic's measurements and findings.
4. Fix.
5. At the end of the phase, run the whole regression set (the guard in 56-).

## 2. The renderer, materials and post: what is available, what is used, what limits the environments

This covers only the capabilities that matter for environment art. Every scene renders at the Offline tier with
supersample 2 (ADR-147 is long fixed: `RenderJob` selects Offline).

### Geometry
- **The terrain is a height field.** It is 4096² over 16 km, scaled per world, which gives these texels:
  - canyon: 1.56 m (worldScale 0.4);
  - Mars: 1.95 m (0.5);
  - ocean: 1.37 m (0.35);
  - Moon: a 2.5 m grid.

  Error-bounded triangulation keeps 0.08-0.2 m vertical error near the path. **Anything smaller than about 1.5 m
  (ledges, fractures, boulders, scree, undercuts) cannot come from the terrain.** It has to come from meshes
  (Blender, the hero agent's lane) or from the material's normals. There are no overhangs: a height field has one
  height per point.
- **There is no displacement, tessellation or parallax occlusion mapping** in the lit mesh path. Displacement exists
  only for procedural deformers, not for terrain or glTF materials.
  - Micro relief is normal-mapped only, so grazing light flattens it at hero distance.
  - This is the largest renderer limit for "do textures hold up at a close camera". An offline POM or a terrain
    detail mesh near the path would be the targeted change if the hero cameras need it.
- **Generator knobs** (attractorWorld): roughness, ridges, domain warp, terraces, erosion, strata fold, side
  canyons, pads.
  - The Mars style adds yardangs (ADR-1902); the ocean uses its own.
  - These shape the macro forms the brief says to keep.

### Materials
- **Material programs** (ADR-036 onward): up to 96 ops, 4 layers and 4 fields.
  - The ops: noise, Voronoi and Voronoi-edge, curvature mask, edge wear, height blend, detail normal, micro detail,
    triplanar, world projection, Oklab ramps, smoothstep, threshold and swizzle (slope and height masks).
  - **At most 4 texture ops per program** (TextureTriplanar plus its normal is 2). One program can therefore blend
    two photoscanned sets, with a third only as a colour.
  - Rock-to-sediment-to-scree transitions with three scanned materials need either a third set's normal or two
    programs split by node. A small engine request would raise it (to 6 or 8).
- **Hex anti-tiling** (ADR-1905, `constant3`) is available to every texture op. It is used on the canyon and Mars.
- **glTF PBR** is the standard for Blender exports: baseColor, metallicRoughness, normal, occlusion and emissive.
  The hero agent's assets come in this way.

### Lighting and shadows
- **Lights:** the sun with cascaded shadows (4 cascades × 4096 at Offline; the canyon's range is 1200 m), plus
  point, spot and area lights with cube and spot shadows inside an 8-view budget (ADR-1906). Per-look controls are
  bias, contact and casts (ADR-1907).
- **Indirect light:** IBL (environment map or procedural sky) plus a fallback ambient. There is **no global
  illumination**: no bounce from the sunlit canyon walls into the shadowed side. That warm bounce is most of what
  makes a real canyon read as a canyon.
  - The path tracer (`--pathtrace`, Embree, CPU) exists and can trace a one-frame reference. Whether it handles
    this content is unverified (material programs, terrain and the portal composition), so it is a look-dev
    reference at most, never the film's renderer.
- **Ambient occlusion (GTAO) and contact shadows** are both available, but this table is what is on:

  | World | AO | contact shadows |
  |---|---|---|
  | canyon (day, dusk, night, dawn) | off | on |
  | Moon | off | off |
  | Mars | off | off |
  | ocean | on | on |

  The ocean's M3g lighting turned them on. **AO is the cheapest available improvement to crevices, strata steps
  and the base of every rock on the canyon, Mars and Moon,** and it is a scene setting, not engine work.

### Atmosphere and water
- **Fog:** height fog (density, falloff, upper density, curve, ground follow, pooling), sky fog, and the per-channel
  spectral extinction (ADR-1908, the ocean only).
  - A ray-marched volume adds anisotropic scattering, local lights in the volume, noise and shadowed light shafts.
  - `post/look/atmospheric` adds a screen-space distance tint.
  - Mars's volume density is 0.00024: almost no dust. The canyon's is 0.000125.
  - The brief's §8.2 ("distance-dependent colour, contrast, visibility; not uniform fog") is fully expressible with
    what exists.
- **Underwater limit:** extinction depends only on the distance from the camera. There is no absorption with depth
  below the surface, so a seabed seen from 3 m keeps its full surface colour.
  - The baseline hero still `h-ocean` shows it: the sediment reads salmon-red at the camera's feet.
  - Real water also removes red from the *light* on the way down. **This is the one engine change Phase E needs:**
    downwelling attenuation by depth (an ADR in 1910-1919), applied to the sun, sky and ambient below a water level.

### Post (what is used versus what exists)
- **Used in the film:** AgX tone mapping, bloom 0.04 (0.06 in the ocean), and the exposure track.
- **Available and unused:**
  - grading: lift, gamma, gain, temperature, tint, saturation and contrast;
  - local contrast (`post/look/localContrast`, with a radius);
  - light wrap;
  - halation;
  - film grain and vignette (`post/output/grain`, `vignette`);
  - physical depth of field;
  - motion blur;
  - sharpen;
  - and the stylised family (glitch, outline and so on, not for this pass).
- The brief's §8.3 list maps onto this almost one to one: highlight roll-off (AgX), restrained bloom, subtle grain,
  grading, local contrast, selective DoF, subtle vignette. **Phase G needs no new post code,** only per-world
  values tuned against the baseline.
- Motion blur at 30 fps on a fast flythrough is the one item to test carefully. It reduces shimmer and the "clean
  digital" look, but it softens the detail the pass adds.

## 3. Technical recommendations (Agent B), in order of value for the brief

These are recommendations only. Implementation waits for the owner's OK on the plan.

1. **Turn AO on for the canyon, Mars and Moon,** and contact shadows on for Mars and Moon (scene settings, judged
   on the hero cameras). Phase B/D/F settings, applied through the hero agent's or my scene files.
2. **Underwater downwelling absorption by depth** (one engine ADR). This is Phase E's foundation, so close
   sediment stops reading as dry ground.
3. **A cheap bounce term for the canyon.** A per-world "ground bounce" hemisphere colour (the sunlit rock's tint)
   applied to the ambient, to stand in for GI. Prove it on `h-canyon-day` before any engine work; if a scene-level
   fill light does it, no code is needed.
4. **Atmospheric depth by world** with existing controls: Mars dust (volume density and anisotropy, height fog,
   warm extinction), the canyon's aerial perspective (`post/look/atmospheric`), the ocean's spectral fog (already
   built). No new code.
5. **More texture ops per program** (4 → 8) if Mars or the canyon needs a third scanned set with its normal.
   Small engine change, on request.
6. **Close-range relief:** offline parallax occlusion on terrain materials, or a near-path detail mesh. Only if the
   hero cameras still read flat after the hero agent's meshes and scans; it is the most expensive item here.
7. **Phase G post:** grading, local contrast, grain, vignette and selective DoF per world from existing controls,
   checked against the regression set for no regressions.

## 4. The baseline (M3g HEAD 3b562396, rendered 2026-11-03)

Media: `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/00-baseline/`
- `frames/<camera id>.png` (52; 1920x1080, supersample 2)
- `clips/leg-<leg>.mp4` (7; 1280x720, the song)
- `01-regression-legs-sheet.png`, `02-regression-crossings-sheet.png`, `03-establishing-sheet.png`, `04-hero-sheet.png`
- `critic-baseline.json` (every still's measurements and model scores; every clip's dimensions, findings, shimmer,
  flicker, jitter, sharpness)
- `05-luma-per-half-second-vs-m3f.txt` (the film's luma against the M3f render, every half second)

### Two defects the baseline found before any art change

1. **REGRESSION (mine, M3g): the Moon goes black for about 5.5 s (37.5-43.0 s).**
   - What happens: the frame is 100% below 3/255 from 38.5 to 42.5 s. In M3f the same seconds showed the Earth,
     the regolith and the lit domes (`01-audit-and-direction/A1-...png`, M3f on top, now below).
   - Cause: M3g's camera rule took the Moon's aims away. The camera now looks along its dive into the great basin,
     whose floor is in full lunar night.
   - The owner has not seen it: nothing rendered the Moon leg in motion since M3f.
   - The Critic did not flag it either. A leg is one shot for it and it samples 5 frames a shot, so a 5 s black
     stretch inside a 16 s shot averages away.
   - This is why the regression guard also tracks luma every half second (`pass3/luma_track.py`). It flags exactly
     these 15 half-seconds; the other 15 flags (93.5-100.5 s) are Mars's approved four-bar cut shifting the night
     earlier.
   - Proposed fix (Phase F, the owner chooses):
     - **(a)** earthshine and the domes' spill lighting the basin floor (environment light, keeps the §1 rule);
     - **(b)** a Moon-only gaze that keeps the horizon in frame during the dive;
     - **(c)** both.
2. **The ocean renders red as the root scene.**
   - When `strange-attractor-ocean.scene.json` is the root (a fixed camera), its terrain draws salmon-red
     (`A2-...png`). Drawn as the night portal's destination (the film), the same terrain is the pale teal reef.
   - It is not the fog, the volume or the octopus (each removed in turn, no change). The terrain's own colour
     differs.
   - Likely cause: the attractor world's built-in Mars style material is used instead of its `terrainProgram` when
     it is the root. It is unconfirmed, it is the first thing Phase E diagnoses, and **the film is not affected**.
   - Until then, `e-ocean` and `h-ocean` are invalid as baselines. The ocean's baseline is its four film frames,
     its two crossings and its clip.

### The camera traces (the guard's "must not get worse than M3g")
- **Forward vs velocity, per leg, max / p95 (deg):**

  | leg | max / p95 | over 25° |
  |---|---|---|
  | canyon | 14.3 / 9.6 | |
  | Moon | 28.9 / 11.9 | 44.10-44.30 |
  | dusk | 41.9 / 7.5 | 48.03-48.27, the portal hand-over |
  | Mars | 14.2 / 13.9 | |
  | night | 20.3 / 10.0 | |
  | ocean | 2.1 / 2.0 | |
  | dawn | 19.1 / 1.4 | |

- **Clearance:**
  - dusk: 2.34 m over the terrain (50.9 s); 3.47 m to a mesh box (camp boulder, 51.6 s).
  - night: 2.46 m over the terrain before the sea-portal descent, which reaches 0.46 m at the crossing by design;
    3.21 m to a mesh box (camp scrub, 96.7 s).
- The trace files: `scratchpad/m3e/ct/audit-p3-base.txt`, `clearance-p3base-{dusk,night}.txt`, re-run with the
  same tools at the end.

### What the stills measure, per world (means; the Critic's image metrics and learned scores)

| world | luma | RMS contrast | saturation | warmth | topiq_iaa | clipiqa+ | laion_aes |
|---|---|---|---|---|---|---|---|
| canyon (day) | 0.31 | 0.16 | 0.40 | +0.09 | 5.24 | 0.58 | 5.29 |
| Moon (frames without the black) | 0.14 | 0.15 | 0.21 | +0.01 | 5.44 | 0.65 | 5.63 |
| dusk | 0.23 | 0.09 | 0.48 | +0.03 | 5.05 | 0.54 | 4.81 |
| Mars | 0.57 | 0.11 | 0.35 | +0.23 | 5.05 | 0.59 | 5.16 |
| night | 0.28 | 0.12 | 0.33 | +0.03 | 5.34 | 0.56 | 5.02 |
| ocean (film frames) | 0.28 | 0.12 | 0.44 | -0.07 | 5.39 | 0.59 | 5.19 |
| dawn | 0.40 | 0.09 | 0.25 | +0.03 | 4.51 | 0.47 | 4.63 |

**What the numbers say, and they agree with the frames:**
- **Mars is the flattest world.** Its RMS contrast is 0.08-0.09 on the three plain frames, the lowest of any world.
  It is bright (0.61) with a narrow dynamic range (0.34): a uniform plain under uniform haze, which is the brief's
  "too flat and uniform".
- **Dusk is the lowest-contrast canyon** (0.06-0.11). The establishing camera's 0.06 is a wall of one value.
- **Dawn scores lowest** on all three models and has the lowest sharpness in the clips (78 against 450-1800). After
  185 s it is mostly sky.
- **The ocean's film frames are a narrow teal band** (contrast 0.09, warmth -0.13). The brief's "terrain in front of
  a blue background": nothing nearer is darker or clearer than the far wall.
- **The canyon has the most shimmer in motion** (0.095, with dusk at 0.131). Fine strata and wire at distance swim
  between frames; added micro-detail must not raise it.
- The models rank within a world, not between worlds. Between worlds they mostly track contrast.
- **The clip dimension scores are all 0.97-1.00:** saturated, as expected (§1.2). The only findings are low ones:
  - the Moon's "long flash" (the portal glare);
  - the ocean's "cut02 repeats cut01";
  - dawn's "the shot keeps going after it stops showing anything new". This one is a fair note on the last 25 s of
    sky.

### What the frames show (by eye, per brief §9)
These are notes for the art direction. They are not decisions (the hero agent's audit leads on the environments).
- **Canyon (day):**
  - Establishing: the masses and strata read well.
  - Hero (`h-canyon-day`, river level): the faces are smooth and shaded flat, with thin bright strata lines like
    seams. There is no relief under about 1.5 m, so it fails "do textures hold up" at close range.
  - Shadowed sides are flat violet with no warm bounce.
- **Dusk and night camp:**
  - The wall above the camp (`e-dusk`, `e-night`) shows dashed horizontal streaks that read as a texture fault
    (strata or anti-tiling at grazing angles).
  - The clearing is an open flat with evenly scattered rocks (§6.5's target).
  - `r-night-096`, `r-night-104`, `r-night-112` show irregular dark blotches on the near rock: shadows of something
    unseen. Worth identifying in Phase C.
- **Mars:**
  - The plain is uniform from 72 to 85 s.
  - The pyramids sit on the sand with no apron, drift or buried courses.
  - The mesas' strata repeat in bands.
  - At hero distance the sand is one smooth sheet.
- **Ocean (film frames):**
  - The reef mounds are evenly scattered, identical-looking cones (the generator's yardang-like peaks).
  - The base is a box on a plain.
  - The far wall sits at one value against the water.
- **Moon:** the composition holds. Apart from defect 1 the leg is clean. `h-moon` is the darkest frame (crush 0.33)
  that still reads: dome light on regolith works.

## 5. The guard and the tools (committed)
- `examples/strange-attractor/pass3/cameras.json`: the shared set.
- `render_cameras.py`: renders the set. `--cam` takes an ad-hoc probe camera; `keep`, `patch` and `dropNodes`
  isolate a cause.
- `critic_run.py`: stills and clips through the Critic. `--no-stills` and `--merge` are for re-runs.
- `sheet.py`: labelled sheets. `--before` makes before/after pairs.
- `luma_track.py`: the film's luma every half second against any earlier render.
- After each phase: re-render the regression set and the phase's eval cameras, then run `sheet.py --before`,
  `critic_run.py` with the phase label, `luma_track.py` against the baseline clips, and the camera audit and
  clearance at the end.
