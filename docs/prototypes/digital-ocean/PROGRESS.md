# Digital Ocean: progress (art agent)

Brief: `00-brief.md` (the owner's, verbatim). Plan: `01-plan.md`. Worktree `../av-gen-ocean`, branch
`art/digital-ocean`. Generator: `tools/sonic_vfx/scenes/digital_ocean.py`, built by
`python3 tools/sonic_vfx/abstract.py build digital_ocean` into `examples/sonic-abstract/digital-ocean.json` and
`.scene.json` (+ meshes `examples/sonic-abstract/meshes/do-*.glb`). Review media:
`~/Desktop/av-gen-review/50-digital-ocean/` (look-development frames in `work/`).

## Stage 0: reconnaissance and plan (done)

- Read the brief; inspected Digital Alpine's generator, kit, meshes, review/capture tools, the live MIDI projects
  (Astral Forge Live, Digital Mosh Live, THE RIFT LIVE), the procedural deformer stack (`procedural.hpp`,
  `procedural.wgsl`), the material program interpreter (`material.wgsl`), fields (incl. ADR-1116 audio fields and
  ADR-906 triggered fields), splines and the path deformer, particles, LFO/envelope/timeline sources and the route
  chain (`processor.hpp`).
- Confirmed Digital Alpine regenerates byte-identically from its generator.
- Wrote `01-plan.md`: the concept (the Tidewriter), the palette shortlist, three compositions, techniques, reusable
  facilities, risks and checkpoints. No engine change is anticipated.
