# ADR-1814: A procedural's LOD chain is built once, and the first frame builds them in parallel

- Status: Accepted (eng/render-improvements, 2026-10-28). Answers the render audit's open question about Rainbow World's
  7 s first frame (REPORT section 5.3, "looks like translation that no cache holds" [H]), which ADR-1811 showed was not
  a shader cache matter.
- Code: `src/scene/procedural.cpp` (`LodChainMemo`, `memoisedLodChain`, used by `makeLodMesh`),
  `src/rendering/procedural_renderer.cpp` (`Impl::prefetchLodChains`, `Impl::lodMeshKey`).
- Test: `tests/unit/test_procedural_lock_border.cpp` (`[adr1814]`).

## Problem

Rainbow World's first frame took 7.1 s on every launch, warm caches or not (ADR-1811's table: 7.0-7.3 s in every row).
`sample` of the process during that frame put the main thread in
`ProceduralRenderer::update -> ensureLodMesh -> scene::makeLodMesh -> assets::buildLodChain`: meshoptimizer
simplification (`meshopt_simplifyEdge`, vertex-cache optimisation) building the LOD meshes of the scene's mesh-sourced
procedurals. Two things made it slow:

1. **Each level rebuilt the whole chain.** `buildLodChain` returns every level at once, but `makeLodMesh` was called once
   per level and kept one, so an object with three levels simplified its source three times. The sample showed three
   equal stacks, one per level.
2. **One object after another, on the main thread.** 101 chains, serially.

Nothing caches the result across launches, so it recurred on every one.

## Decision

- **A chain memo** inside `makeLodMesh`: the chains already built, keyed by the source mesh's bytes (vertices and
  indices) and the flags that shaped the settings (`lockBorder`, `thinning`, the sloppy fallback). The other settings
  are constants of the build. A level is served from the memo; a miss builds the chain as before. Bounded to 256 chains
  (least recently used goes), and thread-safe.
- **A parallel prefetch** at the top of `ProceduralRenderer::update`: every mesh-sourced procedural with `lodCount > 1`
  whose level 1 is not cached yet has its chain built on worker threads (up to the hardware's thread count), filling
  the memo; the frame's `ensureLodMesh` calls then find their levels there. It runs only when there are at least two to
  build, so a steady frame does nothing.
- The same chains, the same bytes: nothing about what a LOD level is changes, only how often and where it is computed.

## Consequences

- **Rainbow World's first frame** (`--live-profile --mode headless --quality ultra`, 1080p, warm caches, two runs each):

  | | First frame | Load |
  |---|---|---|
  | before (ADR-1811's runs) | 7.0-7.3 s | 5.6-7.1 s |
  | the memo alone | 3.3-3.8 s | 5.4-5.5 s |
  | memo + parallel prefetch | **1.63-1.69 s** | 5.5-5.6 s |

  The prefetch logs `101 LOD chain(s) built on 12 thread(s) in 330 ms`. The remaining 1.3 s of the first frame was not
  attributed further.
- **The picture is unchanged**: `tools/picture_check.py` (ADR-1810), Rainbow World's three frames before and after:
  SAME (byte-identical), as are Aurora's and Talking Heads'.
- The memo holds copies of up to 256 chains for the session (vegetation-sized meshes: a few MB).
- Other scenes with many mesh-sourced procedurals (Glowmere Valley 3's scatter, measured only) should gain the same
  way; not measured here.
