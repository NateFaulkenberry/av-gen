# ADR-1995: Distant ground eases between rings, every ring shades its far sides alike, and contour edges ease

- Status: Accepted (Musical Geography CP-D r2, the owner's distant-geometry popping, 2026-11-10),
  scene/musical-geography. Replaces ADR-1976's seam band (its CP-A amendment); amends ADR-1970.
- Code:
  - the ring morph: `GeneratorSpec::morphWidth/morphRatio/morphPitch`, `generatorTileMorph`, `generatorTileMorphed`
    (`src/scene/generator.{hpp,cpp}`); `tileCellAt`, the morph block in `cs_generate_tiles` (`shaders/generator.wgsl`);
    the uniform (`GeneratorTileUniforms::morph`, `morphBand`, `morph2`, `src/rendering/procedural_renderer.*`); JSON
    `distribution.generator.morph` (`src/scene/procedural.cpp`).
  - the contour fade: `TileDynamics::contourFade` (JSON `tileDynamics.contour.fade`, parameter
    `env/tiles/contour/fade`), `tileContourStrength` and `tilePackedSides` (`src/scene/tile_terrain.*`), its twin in
    `cs_tile_contours`; the record's `position.w` carried to the material as `MaterialContext::tileSides`
    (`shaders/procedural.wgsl`, `pbr_shade.wgsl`, `material.wgsl`, `src/scene/material_program.*`), read by the
    `tileContour` op.
  - the far-tile rule's measure: `slabRuleScale` (`src/scene/procedural.*`), carried in `windTiming.z` to
    `slabVertex` (`shaders/procedural.wgsl`).
- Tests:
  - `[geography][tiles][adr1995]` in `tests/unit/test_tile_terrain.cpp`: the morph is 1 on every window's edge and
    the coarser cell there, 0 deep inside, and continuous as the camera slides across five window snaps; JSON
    round-trip and refusals (the old `blend` included); the contour strength is continuous as a level crosses a step;
    the packing; every ring measures the lattice pitch for the far-tile rule.
  - `[gpu][generator][geography][adr1995]` and the amended `[adr1976]`/`[adr1970]` cases in
    `tests/rendering/test_tile_terrain_gpu.cpp`: the kernel matches the CPU mirror tile for tile with the morph on,
    including the coarser-pitch case on the moving ground, the window's edge tiles stand at the coarser ring's cells,
    and the contour pass writes the CPU's side strengths.

## Context

The owner, on the CP-D r2 land clips: "looks like some potential issues with either shadow pop or an issue with haze at
distance, further geometry is popping". The terrain had just become much taller (heightScale 2.2, 45–65 m hills,
islands and headlands), so distant land fills far more of the frame.

Measured on art's land4 scene (1080p, 30 fps, the bay 30–50 s and the sweep 96–116 s), with a per-block "pop" score:
a 24 px block that changes in one frame by more than twice what it changes in the frames either side. Canaries, each
one knob changed:

| Canary | Bay 30–50 s | Sweep 96–116 s |
|---|---|---|
| As built | 3357 | 10644 |
| One 4 m ring out to 1080 m (no 8 m or 16 m ring) | **551**, the 31–33 s events gone | 10333 (unchanged) |
| Contours off | 3187 (unchanged) | **3155**, the 111–114 s events gone |
| Tile motion off | 3084 | 2581 |
| Shadow range 480 → 5000 m | 3208 | |
| AO off | 3361 | |
| Fog off | 3874 | |

Two causes, each proved by the knob that makes it vanish:

