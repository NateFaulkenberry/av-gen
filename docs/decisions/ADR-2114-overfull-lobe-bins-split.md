# ADR-2114: an overfull cloud lobe bin is split into finer cells, not trimmed

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 9b).
- Code: `rendering::binCloudLobes`, `kMaxBinCellsPerSideSplit` (`src/rendering/cloud_renderer.{hpp,cpp}`).
- Tests: `tests/rendering/test_clouds_it9_gpu.cpp` (`[adr2114]`).

## Context

The lobe bins (ADR-2100) hold at most `kMaxLobesPerCell` (64) lobes per xz cell, and the grid is at most
`kMaxBinCellsPerSide` (128, ADR-2105) cells along its longer side. Past 64 the smallest lobes in that cell were
dropped, with a warning. In Neon Dreams iteration 9 the far set (841 lobes, 4.4 km across, 127 m cells) had one cell
under the Keep holding 68: the Keep's lobes ride a 12 m bob (ADR-2112), which widens every footprint, and four of its
crown lobes vanished there (`clouds: 1 of 13184 far lobe bins held more than 64 lobes`).

Raising the cap was the other option. The count has 8 bits (255), but the march reads every lobe of a cell at every
step, so a higher cap makes the dense cells the slowest; and the cells near the cap in the hero set (62) would be free
to grow.

## Decision

`binCloudLobes` bins at 128 cells a side; when any cell is overfull it rebuilds the grid at 256, then 512 cells a side,
and keeps the first grid where every cell fits. Only past 512 are the smallest lobes dropped (and warned about, as
before). A finer grid lists fewer lobes per cell, so a split never costs the march more per sample; it costs a larger
bin buffer (at 256: about 41,000 cells, under 1 MB with the lists).

Neon Dreams: the far set now bins at 256 a side (71 m cells), largest cell 47 lobes, none dropped. The hero set is
unchanged (128 a side, largest 62).
