# Glowmere: Painterly Experiment

Status: a tested look-development candidate, not finished-product acceptance. The direction
is an original stylized alien ecosystem: clear silhouettes, soft lighting bands, broad color
regions, atmospheric depth and selective warm bioluminescence. No Nintendo assets or designs
are included. Existing asset licences still apply; see [asset-library.md](asset-library.md).

## Comparison projects

- [Painterly](../examples/world/glowmere-stylized.json): the new scene and stylized surface mode.
- [Matched PBR](../examples/world/glowmere-stylized-pbr.json): identical scene and camera with
  the surface mode disabled.
- [Prior candidate](../examples/world/terrain.json): preserved scene, using the corrected
  renderer. Its earlier development record is in [shot-glowmere.md](shot-glowmere.md).

The first two entries are available under Look development in the example browser. All three
retain the same 90-second camera automation. The new scene adds broad fan plants, denser grass,
lower-budget vegetation, a violet crown with warm gills, restrained vegetation emission and
an analytic night sky. Existing wind, plant motion and smoothed audio routes remain active.
No soundtrack is bundled; musical response has not been auditioned against a finished track.

```sh
cmake --build --preset release -j 4
./build/release/src/avgen --project examples/world/glowmere-stylized.json --play --tier preview
./build/release/src/avgen --project examples/world/glowmere-stylized.json \
  --render renders/glowmere-stylized-review.mp4 --range 0:90 --size 1280x720 --fps 30 --codec h264
```

Render paths resolve against the project directory. Generated review media are locally ignored
by git. The complete movie wrote 2,700 frames with zero GPU errors, sequence hash
`42c44134a146b511`. Its 37.9 encoded frames/second is offline throughput, not interactive FPS.
Successful encoding and inspected stills do not establish temporal visual quality.

## Renderer contract

Composition JSON `environment.stylized` is a strict boolean, default false. The registered
boolean parameter `scene/stylized` can override it live. Serialization preserves the base value.
The renderer uses the existing spare `lightCounts.z` uniform component; GPU layout is unchanged.

The shared entity/procedural/SDF surface path uses soft punctual-light bands, a broad restrained
highlight, hemisphere ambient, screen-space AO and a subtle rim. Existing shadows, fog and
emission remain. Lit stylized materials ignore base-texture RGB, retaining factors and material
program colors; mask/blend materials still sample texture alpha. Unlit materials retain texture
RGB. Normal, metal/roughness and occlusion textures and split-sum IBL are bypassed by this path.
Emissive textures and general material programs are not bypassed. Area lights retain the LTC
path. The ramp and hemisphere colors are currently shader constants, not per-material controls;
the stylized hemisphere does not follow environment-map intensity.

The procedural sky adds direction-anchored filtered stars and a small disk in the first
directional light's direction. These overlays are disabled for HDR skies and PBR mode. The disk
is outside the opening shot's framing. Thus the matched PBR image differs in sky overlays as
well as surface shading, while geometry and camera remain fixed.

A separate correctness fix makes procedural depth respect scalar/texture alpha cutouts, as
the entity path already does, and marks both vertex positions invariant across pipelines.
This avoids invisible foliage rectangles occluding geometry. It does not add material-program
opacity evaluation to depth. The correction increases honest baseline cost because previously
hidden surfaces now render; pre-fix timings are not valid optimization comparisons.

## Measurements

Hardware: **Apple M2 Max**, not base M2. Release build, 1440x900, realtime tier, first 10 seconds
of the shared camera shot. Three interleaved runs per project, each 300 frames at fixed 30 Hz
simulation, with 288 warmed frames measured. No concurrent build or GPU workload. All nine runs
reported zero GPU errors. `--fps 30` is simulation cadence, not achieved rendering speed.