1. **Ring swaps (the bay).** The rings (4 m to 270 m, 8 m to 540 m, 16 m to 1080 m, then 64 m) are camera-centred
   windows snapped to whole blocks. Each snap moves a ring's edge 16–64 m at once, and a band of land changes tile size
   in one frame. On flat ground that hid; on 40 m island flanks a 2× block's mean steps against the fine tiles, so the
   whole band changes shape and its shadowed risers appear or vanish (32.5 s: the bay island's flank, 4 m ↔ 8 m).
   The scene had no seam band, and ADR-1976's band would not have helped: it counted cells from the window's snapped
   edge, so its weights jumped at every snap too.
2. **Contour edges switching (the sweep).** A tile side is a contour edge when a level lies inside the step to its
   neighbour. The moving ground (the land swell) and the drifting offset (one interval per phrase) carry levels across
   steps continuously, but the edge was on or off. Seen at a grazing angle on a far hill the band covers the whole
   visible sliver of a tile, so a whole tile flashed in one frame, scattered along every contour line (111–114 s).

Shadow range, AO fade and fog were ruled out: none changed the score.

## Decision

1. **The morph.** A ring may carry `morph: {width, ratio, pitch}`. Within `width` metres of the distance the ring is
   always drawn to, a tile eases toward the mean of the `ratio × ratio` block it belongs to:
   - the weight is a smoothstep of the Chebyshev distance from the camera to the tile centre, reaching 1 at
     (n − ½) cells, n = ceil(viewDistance × reach / cellSize). Every window this camera can snap to contains every tile
     closer than that, and every tile the coarser ring can border is beyond it. So the weight is a function of the
     camera, not the window, and the coarser ring takes over exactly where the fine tiles already stand at its cells;
   - `pitch` is the coarser ring's `finePitch`: each tile contributes its own samples at that pitch, so the block mean
     is the coarser ring's cell exactly (the 16 m ring's tiles toward the 64 m ring, sampled at 16 m). 0 = the ring's
     own pitch, which costs nothing extra;
   - the block is shared through the 8 × 8 workgroup, so a tile reads no other cell: `ratio` is 2, 4 or 8, and `align`
     a multiple of it.
   `blend` (the seam band) is gone and refused (ADR-441).
2. **The contour fade.** A flagged side has a strength: the level nearest the middle of the step lies a fraction q of
   the step from its nearer end, and the side is `smoothstep(0, fade, q)` strong. `fade` is
   `tileDynamics.contour.fade` (keyable, `env/tiles/contour/fade`), default 0.15; 0 is the binary edge. The contour
   pass writes each side's dimming in 63rds, six bits a side, into the record's `position.w` (unused by tiles; an
   integer below 2^24, exact in a float). It reaches the material as a flat varying, `MaterialContext::tileSides`, and
   the `tileContour` op draws each flagged side at its strength. No program changes.

3. **One far-tile rule for every ring.** ADR-1959's far-tile rule (the side normal and colour ease to the top's, the
   gap closes) measured each ring's own box on screen. At the swap a 4 m tile is half the pixels of the 8 m tile it
   replaces, so the same flank's risers changed shade in one frame even with the heights equal. With every ring in one
   flat colour and the morph on, the owner's moment still scored 188 → 38 at 33.17 s and 108–88 at 31.9 and 32.5 s;
   measuring the lattice's pitch (`finePitch`, or `cellSize` for the finest ring, over the box's width) took all four
   moments to 0.

## Measured (art's land4 scene, bay 30–50 s far band; the owner's moment is 33.17 s)

| | 31.2 | 31.87 | 32.5 | 33.17 | total |
|---|---|---|---|---|---|
| As built | 274 | 646 | 611 | 372 | 3357 |
| Morph + contour fade | 126 | 574 | 419 | 207 | 2318 |
| + the lattice far-tile rule | 11 | 127 | 170 | 97 | 1106 |
| The same, every ring one flat colour | 0 | 0 | 0 | 0 | 798 |

What remains with art's programs is the 1× and 2× ring programs differing (per-tile jitter, contour share, border);
that is the look's to converge across the morph band (`02-requests.md`). The new engine with the morph off and the fade
at 0 renders art's land4 bay bit-identical to the old engine (all 600 frames, max difference 0).

Sweep 96–116 s: 10644 → 4677 with the fade at 0.15 (0.35: 3516; contours off: 3155). A still camera with the world
moving (the camera parked on the sweep): 10001 → 1826.

Cost (live profile, 1080p ultra, 20 s, twice each, GPU median): generators pass 1.33–1.35 → 1.35–1.37 ms (+0.03 ms);
whole frame 16.46/16.75 → 16.48/16.57 ms (25–45 s) and 16.80/16.83 → 16.84/16.81 ms (96–116 s), within noise.

## Consequences

- The ring swaps are invisible in height and channels. What remains at a swap is the difference between the rings'
  own looks (materials, gaps, shadows): art's programs, not the ground.
- A contour edge now takes about a tenth of a second or more to come or go, instead of one frame. In a still frame,
  sides whose level sits near the end of a step are drawn weaker (with 0.15, about the 30% of single-level sides
  within 15% of an end are dimmed, by half on average: the mean contour strength falls by about 15%).
- `position.w` of a tile record is no longer 1 ("density"; nothing read it for tiles).
- The coarse rings' far risers flatten as the 4 m tiles' do (they used to keep their own normals a little longer).
