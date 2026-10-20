# ADR-1560: avgen keeps its own Metal shader cache, and a cold compile can be reproduced and timed

**Status:** Accepted, 2026-10-20 · **Range:** 1560-1569 (eng/perf-reliability)

## Context

On 2026-10-19 the owner's first launch of `avgen` after the day's merges sat on a blank window for 4 min 45 s,
blocked in `ProceduralRenderer::Impl::createPipelines` waiting on `MTLCompilerService`. The second launch took 1.8 s.
Investigating that needed three things the engine did not have:

- **A way to make a launch cold on purpose.** `avgen` is an unbundled executable, so Metal kept its compiled shaders in
  the cache every unbundled tool on the machine shares (`$(getconf DARWIN_USER_CACHE_DIR)com.apple.metal`). On this
  machine that cache was 3.1 GB, nearly all of it test-binary shader variants. Clearing it would make every tool cold.
  Salting the WGSL does not work either. A salt in dead code is stripped by Metal's front end, and the back end's cache
  is keyed after that, so the launch is still warm. A salt in live code changes the compiler's inlining decisions: one
  extra branch in the material interpreter took the procedural pipeline from 126 s to 7 s, so that launch measured a
  different program.
- **Per-pipeline times.** Nothing recorded how long each pipeline took to create.
- **The MSL Dawn hands Metal**, to see what was being compiled.

## Decision

1. **`avgen` has a bundle identifier** (`com.avgen.avgen`, from `cmake/avgen-Info.plist` linked into `__TEXT,__info_plist`).
   Metal then keeps avgen's compiled shaders in `$(getconf DARWIN_USER_CACHE_DIR)com.avgen.avgen/`, apart from the
   tests and every other tool.
   - **A reproducible cold launch** is `rm -rf "$(getconf DARWIN_USER_CACHE_DIR)com.avgen.avgen"` followed by the launch.
     The only thing it touches is avgen's own cache.
   - It also stops a large test run from evicting avgen's entries from the shared cache. The suites compile thousands
     of shader variants into that cache, so a big test run could otherwise turn the owner's next launch cold.
2. **`AVGEN_PIPELINE_TIMING=1`** logs every pipeline's creation time (`pipeline-timing render|compute '<label>' <ms>`)
   from `gpu::createRenderPipeline` and `gpu::createComputePipeline`, through which every pipeline is created. Async
   creations are logged when they land.
3. **`AVGEN_DAWN_TOGGLES=a,b`** enables Dawn device toggles, for investigation only. With `dump_shaders` and
   `--log debug`, it dumps each pipeline's generated MSL.

Unset, the two variables change nothing.

## Consequences

- The measurement recipe is
  `rm -rf "$(getconf DARWIN_USER_CACHE_DIR)com.avgen.avgen"; AVGEN_PIPELINE_TIMING=1 build/release/src/avgen --headless --frames 1`.
  At 416d48f1, on a quiet M2 Max, this measured 305 s cold:
  - procedural-opaque 126 s;
  - procedural-depth 88 s;
  - sdf-raymarch 15 s;
  - the rest 1-6 s each, created serially.

  A warm launch measured 1.7 s.
- The first launch of a build with this change compiles every shader once into the new cache.
- The test binaries are not bundled. They keep using the shared cache.
