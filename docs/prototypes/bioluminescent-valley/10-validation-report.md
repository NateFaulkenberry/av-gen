# Bioluminescent Valley: validation report (spec 09, Phase 6)

Date: 2026-10-09. Branch: `proto/bio-valley-water`, cut from `ada3a3a8` (the cinematic finishing pass). Machine: Apple
M2 Max, macOS. Every result below was produced by a run whose output was read. Nothing is reported as passing
without one.

## What was validated

**The code built and tested is `4d98ec52`:** ada3a3a8 plus two commits.
- `f2efbaf2`, ADR-1291 (`mirrorContinuity`). The owner later rejected it (`aaffcc42`), and it is not to be merged.
  It is off by default and its 0 path is the ADR-1281 code, so it does not change any existing scene.
- `4d98ec52`, a new GPU test for ADR-1290.

The commits after it (`aaffcc42`, `e36597ad`) change documentation only.

### Shared engine changes under review (cinematic pass, all from ada3a3a8)

| ADR | What | Code | Tests |
|---|---|---|---|
| 1286 | cinematic water: frame copy, SSR, transmission, swell, roughness variation, tint patches, glints; byte-identical when off | `shaders/water.wgsl`, `src/rendering/water_renderer.*`, `src/rendering/scene_renderer.cpp`, `src/scene/water_surface.*`, `src/scene/composition.cpp` | `test_water_cinematic.cpp` (3 CPU cases), `test_water_cinematic_gpu.cpp` (6 GPU cases) |
| 1287 | an emission tint may lift a channel (procedural `emissiveColor` keeps authored values above 1, up to 64) | `src/scene/procedural.cpp` | `test_procedural_authored_range.cpp` (1 CPU case) |
| 1288 | a render or headless run refuses a project whose scene did not load (exit 1) | `src/app/engine.*`, `src/app/application.cpp` | `test_project_system.cpp` (1 CPU case), plus the hand check below |
| 1289 | sculptural clouds from authored lobes (`clouds` block, CloudRenderer in the Environment seam) | `src/scene/clouds.*`, `src/rendering/cloud_renderer.*`, `shaders/clouds.wgsl` | `test_clouds.cpp` (2 CPU), `test_clouds_gpu.cpp` (1 GPU) |
| 1290 | 16 material program slots (was 8) | `src/rendering/material_programs.hpp`, `shaders/material.wgsl`, `src/scene/route_liveness.cpp` | `test_route_liveness.cpp` (bookkeeping); NEW `test_material_gpu.cpp [adr1290]` (rendering), plus the hand check below |

**Scene-specific work** (examples/biovalley, assets, shots) is not engine code and is not covered by these suites,
except for the [rift] scene guards.

### ADR-1290 review finding: a test was missing

The table, the shader constant, the select buffer (`kMaxGpuMaterialPrograms + 1` regions) and the block size
(static_assert) all derive from the one constant, so they are consistent. But no test showed that a program in slots
9-16 actually renders: the only test was the liveness bookkeeping.

Added `4d98ec52` ("the ninth to sixteenth material programs render; the seventeenth falls back to the material",
`[material][gpu][adr1290]`). It builds 17 flat-colour programs and checks:
- programs 9, 12 and 16 render red;
- program 17 is not uploaded and shades with the material's own grey.

It passes.

## Clean build

```
cd /Users/natefaulkenberry/Documents/GitHub/av-gen-biovalley-water   # HEAD 4d98ec52, clean tree
cmake -S . -B build/clean -DCMAKE_BUILD_TYPE=Release > clean-cfg.log 2>&1
cmake --build build/clean -j 10 > clean-build.log 2>&1
```

- The build directory was fresh (`build/clean`, created by this run).
- No source was edited while it built (12:54:15 to about 13:05).
- **Result: exit 0, 0 compiler errors**, `[100%] Built target avgen_tests`.

## Test data prerequisite: the RIFT scene

```
python3 examples/bioluminescent/build.py    # this version has no --scene-only flag; it writes all three
```

