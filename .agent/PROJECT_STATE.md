# Project State

**Updated:** 2026-09-19 14:30
**Branch:** `main` @ `598082c1`
**Build:** PASS (`cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release && cmake --build build/release`)
**Tests:** `~[gpu]` green at `ed5c39ec`: **2,459 cases**, 4 skipped, `1 failed as expected`, exit 0.
A run covering `598082c1` was in flight at the time of writing and should be re-confirmed.

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
- `agent/cosmicart` — **the owner's priority.** Tree of Life cosmic pass. Phase 0/1 merged (wind,
  ADR-360). In flight: the wind switched on with a calibrated default, `emissiveBoost` 0.5, a
  groundColor ladder, then falling leaves and the cosmic vortex. Holds a soft-particle ADR to be
  numbered **367**.
- `agent/imagelook` — the Image/Look framework (`docs/image-look-spec.md`). Section 60 byte-identity
  proved end-to-end. Holds an ADR to be numbered **368**.

**Paused by the owner:** the multi-backend offline render upgrade, after its audit landed
(`docs/offline-backend-audit.md`, 578 lines). Resume at step 5: extract `FrameRangeDriver` from
`RenderJob` -- it is GPU-free, testable, and path-traced *sequences* fall out of it.

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
- **The comet effect sometimes draws a hard straight line across the frame** (owner screenshot,
  2026-09-19). Under investigation. Two causes ruled out with evidence: the aurora's
  `h < 0.0 || h > 2.3` cut fires only where `body` is already zero, and the horizon `smoothstep` is
  genuinely applied rather than being a decorative early-out. Leading hypothesis: the coma is
  `(hr^2/(perp^2+hr^2))^2`, which **never reaches zero**, inside a hard rejection sphere of radius
  `... + halo.w*2`, so the wash is clipped along that sphere's silhouette -- which across a narrow
  field of view projects to very nearly a straight line. Note the effect named "Bioluminescent
  Comet" carries **both** a comet and a camera-anchored aurora, so either half could be at fault.
- **An app save destroys a baked camera.** Reaching for the viewport on an already-directed project
  calls `releaseDirectedCamera`, which drops the six `directedCameraTargets()` plus
  `cameraAimFollow` and `cameraShotSpans`; the next Save writes the loss. The serializer is fine --
  a headless save round-trips both tables. Re-baking re-photographs the hero anchors (ADR-344), so
  it cannot be undone casually.
- **Metal ray tracing has no hardware behind it on this machine.** `supportsRaytracing: 1` but
  `MTLGPUFamilyApple9: 0` -- the RT units arrive with M3 and this is an M2 Max, so Metal RT is
  `intersect()` on the shader cores. Whether it beats a 12-core Embree is unmeasured.
- **There is no CPU tone map.** AgX and the other four operators exist only in `tonemap.wgsl`, and
  `render_job.cpp` refuses a CPU copy deliberately, so **a path-traced frame cannot become a video
  frame without a GPU**.
- **`--render-in-app` and `--pathtrace` rewrite the project they render**, deliberately (the save is
  what makes a render reproducible). Pointing either at a checked-in example corrupts it, and
  `check_project_integrity.py` will pass on the result because it is still valid. Copy to scratch.

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
