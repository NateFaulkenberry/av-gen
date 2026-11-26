# ADR-2242: an emitter hit is looked up, not searched for; instanced emitters take no light-sampler weight

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H1; audit defects D5, D7, D12 in part)
- Code: `pathtrace/lights.{hpp,cpp}` (`emissivePdf`, `lightEmission`, `sampleLight`), `pathtrace/snapshot.{hpp,cpp}`
  (`emissiveIndex`, `lightEmission`), `pathtrace/path_tracer.cpp`
- Tests: `tests/unit/test_pathtrace_mis.cpp`; the A/B run below

## Context

`emissivePdf` scanned every emissive triangle each time a BSDF ray hit an emitter, on the stated assumption that
such hits are rare. Neon Dreams has 4,383,692 emissive triangles (63% of the scene), so most hits are emitter hits.
A 5-second profile of a 960x540 trace put 82% of the CPU in the scan and 7% in `colorTemperatureToRgb`,
recomputed for every light at every sample.

Separately, an instanced hit's `meshIndex` indexes `Snapshot::instanced`, but it was looked up as an index into
`meshes`. It could take the MIS weight of an unrelated mesh triangle.

## Decision

1. `Snapshot::emissiveIndex` maps `(meshIndex << 32) | primIndex` to the triangle, built with the area CDF.
   `emissivePdf` is a lookup.
2. `emissivePdf` takes `instanced`. Instanced emitters are not in the light sampler's list, so their density is 0 and
   the BSDF strategy keeps them whole.
3. `Snapshot::lightEmission` holds each light's colour x temperature x intensity. `sampleLight` takes it.

## Evidence

The lookup was proven byte-identical before the scan was deleted. Neon Dreams, t = 20 s, 960x540, 8 spp, depth 3,
8 threads, with the instanced and procedural changes switched off:

- the EXR compared equal to the shipped build's with `cmp`;
- render time went from 159.9 s to 2.8 s;
- the load average was 16.8 and 8.1 respectively, so the speed-up is at least about 25x even allowing a 2x load
  confound.

The record is `115-parallel-engine/20-hybrid-rt/01-audit/logs/pt-diag-arms-t20.log`. The light-emission cache
returns the value the per-sample call returned, from the same function.
