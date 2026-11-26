# ADR-2245: multi-layer EXR channels are written in OpenEXR's sorted order

- Status: Accepted (2026-11-26), eng/hybrid-rt (Hybrid RT H1; audit defect D13)
- Code: `assets/exr.cpp` (`writeExrLayers`)
- Tests: `tests/unit/test_pathtrace_environment.cpp` (`[adr2245]`)

## Context

`writeExrLayers` wrote the channel list in the order given (`R,G,B,albedo.*,normal.*,emission.*,depth.Z,id.X`), and
tinyexr lays the data out in that order. OpenEXR keeps a header's channels in a sorted map and reads the data in
sorted order. A sorted reader therefore puts each channel's data under another channel's name. This repo's own
`tools/read_exr.py` read the tracer's normals as infinities and its depth as +-0.008. The plain beauty writer
(`writeExr`, tinyexr's `SaveEXR`) was already sorted.

## Decision

Sort the channels byte-wise by name before building the header (strcmp, as OpenEXR does). The data moves with its
name. The only caller is the path tracer's AOV writer; the raster's outputs do not use this function.
