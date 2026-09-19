# Project State

**Updated:** 2026-09-19 06:20
**Branch:** `main` @ `2bcc5606`
**Build:** PASS (`cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release && cmake --build build/release`)
**Tests:** `~[gpu]` green at `2bcc5606`: **2,417 cases / 3,796,474 assertions**, 4 skipped,
`1 failed as expected`, exit 0.

> The single `FAILED:` in every run is `test_character_lab_slopes.cpp:187`, a `[!shouldfail]`
> control. Catch2 reports it as *failed as expected* and exits 0. **It is a pass.**

## Implemented
- **Realtime renderer** — WebGPU/Dawn on Metal. Clustered lights, shadows, GTAO, HDR/AgX post,
  procedural scatter with GPU cull, water (ADR-099), world effects (ADR-207).
- **Offline path tracer** (`src/pathtrace/`, ADR-351/352/353) — Embree, glTF metallic-roughness
  BSDF, MIS, Russian roulette, OIDN (prebuilt, off by default), multi-layer AOV EXR, `TraceJob`,
  `--pathtrace` CLI, Render-panel option. Renders one frame, not sequences.
- **Character AI** — perception, `decide` considerers (ADR-333), route pricing (ADR-336), root
  motion (ADR-337), foot IK on farm rigs (ADR-348-series).
- **Asset LOD** for imported meshes (ADR-348) — 41.8% GPU saving at hero camera.
- **Day/night** (`scene.environment.dayNight`, ADR-343) — one `dayPhase`, pure function of time.
- **Scenes** — Glowmere Valley 2 family, Tree of Life floating island + ocean world.

## In development (unmerged worktrees)
- `agent/cosmickey` — cinematic key light for the cosmic Tree of Life scene. 3 commits.
- `agent/slicetool` — cmd-click slice tool for the sequence lanes, obeying active snapping.

## Known regressions / unresolved
- ~~**Possible colour regression** at multicam t=48.5~~ **CLOSED 2026-09-19.** The owner checked the
  film and there is no regression. Never reproduced by anyone; it cost no work because nobody
  started. Do not re-open without a render pair.
- ~~**~5 fps** in both Tree of Life scenes~~ **FIXED** (ADR-355). Three call sites used the
  uncached `MeshData::bounds()` (a full vertex scan) where `Scene::meshBounds()` caches against
  `meshVersion`; the tree's 45 entities carry 39.9 M vertices, rescanned ~5x a frame. Editor frame
  **350.33 ms -> 8.64 ms**. LOD was never the cause and could not have been: the GPU was 24.6 ms of
  a 350 ms frame. LOD is separately enabled on the floating island at `maxScreenError: 16` (GPU
  13.89 -> 11.01 ms); **the ocean world still needs the same five keys.**
- Bokeh past the waterline reads as a hard cut in wide Glowmere shots (ADR-349). Diagnosed as
  depth-of-field, not water. Unfixed.
- Water reflections sample the IBL cube, so sky and water disagree at dawn/sunset.
- The analytic sky has no clouds (the stated trade of ADR-345).
- `--save-project` writes `heroes[].position` from the *last simulated frame* (ADR-344). Worked
  around in the film; not fixed in the engine.
- `nodes/ember/position` is 68 m from its scene position and overrides it.

## Constraints
- **Project `parameters` are applied over the scene** (ADR-264). A scene edit alone may do nothing.
- **Determinism**: scrub must equal play. Pose layers and `dayPhase` are pure functions of time —
  no accumulators (ADR-091, the `LfoSource` precedent).
- **`QualityTier::Offline` forces top LOD and zero hysteresis.** Offline must never inherit a
  realtime compromise.
- **Opt-in for anything that changes other scenes.** "A behaviour that changes under everyone is
  not a fix."
- Do not modify the 3.16 M-triangle Tree of Life source asset. LOD0 is the source.
- `assets/treeisle/*.glb` and `assets/environments/*.exr` are **gitignored**; regenerate with
  `tools/glowmere_tree_layers.py` / `tools/import_hdri.py`. A merge brings the manifest, not pixels.

## Performance notes
- Tree of Life floating island, live editor at the owner's canvas (3408x1786, 6.09 Mpx, scale 2.00):
  frame **8.64 ms** after ADR-355, from 350.33 ms. GPU 11.01 ms with LOD, 13.89 without.
- The editor logs its real canvas once at frame 60 (`application.cpp:4092`); the headless path now
  prints a CPU stage breakdown too, which is what made ADR-355 findable.
- Path tracer: Glowmere 640×360 @16 spp ≈ 15.6 s render, 385 ms BVH (contended machine).
- **No clean benchmarks exist.** `CrashPlanService` has been at 145% CPU throughout.
