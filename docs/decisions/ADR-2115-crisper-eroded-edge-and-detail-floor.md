# ADR-2115: a crisper eroded cloud edge (`erosionEdge`) and a floor on the detail share (`erosionDetailFloor`)

- Status: Accepted (2026-11-20), avagent/neon-dreams (Neon Dreams, iteration 9b; owner msg 105: the cloud wall behind
  the garden at 44.5 s "reading as too blobby").
- Code: `scene::Clouds::erosionEdge`, `erosionDetailFloor` (`src/scene/clouds.{hpp,cpp}`); `erosion3` in
  `CloudUniformGpu` and `shaders/clouds.wgsl` (`lobeGrid`).
- Tests: `tests/rendering/test_clouds_it9_gpu.cpp` (`[adr2115]`).

## Context

At 50 to 150 m the Neon Dreams wall read as smooth round balls with soft rims. Measured causes, in order of weight
(A/B stills at 44.5 s, work/engine/it9b/blob in the studio project):

1. The eroded edge (ADR-2106) is `lam x 0.07` on a crown to `lam x 0.28` under a base, with `lam` the coarse billow
   (60 m): 4 to 17 m of soft ramp on lobes 25 to 60 m in radius. With the cloud's low extinction (0.15/m) the ramp
   reads as a glow round a ball, not a surface.
2. The coarse billow (60 m) is larger than the lobes, so it dents a lobe once instead of breaking its silhouette.
3. The deck "seat" lobes (ADR-2107) take 0.3 of the erosion so the decks do not sink; at that share their skin is
   almost the bare ellipsoid. Some of the wall is seat lobes.
4. The half-resolution march and the temporal history were not the cause: a full-resolution march (`resolution` 1)
   changed the edges by less than the settings above.

## Decision

- `erosionEdge` (0.05..4, default 1 = as before) scales the eroded edge's width. It never goes under the pixel
  footprint / half-step floor that ADR-2106 already applies, so a narrow edge does not alias.
- `erosionDetailFloor` (0..1, default 0 = as before) is the least share of the mid and fine octaves and of the
  eroded edge any lobe takes; a lobe's own `erode` still scales the coarse octave. A held lobe keeps its body but
  not a smooth skin.
- With both at their defaults the shader computes exactly what it did (the coarse and detail terms were one product).

Neon Dreams takes `erosionScale` 60 -> 40 m and `erosionEdge` 0.5 (build.py `CLOUD_LOOK`). `erosionDetailFloor` is
left at 0: at 0.5 and 0.7 the seat cloud under the Quay pier sank and showed the stilts at 27 s (the ADR-2107 failure),
and at an edge of 0.35 the half-resolution march stippled the rims. The rest of the "blobby" read is the lobes'
shape, which the art's flatter bank of torn mats (82fbd24f) addresses.
