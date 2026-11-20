# ADR-2105: An opaque cloud hides what is behind it; ambient height over the hero lobes; finer lobe bins; a cheaper glow shadow

- Status: Accepted (2026-11-19), avagent/neon-dreams (Neon Dreams, iteration 6)
- Extends: ADR-2100 (Cloud Environment v2), ADR-2101 (bins), ADR-2104 (glow shape, far lobe set).
- Code: `shaders/clouds.wgsl` (`fs_clouds` early-out, `densityBase` height, the glow shadow tap),
  `src/rendering/cloud_renderer.{hpp,cpp}` (`kMaxBinCellsPerSide`, `binCloudLobes`).
- Tests: `tests/rendering/test_clouds_v2_gpu.cpp` (`[adr2105]`: an opaque cloud in front of a sky 2000x white shows
  none of it; a 3.4 km set of small lobes is binned at span / 128).

## Context

Neon Dreams iteration 5: (a) the Condensery's lights and the moon showed through the whole Veil. The march stopped at
1 % transmittance and composited the 1 % left, so an emitter a hundred times brighter than the cloud came through any
thickness (found by the art agent; the new test measures 254.8 of 255 through 1.9 km of cloud before, 0 after).
(b) The ambient gradient ran over the renderer's box, which also holds the far lobe set (ADR-2104): with towers to
1,600 m every hero formation sat in the box's bottom tenth and took the bottom colour only (no lighter crown).
(c) 64 bin cells along a 3.4 km hero set made 52 m cells; the art dropped 343 small lobes to stay under 64 a cell.
(d) The ADR-2104 glow shadow tap evaluated the full density (sea relief, noise) once more per lit sample: ~25 ms a frame
in the Veil at 1080p.

## Decision

1. Past the cut-off (`trans < 0.01`) the march sets the transmittance to 0: the cloud is opaque from there.
2. The lobes' ambient height is `remap01(y, heroSpan.x, heroSpan.y)`: the hero lobes' own vertical range (with no far
   set this is the box's range, as before).
3. Bins: at most `kMaxBinCellsPerSide` = 128 cells along the longer side (was 64); the rest of the rule is unchanged.
4. The glow shadow tap reads the lobes only (`lobeBase`), not the sea or the noise.

## Consequences

- Any scene: a pixel behind an opaque cloud loses its last 1 % (invisible except for emitters far brighter than the
  cloud). A scene with a far lobe set gets its ambient crown back. Finer bins change nothing in the density, only
  which cells a lobe is listed in (fewer truncations).
- The glow shadow no longer darkens a glow behind sea relief; the glows it serves sit among lobes.
- Measured (Neon Dreams, 1080p offline, steady state): see the iteration-6 report in the project.
