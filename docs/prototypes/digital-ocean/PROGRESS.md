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

## Stage 1: composition and graybox (done)

- Graybox: a camera-centred fan of sea (a generated GLB), the engine's procedural sky with a flat peach horizon, a sun
  disc 4 km out, a lit flat material, two plain sine trains.
- Compositions tried (`stage1-compositions.png`: A top-left, B top-right, C bottom-left, C2 bottom-right):
  A low and cinematic (3.2 m, 30 mm), B elevated (32 m, 26 mm), C graphic (9 m, 55 mm), then C2/C3/B2 between.
- **Chosen: C2**, eye 12 m, 45 mm, pitch -3.6 deg: the horizon on the upper ~40 %, the low sun (4.6 deg, 12 deg right of
  the view) on the right third, its reflection a column down the frame, the swell rows reading as graphic bands. A
  drowned in near water with no pattern; B showed the pattern but put the sun high and small; C2 keeps both.
- Decision: the camera stays composed and nearly still (the brief's preference); any drift comes later and subtly.

## Stage 2: water silhouette and primary motion (done)

- **Sea mesh** (`ocean_rings`, `ocean_wedge`): three pieces (near 0.6-200 m, mid 200-1200 m, far 1.2-6 km; 73k
  vertices in all) because the vertex stage's finite-difference normal step is 1e-3 of a source's bounds. Rings 0.5 m
  apart near the camera, then `6e-5 r^2` apart (a constant ~1.5 px row at 1080p for this lens and height), so the far
  water does not turn into sub-pixel slivers.
- **Gerstner trains from sine deformers** (`gerstner()`): a vertical sine plus a horizontal one along the travel a
  quarter-cycle later. Verified by rendering both signs: the horizontal amplitude must be NEGATIVE along the travel
  (`-Q/k`) for the points to gather at the crests (positive sharpens the troughs). Steepness is the classic Q*A*k.
- **The swell set**: a long swell from under the sun (72 m, 1.15 m, Q 0.5, travelling toward the viewer), a cross
  swell from the right (37 m, 0.42 m, Q 0.35, 31 deg), a wind sea from the left (15.5 m, 0.16 m, Q 0.25, -38 deg);
  deep-water dispersion scaled by 0.62 (a dream's pace). Three directions and periods never realign, so the surface
  evolves rather than bobbing.
- Media: `stage2-swell-still.png`, `stage2-swell-motion.mp4` (12 s), `stage2-swell-motion-sheet.png` (1 fps).
- Seen and deferred to the material stage: the lit graybox shows GTAO speckle in the near troughs and specular
  dashes at the horizon; both belong to the lit PBR path the scene will not use.
