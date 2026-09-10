# Glowmere Valley: Directed Candidate

Entry point: [terrain.json](../examples/world/terrain.json), with scene data in
[terrain.scene.json](../examples/world/terrain.scene.json).

This is a tested scene-development candidate, **not a finished cinematic or 60 FPS sign-off**.
The engineering changes below are implemented; visual and performance acceptance remain open.

The subsequent [painterly experiment](stylized-glowmere.md) preserves this project as a
comparison, adds an opt-in surface mode and records newer matched measurements. The timings
below predate the procedural alpha-depth correction and must not be compared directly with
that experiment's results.

## Run and render

From the repository root, after installing the asset library described in [assets.md](assets.md):

```sh
cmake --build --preset release -j 4
./build/release/src/avgen --project examples/world/terrain.json --play --tier realtime
./build/release/src/avgen --project examples/world/terrain.json \
  --render renders/glowmere-review.mp4 --range 0:90 --size 1280x720 --fps 30 --codec h264
```

Relative render paths resolve against the project directory. The latter writes
`examples/world/renders/glowmere-review.mp4`. The shot holds its final pose after 90 seconds;
it is deliberately not a looping flight. No soundtrack is bundled. Load audio or use live input
to exercise the two existing audio-analysis routes.

Reused assets include the Quaternius vegetation library and the existing Poly Haven fern,
grass, Calathea and Kloppenheim sky assets. No new runtime dependency or external download was
introduced. Their existing library provenance and licences remain applicable; see
[asset-library.md](asset-library.md).

## Direction

- A low approach establishes a broad luminous crown, a close pass reveals its underside,
  and the camera rises into the valley. Position and target have separate smooth tracks.
- The elder is built from existing sphere, spline-tube and radial distribution primitives.
  Its dark cap, curved stem and hanging filaments are independently authored objects.
- A dedicated moon rig, scene-owned HDR sky, low anisotropy mist and restrained bloom separate
  atmospheric lighting from material emission. Project-local fern and tree emission overrides
  avoid changing the look of other scenes that share those materials.
- Scanned ground vegetation, more grass and three proximity relationships create local groups:
  ferns near canopy, fungi near ferns, and pebbles near boulders.
- The crown uses a normal-dependent underside mask and noise-warped radial gills. Fungi use a
  separate tissue program; ground uses biome color and world-space roughness variation.
- Bass adds at most 0.16 to the crown's 2.8 emission intensity; treble adds at most 24 to the
  300/s spore rate for unit input. Attack/decay are 1.2/3 seconds and 0.6/2 seconds respectively.
  Audio does not drive the camera or key light.

## Engineering changes

The ecology proximity rule is deterministic, validated and evaluated at build time. Its hash
participates in dependent instance invalidation. Existing no-rule placement streams are preserved.
See [world.md](world.md#placement-relative-to-another-layer) for the authoring contract.

Multi-material procedural mesh nodes now have independent part multipliers, with repeated-frame
and unregister tests. This does **not** yet solve named-part control for terrain scatter layers.
See [procedural-geometry.md](procedural-geometry.md#material-parts) for scope and program caveats.

Project loading no longer discards a composition's HDR map when the project omits an environment
override. Explicit overrides still win. Reloading the same file-backed composition restores its
authored environment after a previous override. File-backed, inline, override and scene-switch
cases have regression tests.

## Validation

Commands used:

```sh
cmake --build --preset release -j 4
ctest --preset release --output-on-failure
cmake --build --preset asan --target avgen_tests -j 4
./build/asan/tests/avgen_tests '[ecology],[composition],[project],[glowmere]'
./build/release/tests/avgen_tests '[material][glowmere]'
```

The first full release run discovered 916 tests with no failures and four skips. After adding
the material behavior test, the final full run discovered 917: one intermittent Syphon burst
failure (`frame.has_value()` in `test_texture_share.cpp`) passed on isolated rerun. Four skips
were the two optional Khronos sample imports, external ffmpeg encoding, and NDI runtime test.
Do not describe the final full run as unconditionally green.

ASan/UBSan passed 53 selected ecology/composition/project/shot cases (23,707 assertions) before
the final material-only refinements. The new material test passed 3,331 assertions in both
release and ASan/UBSan builds.
The scene integration test samples all 90 seconds for ground clearance, bounded translation,
continuous view direction, bound automation/routes and valid material references.

The complete 1280x720 H.264 render wrote 2,700 frames with zero GPU errors and sequence hash
`4d96632a62612712`. Its 31.3 encoded frames/second is **offline throughput**, not interactive
frame latency. Opening, 30-second close and 82-second vista stills were inspected. Rendering
every frame verifies execution, not the absence of shimmer, popping or framing defects in motion.

## Measured performance

Hardware reported by the renderer: **Apple M2 Max**, not a base M2 MacBook. Release build, same
project, 180 frames at a fixed 30 Hz simulation clock, with 168 warmed frames measured. Three
interleaved repetitions per setting; no concurrent build or GPU test. These are development
candidate measurements before the last fill-light and material-only refinements.

| Tier and size | Per-run median wall ms | Median of medians | Per-run p90 ms |
|---|---|---|---|
| realtime 1440x900 | 43.70, 43.65, 39.86 | 43.65 | 53.15, 53.31, 46.85 |
| realtime 1280x720 | 49.43, 44.18, 41.44 | 44.18 | 62.35, 59.57, 52.64 |
| preview 1280x720 | 32.36, 38.38, 38.66 | 38.38 | 45.95, 46.27, 51.66 |

All nine runs reported zero GPU errors. The counterintuitive resolution ordering and spread
preclude a scaling claim. None establishes either a sustained 30 FPS budget or the requested
16.67 ms / 60 FPS budget. A separate 120-frame 1440x900 sample reported about 81,000 logical
instances and 1,765 camera-visible instances. Logical triangles are not post-LOD submitted work.

The earlier original-scene sample was 35.30 ms median at 1440x900, but it used a static camera,
fewer frames and the broken sky-loading path. It is not a controlled optimization baseline and
must not be used to claim a speedup. Prior invalid whole-process scatter timings remain invalid.

## Remaining acceptance work

- Bright terrain bands remain. Removing a tangent normal derived from biome UVs did not remove
  them; that hypothesis was falsified. The simpler ground material is retained, but the lighting
  or geometry cause is unresolved.
- Distant foliage is repetitive and stippled; the close hero still reads as procedural geometry.
  The ending needs a stronger visual destination. Temporal aliasing and LOD transitions need
  continuous playback review, not just stills or a successful encode.
- Audio routes bind and have bounded authored gains, but musical behavior with a real soundtrack
  has not been auditioned. No audio-driven visual acceptance claim is made.
- Independent terrain-layer material parts, automatic frame-budget control, occlusion culling,
  hero rigid-body physics and the proposed 1k-to-100k benchmark ladder are not added by this work.
- Base-M2 performance, sustained thermal behavior and final output-resolution timing remain
  unverified. Existing quality tiers remain manual, not an adaptive performance controller.

The next acceptance gate is a reviewed moving shot with resolved terrain/foliage artifacts and
repeated whole-shot interactive timing on the target hardware, followed by a clean full test run.