- Exit 0. It wrote `rift.scene.json` (14.0 MB, gitignored), `rift.json` and `rift-live.json`.
- `git status` afterwards was clean: the tracked projects came out identical.

## CPU suite

```
tools/gpu-lock.sh bash -c './build/clean/tests/avgen_tests > cpu-suite.log 2>&1; echo "BIN EXIT $?" >> cpu-suite.log'
```

Run under the GPU lock (the CPU suite encodes video). Started 13:05:41, finished 13:48:41.

```
test cases:     4238 |     4218 passed | 19 skipped | 1 failed as expected
assertions: 10343232 | 10343231 passed |  0 skipped | 1 failed as expected
BIN EXIT 0
```

- **The one expected failure** is the documented `[!shouldfail]` in `tests/unit/test_character_lab_slopes.cpp:187`
  (an invariant the engine is known not to satisfy). It is the known baseline.
- **[rift]:** all 5 cases pass (`./build/clean/tests/avgen_tests "[rift]"`: 75 assertions in 5 test cases). The
  2 known [rift] failures are gone because the scene now exists.
- **The 19 skips are explicit, and all are missing optional data or runtimes:**
  - the 100STYLE and scout motion packs and databases: 15;
  - `AVGEN_SAMPLE_ASSETS` not set: 2;
  - an ffmpeg-fallback path: 2.

## GPU suite

```
tools/gpu-lock.sh bash -c './build/clean/tests/avgen_render_tests > gpu-suite.log 2>&1; echo "BIN EXIT $?" >> gpu-suite.log'
```

Started 13:51:05, finished 14:28:25.

```
test cases:    657 |    656 passed | 1 skipped
assertions: 644982 | 644982 passed
BIN EXIT 0
```

- **The one skip:** `test_texture_share.cpp:243`, because the NDI runtime is not installed (Syphon is available).
- **ADR-tagged GPU cases in the build:** [adr1286] 6, [adr1289] 1, [adr1290] 2, [adr1281] 1, [adr1291] 2. All
  passed in the run above.

## Hand checks

### A broken project's `--render` exits non-zero and writes no frames (ADR-1288)

Two projects:
- `broken.json` names a scene file that does not exist;
- `garbled.json` names a scene file that is not JSON.

```
./build/clean/src/avgen --headless --project broken.json  --render frames-broken  --format png --range 1:1 --size 320x180
./build/clean/src/avgen --headless --project garbled.json --render frames-garbled --format png --range 1:1 --size 320x180
./build/clean/src/avgen --headless --project broken.json --frames 2
```

| Run | Exit code | Frames written |
|---|---|---|
| broken, `--render` | 1 | 0 |
| garbled, `--render` | 1 | 0 |
| broken, headless `--frames` | 1 | none expected |

Each logs `the project's scene did not load, so what would be drawn is not the project's world: <cause>`. Logs:
`~/Desktop/av-gen-review/45-bioluminescent-valley/validation/`.

### A ninth material program renders: the moon's phase shader (ADR-1290)

- In the generated vista scene, `moonPhase` is program 9 of 9, the slot the old 8-entry table dropped.
- Rendered `shots/vista.json` (1920x1080, t = 1 s): exit 0, no "not uploaded" warning.
- Control: the same scene with the moon's program unset (`val-vista-noprog`).

| Render | Moon pixels with luma > 150 | What it shows |
|---|---|---|
| with `moonPhase` (slot 9) | 4,367 | a phase-shaded crescent with a terminator |
| without it | 21,688 | a flat full disc |

So the ninth program is uploaded and drives the surface. Image:
`validation/moon-ninth-program-vs-none.png`.

## Not covered

- Glowmere scenes were not touched or regenerated (owner correction: this spec means BIOVALLEY).
- Performance was not measured (spec 09: Phase 7 recommends profiling next).
- ADR-1291 passed its own tests, but it is rejected on looks and must not be merged. The lead can take
  `4d98ec52` (the ADR-1290 test) on its own.
