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

## Stage 3: water colour, lighting, material (done)

- Everything unlit, painted by programs (Alpine's pattern). `Prog`, a small assembler in the generator, numbers ops by
  label so routes can address `material/<prog>/op/<n>` by name. Found: an op's `value` defaults to 1, so a `remap`
  without one CLAMPS -- my first sky had no vertical gradient because e = -v.y was clamped to [-1, 0]. The assembler now
  writes `value: 0` for every remap unless told otherwise.
- **Sea program** (`doSea`, 46 ops, the near piece): micro-ripples (a noise carried by the rest position tilts the
  normal, faded out by 200 m), the reflected sky by the reflected ray's elevation, the sun on the water by the reflected
  ray's angle to the sun (a broad warm path + crisp glints, two smoothsteps), a Fresnel mix with the water's body colour,
  and light through the backlit crests (crest height x (toward the sun)^3). The reflected ray is never built: r.y/2+1/2
  and r.l/2 are each a product plus a gradient, which saved the ops that foam needs.
- **Far sea program** (`doSeaFar`, mid and far pieces): their rows undersample the shorter trains, which drew crest
  light and normals as dashes that changed every frame (seen in 8-frame horizon crops). The normal leans to vertical and
  the crest light fades between 200 and 700 m: the distant sea becomes smooth colour and the glitter path, as a painter
  would treat it. Crops after: coherent.
- **Sky dome** (`doSky`, 31 ops): horizon warm toward the sun and cool away, a violet middle, the zenith; a broad glow,
  a halo and a crisp disc (1.7 deg, smoothstep-edged); below the horizon the far water's colour.
- **Palettes compared on the real scene** (`stage3-palettes-4.png`): golden hour, electric twilight, bioluminescent
  midnight, surreal tropical. **Chosen: electric twilight** (indigo water, cyan crest light, violet-to-coral sky, a
  cream sun with a pink halo): the strongest value structure and the most distinct from Alpine's rose and violet.
  Golden hour and midnight stay as the related variations for the performance (Stage 8).
- Banded vs continuous (`stage3-smooth-vs-bands.png`): quantized reflection (3-4 bands) reads as a cel illustration
  but loses the sheen and its hard edges would crawl at grazing angles; continuous kept (`BANDS` knob, 0).
- Cost check (headless 1080p, scale 0.85): GPU median 14.0 ms with the first two programs; noted, measured again later.
- Media: `stage3-twilight.png`, `stage3-golden.png`, `stage3-palettes-4.png`, `stage3-smooth-vs-bands.png`,
  `stage3-motion.mp4`.

## Stage 4: sky, clouds, atmosphere (done)

- **Clouds**: one procedural node, a `points` distribution of 196 squashed sphere puffs in 26 banks round the WHOLE
  horizon (3.2-5.8 km), three decks of twilight stratus (low and long, middle, high and thin). A world-space `twist`
  about the camera's vertical axis (amount 0, speed 0.0016 rad/s, ~65 min a turn) drifts the field: seamless, running
  with no transport, and the view never empties because the banks go all the way round.
- **Cloud program** (`doCloud`, 25 ops): a dusky violet body; undersides lit coral in a pool round the sun (smoothstep on
  the true angle to the sun, 2-19 deg); a bright rim on grazing edges near the sun; distance haze toward the sky's
  middle colour. Two wrong turns recorded: (1) seen from below every visible face IS an underside, and every cloud in
  frame is within 25 deg of the sun, so a (1+cos)/2 falloff lit them all peach -- found by rendering the registers;
  (2) hazing toward the horizon colour turned clouds at 3-10 deg peach too; they now haze toward the sky's middle.
  Cumulus puffs read as clip-art circles and were dropped.
- **Island**: a long low faceted mesa group (meshes.rock, stretched) at 4.3 km on the left third, a flat hazy violet
  silhouette: it balances the sun on the right third and gives the horizon scale. A needle stack read as a shark fin
  and was removed.
- Media: `stage4-sky.png`, `stage4-sky-drift-4s-120s-300s.png` (the drift over five minutes).

## Stage 5: foam and secondary detail (done)

- **Foam caps** (near program, 3 ops): a sharp threshold on the crest-height ramp, so only the tops of the highest
  crests -- where the three trains reinforce -- carry mint foam; seen edge-on from 12 m they read as thin ribbons that
  come and go as the trains drift in and out of phase. Tried first and rejected: the `curvatureMask` op (screen-space
  curvature) gave a broad mint wash over every crest, not a ribbon.
- **Micro-ripple tilt** folded into one add along (1, 1, 1) to free ops (the near program is exactly 48 ops); its
  amplitude dropped to 0.055 because the tilt now also moves n.y and made the near glints too large.
- **Spray**: deliberately not scattered over the sea. Spray belongs to events -- dolphin splashes, the hero wave, the
  drums -- and is built with them (Stages 6-7).
- Media: `stage5-foam-caps.png` (two thresholds x two moments; right column chosen).

## Stage 6: signature -- the dolphins (done)

- **Mesh** (`dolphin_mesh`, 240 faceted triangles): a lofted bottlenose (melon, beak, tapering peduncle), a swept dorsal
  fin, pectorals and flukes; 3.1 m. Its first render was flat coral: the loft's winding was inverted, so every normal
  pointed inward and the rim term flooded the body. Rewound, and the generator now ASSERTS every body facet faces out.
- **Motion with no engine change**: each dolphin is a procedural node with a local `sine` (the tail's kick, growing from
  the nose to the flukes) and a `path` deformer that bends the body along a scene spline; `deform/2/pathOffset` is where
  its tail is. The spline is the engine's Catmull-Rom with rotation-minimising frames from `up`, so the back stays up
  over the arc; `catmull()` in the generator reproduces it to place splashes.
- **Ambient pods** (`PODS`): pod A, three dolphins crossing the sun's path at ~88 m left to right, every 31 s, staggered
  0.55 and 1.25 s; pod B, two smaller and far (~150 m) near the island, right to left every 47 s (31 and 47 never align,
  so the rhythm of leaps never repeats exactly). Each rides a CLOSED loop: an arc out of the deep, ~2.3 m over the mean
  sea, back down, and a long return far under the opaque sea; the loop is solved to period x 8.5 m/s. An LFO **saw** per
  pod drives `pathOffset` = loop length x saw: pure in time, no transport, and seamless at the wrap (the loop is closed).
- **The hero leap** (`HERO`): an open path at 56 m with a 4.2 m arc over the glitter, driven by an **envelope** source
  (`env.leap`): its linear attack (~4 s) carries the hero from deep on the left to deep on the right; its 1 ms release
  sends it back to the start, unseen under the sea. A retrigger mid-leap continues from where it is (no reversal);
  nothing accumulates. Fired by a pad in Stage 8 (`LEAP_TRIGGER`); here by a scored timeline event for review.
- **Splashes**: a particle system at every surface crossing (exit and entry, per dolphin), its `spawnRate` opened by two
  binary routes from the same saw/envelope for exactly the moment the body passes (nose to tail + 0.15 s); the drops'
  `collision: splash` leaves rings on the water. Pools are fixed (320/600), so nothing can grow.
- Placement iterated: the hero first breached at the bottom edge of the frame; moved out to 56 m so the arc silhouettes
  against the sun's path. Pod A moved from 52 to 88 m so the two never compete.
- Media: `stage6-dolphins.mp4` (pod at 1-4 s, hero fired at 5 s), `stage6-hero-sequence.png`,
  `stage6-pod-sequence.png`.