| Metric | Prior candidate | New scene, PBR | New scene, stylized |
|---|---:|---:|---:|
| Wall median, ms (median of runs) | 58.05 | 31.05 | 31.18 |
| Reciprocal median, FPS | 17.23 | 32.21 | 32.07 |
| Wall p90, ms (median of runs) | 73.31 | 37.40 | 38.47 |
| GPU median, ms | 53.94 | 26.28 | 26.41 |
| Scene GPU, ms | 50.27 | 23.33 | 23.53 |
| Shadow GPU, ms | 0.98 | 0.92 | 0.92 |
| Volume GPU, ms | 1.25 | 0.66 | 0.66 |
| Particle GPU, ms | 0.07 | 0.07 | 0.13 |
| Sampled procedural CPU, ms | 0.40 | 0.47 | 0.48 |
| Sampled engine CPU, ms | 0.72 | 0.44 | 0.44 |
| Peak process RSS, MiB | 1218.9 | 740.4 | 748.4 |
| Draws / indirect calls at last sample | 106 / 146 | 107 / 150 | 107 / 150 |
| Shadow draws at last sample | 117 | 119 | 119 |
| Visible / logical instances at last sample | 1714 / 81212 | 2255 / 116612 | 2255 / 116612 |
| Logical triangles at last sample | 39247387 | 15137808 | 15137808 |

CPU numbers are medians of periodic update samples, not whole-frame CPU latency. RSS is process
high-water memory, not independently measured GPU allocation. Logical triangles are not
post-culling/LOD submitted geometry. Independent pass medians need not sum to the frame median.
Particle capacity remains 10,240; volume steps fall from 18 to 12.

Raw per-run wall medians: prior 58.05/57.05/60.36 ms; matched PBR 30.63/31.05/31.24 ms;
stylized 31.13/31.18/31.24 ms. The scene is 46.3% cheaper in wall time with 43.6% more logical
instances, but the surface modes effectively tie. **No shader-only speedup is established.**
The observed gains come from simpler assets, fewer repeated material programs, reduced volume
steps and selective lighting. Disabling shadows alone did not resolve the earlier edge artifacts.

Reproduce each project in interleaved rounds with:

```sh
/usr/bin/time -l ./build/release/src/avgen --headless \
  --project examples/world/glowmere-stylized.json --frames 300 --fps 30 --size 1440x900 --tier realtime
```

An uncontended three-run 1280x720 preview sample measured wall medians 21.56/23.97/24.99 ms
and p90 29.89/32.62/35.80 ms, with zero GPU errors. That is approximately 41.7 reciprocal-median
FPS, not sustained 60 FPS or even a whole-shot 30 FPS guarantee. Quality tiers remain manual.

## Validation and remaining gates

The full release CTest run discovered 922 tests: 917 passed, four optional tests skipped, and
one Syphon frame-burst test failed at `frame.has_value()` in `test_texture_share.cpp:214`.
The isolated rerun also failed. The same test failed intermittently before this style work;
its root cause remains unresolved. Skips cover two Khronos asset imports, ffmpeg and NDI.
The full suite is **not green**.

New GPU coverage passes 33 assertions across four cases: deterministic/reversible mode switching,
texture-color policy, alpha-depth visibility and direction-anchored sky. All three shot contracts
pass 13,664 assertions. Six material programs pass 6,662 finite-output and emission checks.
An ASan/UBSan run passed the mode, shot and earlier three-material checks (17,003 assertions);
the expanded six-material test subsequently passed all 6,662 assertions under ASan/UBSan.

```sh
ctest --preset release --output-on-failure
./build/release/tests/avgen_render_tests '[stylized]'
./build/release/tests/avgen_tests '[integration][glowmere]'
./build/release/tests/avgen_tests '[material][glowmere]'
cmake --build --preset asan --target avgen_tests -j 4
./build/asan/tests/avgen_tests '[material][glowmere]'
```

Outstanding acceptance work: noisy foliage edges and ground patterns, repetitive tree
silhouettes, sparse hillside composition, an overly regular hero and a stronger ending.
Continuous playback must check shimmer and LOD changes. Neither premium visual quality nor
the 16.67 ms frame budget is met. Base-M2 testing, sustained thermal behavior, whole-shot timing,
dedicated GPU memory measurement and soundtrack review remain unverified. This experiment
supports retaining both surface modes, not declaring the stylized shader a faster replacement.