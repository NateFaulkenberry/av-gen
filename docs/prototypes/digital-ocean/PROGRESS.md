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

## Stage 7: audio-reactive behaviour (done; one engine change, ADR-1370)

**The map** (`instrument()`, `audio_fields()`): every response has its own scale and decay; nothing flashes the frame.

| music | signal | the sea's response |
|---|---|---|
| bass | `tide`: a `spectrum` field (32-75 Hz) x a planar sine carrier, field deformer | TIDEWRITING: the bass's last ~15 s laid out as swell rolling in from 400 m at 26 m/s |
| kick | `kickFront`: an `onset` field (low band), compound with the hero wave; `response.kickEnv` | a lifted front rolls in from 70 m; crest light +55 % and foam x2.2 flash (420/600 ms) |
| level | `response.level` | the long swell +0.5 m (1.8 s / 4.5 s); the crest ramp scales with it so foam stays a cap |
| intensity | `response.intensity` | STRUCTURE: the long swell's crests sharpen (Q 0.42 -> 0.52) |
| mids | `audio.mid` | the wind sea roughens |
| snare | `response.snare` | a burst of spray off the near swell in the sun's path |
| hats / highs | `response.hatEnv`, `audio.treble` | the glints flare and brighten |
| sustain | `response.sustain` | the sun's halo and glow swell, its path on the water warms |
| brightness | `sonic.brightness.slow` | the grade drifts a little warmer or cooler |
| MIDI notes | `notes.noteOn`, `lastPitch`, `lastVelocity` | sparks on the sun's path, distance by pitch, count by velocity |

**Found and fixed, in order (each by an A/B render, not assumed):**
1. Kick fronts changed nothing (pixel diff 0.0). For `waveGeometry: planar`, `waveDistance` is `dot(q, axis)` in the
   field's own frame and IGNORES `point` (ADR-1116 says "about point"): every distance on the sea was negative, the
   fronts lived behind the camera, and the tide's delay clamped to "now" (it still moved the sea, so its first A/B
   passed while carrying no history). The fields are now placed with their node `position`. Recorded in ADR-1370 as a
   finding for the owner; not changed (it would move existing planar fields).
2. Under Trench the middle distance became crumpled foil. Ruled out in turn: the highs widening the micro-ripples
   (removed anyway: it aliased), the Gerstner steepness sum (it was 1.1, which can fold the surface: now 0.85, +0.1 at
   full intensity, kept under 1). The tide off -> clean (`stage7-crumple-found-tide-off-vs-on.png`). Cause: the history
   ring has ~94 rows a second, so at 26 m/s one analysis hop covers 0.28 m of sea; the bass's hop-to-hop jitter became
   spatial noise under a 1.5 m swell.
3. **ADR-1370** (engine): a spectrum field's `audioSmooth` window reads the mean of its band round its delay. The first
   version used eight fixed taps; over 0.9 s they were ten rows apart and ALIASED the jitter -- Glowmere showed it as
   moire rings on the sea mesh's own concentric rows. Now a tap per history row (2..32): a true box filter. The tide
   uses 0.17 s (16 rows, ~4.5 m of sea) over a narrower 7-bin sub-bass band. CPU reference and GPU in step; unit test
   (alternating rows -> 0.5) and the CPU/GPU parity harness carry a smoothed field. `avgen_tests [audio-fields]` exit
   0 (82 assertions); `avgen_render_tests [audio-fields]` exit 0 (1008 assertions).
4. The snare spray was a scatter of specks over a 34 x 22 m box (the particle noise the brief warns about): now a gust
   from 7 x 2.5 m on the near swell in the sun's path.

Media: `stage7-silent-trench-allyougot-glowmere.png` (same scene, four inputs), `stage7-crumple-found-tide-off-vs-on.png`,
`stage7-after-adr1370.png` (Glowmere x2, Trench), `stage7-trench-60-90s-before-taps-fix.mp4`.

## Stage 8: MIDI as a performance instrument (done)

**The map** (`MIDI_CC`, `MIDI_PADS`, `midi()`; any device, any channel; the project's `control.midi.bindings`):

| control | signal | gesture |
|---|---|---|
| CC 1 | `control.night` | NIGHT: the palette's other end -- navy sea, emerald bioluminescent crests and foam, a pale moon |
| CC 2 | `control.swell` | SWELL: long swell +1.1 m, cross swell +0.5 m (the crest ramp follows, so foam stays a cap) |
| CC 3 | `control.chop` | CHOP: the wind sea roughens, the glints multiply |
| CC 4 | `control.foam` | FOAM: the foam threshold drops, foam brightens |
| CC 5 | `control.sun` | SUN: halo, glow and the sun's path x2.5 |
| CC 6 | `control.tide` | TIDE: the bass-history swell from x0.4 to x2 |
| CC 7 | `control.golden` | GOLDEN: toward golden hour |
| CC 8 | `control.spray` | SPRAY: drum spray and note sparks up to x3.5 |
| pad 36 | `control.padLeap` | LEAP: the hero dolphin's envelope (a full ~4 s arc across the sun's path, splashes at exit and entry) |
| pad 37 | `control.padWave` | HERO WAVE: a triggered pulse front (ADR-906) rises 240 m out and rolls in at 13 m/s, foaming |
| pad 38 | `control.padGlow` | BLOOM: emerald light along every crest and in the foam, held 0.7 s, fading over ~3 s |
| pad 39 | `control.padFlare` | FLARE: halo x4, glow, path and glints blaze and settle over ~2 s |
| pad 40 | `control.padSpray` | SPRAY: a gust of 420 drops off the near swell |

- **Palettes as a time of day**: every palette colour a program paints with is recorded by the assembler
  (`Prog.pcolour`), and `palette_routes()` adds (target - twilight) per channel at full control: one control moves the
  whole world coherently (sky, sea, clouds, dolphins, island). CC 1 and CC 7 are alternatives (both up adds both).
- **Gestures, not switches**: pads carry their own envelopes (peak-hold + fall), the leap is a complete arc, the hero
  wave a complete crossing; repeated hits restart a gesture, never accumulate (no growing state anywhere: fixed pools,
  stateless fields, an envelope source).
- **Wave speed and direction are deliberately NOT on a control**: a deformer's angle is `k.x + speed t`, so changing
  `speed` (or the axis) mid-performance jumps every wave; an integrated phase would run into the phase parameter's
  +-100 rad bound within minutes. Swell, chop, tide and the hero wave cover the expressive range instead.
- **Fixed in this stage**: foam turned into flat mint slabs when CC 2 raised the sea (the crest ramp tracked only the
  loudness) and when the hero wave arrived (mixed foam erased the crest's form). The crest ramp now follows CC 2 too,
  foam is ADDED over the crest's shading (ops freed by replacing the micro-ripples' hand-made distance fade with the
  engine's anti-aliased `microDetail`), and the hero wave is 1.9 m (2.6 saturated the crest light).
- **Offline review**: `REVIEW_MIDI` (generator knob) replaces each control signal, wherever it is read, by a scored
  timeline source; `stage8-scored-midi-offline.mp4` plays the probe's script with no audio.
- **Live, end to end**: `avgen_sonic_probe controller` (a scenario added to the probe tool: CC ramps and pads 36-40 over
  a held synth note into BlackHole) as a CoreMIDI virtual source, the editor in `--live` mode with the project. 370 of
  the probe's messages and 8 notes arrived; every gesture appears in the capture (`stage8-live-midi-end-to-end.png`,
  `stage8-live-midi-capture.mp4`). Two harness lessons: this machine's saved MIDI device (`SE49 MIDI1`) overrides the
  project's `*` filter, so the first run received nothing; and with a name filter a source must exist BEFORE the editor
  opens (it is never retried), so the probe starts first with a 25 s lead-in.

## Stage 9: animation polish (done)

- **Rarity of the signature**: the pods leap every 53 s (pod A, three dolphins) and 79 s (pod B, two): incommensurate,
  so the pattern of leaps does not repeat for over an hour; the hero leap stays the performer's. 31/47 s was too often
  for a set piece.
- **The camera breathes**: a seamless 0.35 m rise over 26 s (`camera/position` component 1) and a 0.7 deg pan over 71 s
  (`camera/target` component 0): two periods never in step, each track first key = last key and loopLength = span (an
  endless live transport, ADR-1342). The composition never moves enough to read as camera motion.
- **Rhythms in the world** (none shares a period): swell trains 10.9 / 7.8 / 5.1 s, tide arrives over ~15 s, kick fronts
  over ~3 s, pods 53 / 79 s, clouds ~65 min a turn, camera 26 / 71 s.
- Silence check: `stage9-silent-60s.mp4` and its 4 s sheet: the sea never repeats, a pod crosses, the frame stays calm.

## Stage 10: visual critique and iteration (done, three rounds)

Seven conditions, one frame each, same scene (`stage10-critique-round{1,2,3}.png`): (1) no audio, (2) quiet (Glowmere
intro), (3) bass-heavy (Trench, 77 s), (4) dense percussion (Feline Footwear, 95 s), (5) sustained (Night Shift, 40 s),
(6) MIDI (hero wave + leap + night at 0.6, scored), (7) peak (Trench + swell, sun and chop full + a flare).

**Round 1, the three most significant weaknesses:**
1. *The nearest crest became a flat glowing slab* (2, 4, 5): crest light and foam saturated on the closest swell, the
   brightest shape in the frame, competing with the sun and showing no form.
2. *The peak blew out* (7): sustain x the sun control x the flare multiplied the halo ~24x; the sky washed to pink
   haze and the sun disc nearly vanished -- the "excessive glow" the brief warns against.
3. *Clouds read as cut-paper lenses*: identical flat discs with bright outlines (the rim term at 3x drew a hard edge).

**Fixes:** (1) a near fade -- first on the crest light only (round 2 showed the slab was mostly additive foam, so it
persisted), then on the crest-height ramp itself, before both the crest light and the foam use it (round 3: the
nearest crest is a lit edge over shaded water); (2) the glow gains rebalanced (sustain halo 1.4 -> 0.7, sun control 1.5
-> 0.9, flare halo 3.0 -> 1.6) so the three add up to a bright sun, not a white sky (round 2: the disc holds at peak);
(3) the cloud rim 3.0 -> 1.1 and more, smaller, more varied puffs per bank (352 instead of 196) with flatter bases.

**What remains, judged honestly:** the clouds are better but still lean toward stacked lenses -- the weakest element
next to the sea; a sculptural cloud technique (ADR-1289's lobes, or authored meshes) would be the next step and was not
taken for cost. Under the hottest settings the crest light is still a dominant cyan; it is the scene's accent and is
kept, but it is the first thing to tame if a palette variation is added.

Earlier critique-driven fixes (Stages 3-8) are in their own sections: the far-field dashes, the foil (ADR-1370), the
moire, the spray specks, the foam slabs at full swell and on the hero wave, a hero wave that saturated at 2.6 m.

## Stage 11: performance and final validation (done)

Measured with `tools/live_scene_profile.py` (headless, 1080p and 2160p, M2 Max) under `tools/gpu-lock.sh`; reports in
`~/Desktop/av-gen-review/50-digital-ocean/stage11-profiles/` (`prof-*` baseline, `prof2-*` final, `elim-*`).

| case | GPU median / p95 | frame median / p95 | CPU work |
|---|---|---|---|
| 1080p silence Ultra, baseline | 21.6 / 22.2 | 24.0 / 28.7 | 1.2 |
| 1080p Trench Ultra, baseline | 27.1 / 27.7 | 32.7 / 35.4 | 4.7 |
| 1080p silence Ultra, final | 22.4 / 22.8 | 25.2 / 30.0 | 1.6 |
| 1080p silence auto, final (settles at Ultra) | 22.4 / 22.9 | 24.8 / 29.3 | 1.3 |
| 1080p Trench Ultra, final | 26.1 / 26.5 | 28.3 / 34.4 | 1.7 |
| 2160p silence Ultra, final | 61.6 / 62.1 | 64.2 / 68.8 | 1.4 |

Live target 30 fps (33.3 ms, ADR-1341/1360): met at 1080p with headroom in every measured case; 4K is over and the live
ladder's render scale takes it (fragment-bound: 4K costs 2.7x 1080p). The final silence numbers include the round-3
crest work (+3 ops) and the hemisphere sky; the Trench case gained ~1 ms from the cheaper tide (13 taps x 6 bins).

**Feature costs by elimination** (1080p silence, against 22.4 ms): sky dome 3.1 ms, clouds 1.6, the near sea's detail
0.85, dolphins and all particles 0.4; the sea (meshes, ten deformers, programs) ~16 ms. The hemisphere-sky hypothesis
(sky program shaded under the sea) was rejected by measurement: no change. The engine's `noprograms` diagnostic lever
cannot be used with `--compare` ("no form the project can keep"), so the programs' total was not isolated directly.

**Validation:** after merging origin/main (`b498a314`; the coordinator's index-preserving `write_index`, ADR-1344/1350/
1360; one conflict in the ADR table, resolved in order), regenerating the scene (unchanged) and
`check_project_integrity.py` (0 problems):
- `tools/ci/run-suite.sh cpu-assets`: **exit 0**, 4231 passed, 0 failed, 19 skipped, 1 expected failure, 4251/4251 ran.
- `python3 tools/review_examples.py --only "Digital Ocean"`: **ok** (Live > Sonic Abstract, Ready, exit 0, mean 103.6,
  spread 44.8; `stage11-review-examples-frame.png`).
- `tools/ci/run-suite.sh gpu --bin build/release/tests`: **exit 0**, 657 passed, 0 failed, 1 skipped, 658/658 ran.
- Live, end to end: Stage 8 (the editor in `--live`, the probe as a MIDI controller, every gesture in the capture).

# Pass 2: an illustrated ocean world (brief `04-brief-art-pass-2.md`)

Worktree `../av-gen-ocean2`, branch `art/digital-ocean-2` from main `f3851d75`. Media:
`~/Desktop/av-gen-review/54-digital-ocean-2/` (`before-pass1.png` is the merged Pass 1 scene).

## Pass A: research and style frames (done)

- Direction chosen: **graphic travel poster x anime background**. Bold silhouettes and flat colour regions for
  land, boats and creatures (poster/woodblock); painted gradients, luminous rays and lit cloud banks for the sky
  (anime backgrounds); the water stays the animated surface, its crests and glints simplified into colour bands.
- **Palettes**, each designed as a whole (every key -- sky, water, crest light, foam, clouds, land, leaf, trunk, hull,
  sail, rays -- is set per palette, never derived): **violet sunset** (primary: cobalt water, magenta-lavender crest
  light, peach rays, pink-orange clouds, dark violet land, an amber sail), **golden afternoon** (turquoise water,
  cream foam, coral sun, soft blue sky, green island, a vermilion sail), **electric blue** (ultramarine and cyan,
  mint surf, saturated blue, yellow hull and coral sail, navy land), and the **midnight** night end kept from Pass 1.
- **Compositions** (`passA-studies.png`, in order: Pass 1, E1, E2, E3, E1 afternoon, E1 electric): E1 (18 m) cut the
  boat at the frame edge and the island crowded the sun; E2 (26 m) flattened the swell; **E3 (14 m, 32 mm)** keeps the
  boat whole in the lower-left third, the island behind it, the sun on the right third.

## Pass B: composition and silhouette (done)

- **The island** (`island_height`, a polar heightfield): twin volcanic summits, a ridged flank, a beach shelf toward
  the camera; 980 m out on the left third. **Far chain**: three hazy islets on the horizon. **Palms** (11, a curved
  trunk + nine drooping blades, two meshes for two colours) on the shelf and low slopes, scaled 1.5-2.5x so they read.
- **The hero sailboat** (`boat_meshes`): a lofted hull with sheer and raked bow, mast, boom, a bellied mainsail and
  a jib; 11 m, 58 m from the camera. It RIDES the water: its deformer stack is the sea's own Gerstner stack (world
  space), so it rises and travels with the surface under it, plus a slow two-axis local roll.
- **Dolphins** twice as long (6.4 m), leaps 4-7.5 m high, at 78-300 m; the leap path now has an approach just under
  the surface before the rise (the brief's preparation beat).
- One illustrated program (`doIllus`) paints land, trees and boat: flat colour per node (palette-routable), a broad
  light band and shadow band, a warm rim toward the sun, distance haze.
- Media: `passB-composition-before-after.png` (Pass 1; violet at 2 s and 6 s; afternoon).

## Pass C: illustration treatment (done)

- **Clouds rebuilt** as illustrated formations (`cumulus`): a flat base row of wide squashed puffs, rows stacked above
  it narrowing to a rounded crown (anime cumulus), or long wind-stretched ribbons low on the horizon. Two layers at
  ~3.6 km and ~6.5 km drift round the viewer at different rates (parallax), with a slow world-space noise that makes
  the puffs swell and settle. Lit in two BANDS, not a gradient: undersides facing the low sun glow (`clit`), tops and
  far sides fall into one flat shadow tone (`cshade`), a warm rim on the edges near the sun.
- **The island** gets its own painted program (`doLand`): vegetation on gentle slopes, rock on steep faces, sand on
  the shore, and a sun side / shadow side split by one band, a warm rim and haze.
- **The water's reflection is quantized into three colour regions** (`BANDS = 3`, which Pass 1 rejected for a
  realistic sea): flat woodblock-like patches over the swell. It reads far more illustrated.
- Seen and carried to later passes: the island's sand and rock regions barely register; the dolphin is still small;
  one cloud row near the sun reads as a string of equal balls; the rays are hard white wedges.
- Media: `passC-illustration-palettes-and-bands.png` (violet, afternoon, electric, violet with banded water).

## Pass D: atmospheric effects (done)

- **Sun rays**, painted into the sky program (so clouds and land occlude them): a noise of the view ray's direction
  around the sun (its component across the sun's axis, normalized -- `power -0.5` stands in for the reciprocal no op
  offers), thresholded into tapered wedges that fade with angle. Two slow LFO sines (0.0137 and 0.0089 Hz, pure in time)
  drift the noise's offset on two axes: rays lengthen, part and merge, never spin. Softer edges, lower gain and a
  steeper falloff than the first trial (`passD-rays-drift-2-10-20-29s.png`, `passD-30s.mp4`).
- Bow wave and stern wake particles on the boat (superseded by the boat step: the wake read as a round blob).
- Island regions retuned (rock on the steeper flanks, wider sand band) -- superseded by the island step.

## Island step (the owner's brief `08-brief-island-simplification.md`) (done)

- The mound is gone. The island is now an extruded silhouette, not terrain: a long rounded-rectangle footprint
  (640 x 150 m, 900 m out on the left third) whose top line `island_top` is almost flat with a few soft bumps, one dip
  and a long low tail; a shallow rise from the water; 26 m at its highest. It reads as a low island as a flat
  single-colour silhouette. ~3 % of the image height for the land, ~7 % at the tree crowns; sky ~44 %, ocean ~56 %.
- The trees use one vocabulary: windswept palms (a curved trunk, six broad tapered fronds, a silhouette rather than
  leaves) and broad low canopies (three overlapping flattened masses on a short trunk), with gaps between them.
- About three land colours (`doLand`): the main colour on faces that look up, a darker underside on the steep faces,
  and in variants A/B a sun-facing accent; B and C add a narrow sand line at the water. No outlines and no foam ring.
- Stable: no deformer on the island and nothing routed to its shape. Only the palms sway, 0.2 m at the crown over 9 s;
  the canopies hold still. Palettes still recolour it, and the far chain is now two faint low strips.
- Variants from the same camera, sea, sun and clouds at 1080p (`54-digital-ocean-2/island/`): **A** minimal (4 trees,
  three tones), **B** tropical anime (7 trees, sand line), **C** cut-paper (5 trees, hard two-tone land, trees one
  dark tone). Round 1 placed the strip at 1.45 km and it vanished into the haze (`island-before-A-B-C.png`); round 2
  brought it to 900 m (`island-before-A-B-C-round2.png`).
- **Chosen: C.** It has the minimal geometry of A, the cleanest graphic read, and a canopy anchoring each end, so the
  strip is balanced (A leaves the left half bare). B's extra trees add clutter, not identity. C is also the least
  shaded variant, which matches the travel-poster direction chosen in Pass A.

## Boats step (the owner's brief `09-brief-boats.md`) (done)

- **Reference direction** (real hull profiles, no brands or logos): a modern 15-17 m express cruiser or sport motor
  yacht. Long and low; a fine, steeply raked stem; flared bow sections over a hard chine; a sheer that steps down
  aft; a wide transom over a swim platform. The superstructure is low, with a raked windscreen under a hardtop and a
  long dark wraparound glazing band rather than portholes, plus a narrow hull-window strip amidships.
- **Built from real proportions first** (`loft`, `box_loft`, `deck_surface`): hull stations are given as deck
  half-breadth, sheer height, chine and keel, and lofted. The cabin is lofted the same way, and its upper side band is
  the glazing. Three meshes take three colours: pearl hull, pale deck and cabin, navy glazing.
- **Candidates**, same camera and scene, at 1080p (`54-digital-ocean-2/boats/`):
  - A: the 16 m motor yacht.
  - B: a 10 m deep-V performance runabout with a long foredeck and a low wraparound windshield.
  - C: a 13 m modern sloop with a plumb bow, low coachroof, 18 m mast, and a high-aspect cambered main and jib.

  At 130 m all three were too small to judge (`boats-before-A-B-C.png`), so the course moved in to ~70 m
  (`boats-before-A-B-C-70m.png`).
- **Chosen: A.** Silhouette first: it reads at once as a modern motor yacht. The glazing band and hardtop make a clear
  graphic shape, and the hull is long and low with a credible bow. B is too low and small to read at live
  resolution. C's flat sails pull the read back toward the toy it replaces. One excellent vessel, as the brief asks.
- **Motion and water contact.** The yacht cruises at 3.2 m/s across the view on a straight course 64-84 m out. Its
  `pathOffset` comes from an LFO saw, which is pure in time; the course runs well past both frame edges, so the wrap
  happens out of sight (~75 s a crossing). It then goes through the sea's own world-space Gerstner stack, so it
  rises, falls and travels with the swell; a 6 cm roll is the only local motion.
- **The wake** is a directional trail from the stern that follows the same saw and widens behind the course. A small
  bow wave is thrown forward and outward. There is no ring. It was toned down after the first motion check, where it
  read as a jet trail (`yacht-A-crossing-26-44s.mp4`, `boatA-motion-crops.png`). Pass 3 revisits it to match the new
  water.
- Palette: pearl hull, cooler pale deck (first trial was too peach), navy glazing, each set per palette.

### Boats step, revision (the coordinator's notes)

- **The hull no longer reads as a box.** Seen abeam at ~70 m, the visible hull is the band between the waterline and
  the sheer, so the plan taper never showed. Changes:
  - The stem now rakes forward with height: forward of 11 m, points move ahead up to 0.75 m per metre of height, so
    the bow rises and points.
  - The sheer sweeps from 2.7 m at the bow down to 1.05 m at the stern.
  - The hull narrows from amidships in plan.
  - A dark **boot band** runs from the waterline to 0.42 m (its own mesh part, painted in the glazing's navy), so the
    hull sits in the water.
- **The wake is geometry, not particles**:
  - `wake_mesh` builds a Kelvin wake in the boat's frame: two arms diverging from the stern at 19.5 deg, 46 m long
    and widening; a short churned strip straight back; and two short bow-wave arms from the stem.
  - The wake rides the same course (path deformer) and the same sea and music swell as the hull.
  - Its foam (`doWake`) is a **world-anchored** noise, so foam stays where the boat left it while the arms move on.
    It is cut by a threshold that rises with age along the arm and toward its edges, and written as the opacity of an
    alpha-MASKED material.
  - The result is crisp, broken graphic foam with two diverging arms, and no smoke or ring.
  - No engine change: program opacity already feeds the mask discard.
- The hull now also carries the audio swell deformers (tide, kick fronts, hero wave). Without them it would sink or
  hover when the music raised the sea.
- `boats-A-B-C-crops.png` was deleted: its crop windows missed the boats.
- The first masked wake rendered as solid peach strips. A program-made cutout kept its full depth in the procedural
  depth prepass, so the sea behind it failed the depth test and the cleared horizon colour showed through. Fixed in
  the engine as **ADR-1371**: `fs_proc_depth` evaluates a masked material's program. The GPU test fails without the
  fix. With the fix, the first thresholds cut nearly all the foam; they are retuned (0.36-0.8).
  `yacht-v3-hull-1080p.png` shows the revised hull: raked rising bow, sweeping sheer, boot band.

## Pass E: musical colour and wave modulation (done)

- **One colour axis for the whole world** (`musical_colour`): `visual.mood` (an interpret mapping) = CC 1 + 0.35 x
  `response.intensity` (12 s) + 0.75 x the daybreak envelope, clamped to 0..1. It drives two segments of
  coordinated palette routes, violet sunset -> golden afternoon (0 to 0.5) and golden afternoon -> electric blue
  (0.5 to 1). Every palette colour of every program and every illustrated node moves together, smoothed over 1.5 s.
  A build therefore brightens the world toward day and a breakdown sinks it back to the sunset: a change of light,
  not a colour cycle.
- **Each band has its own role, time constant and target**, never all at once:
  - bass deepens the water's body and lifts the crest light (200 / 1200 ms);
  - highs add mint to the crest light (60 / 600 ms);
  - sustain turns the sky's middle pink and warms the clouds' lit undersides (2.5 / 5 s);
  - level strengthens the rays (0.8 / 2.5 s);
  - kick flashes the rays warm (0 / 350 ms);
  - onsets open the rays' reach for a moment (900 ms);
  - Pass 1's tidewriting swell, kick fronts and the level/intensity swell remain.
- Found by rendering: under loud music the sky washed out to milky lavender (ray, halo and sustain boosts multiplied);
  the level-driven ray gain and the sustain pink were halved.
- Media: `passE-F-colour-and-conditions.png` (round 1), `passE-F-round2.png` (CC 1 at 0 / 0.5 / 1, daybreak, Trench,
  Feline Footwear).

## Pass F: MIDI performance (done)

- CC 1 palette (the mood axis), CC 2 night (the bioluminescent end), CC 3 waves (swell height; the crest ramp follows
  so foam stays a cap), CC 4 wave energy (chop, foam and the tide's depth together), CC 5 sun rays (strength, reach,
  halo, the sun's path), CC 6 clouds (the glow on lit undersides and rims), CC 7 water colour (richness of the body and
  crest light), CC 8 spray.
- Pads 36 leap, 37 hero wave, 38 bloom, 39 flare, 40 spray, and **41 daybreak**: an envelope (3 s attack, 5 s hold,
  4 s release) added into the mood, so the whole world sweeps toward electric day and back to the sunset. Verified on
  a clip from 0 s (`passF-daybreak-pad-0-14s.png`, 2 s apart). A still that starts after the pad cannot show it: a seek
  does not replay envelopes.
- Review tooling: the offline MIDI scorer now also rewrites interpret-mapping inputs, and the kit lists a scene's own
  sources before its interpret sources, so a mapping can read an envelope. Every other Sonic Abstract scene is
  unchanged, checked by regenerating three and comparing bytes.
- Wave speed and direction are still not on controls: a deformer's phase is `k.x + speed t`, so changing either
  jumps every wave. This belongs to Pass 3's water work.

## Pass G: visual critique (done, two rounds)

Seven conditions at 1080p (`passG-round1.png`): silence, quiet (Glowmere), bass (Trench 77 s), percussive (Feline
Footwear 95 s), MIDI (palette 1, waves 0.8, clouds 0.7), peak (Trench + waves, rays and energy full), and night. Plus
a clip from 0 s with a scored leap and hero wave (`passG-events-leap-herowave.png`).

**Round 1: the three most significant (non-water) defects**

1. The dolphin leap was still a speck, about 2 % of the frame: not an identifiable event.
2. In the day palettes the rays washed the sky to pinkish white: near-white ray colours added onto a bright sky.
3. The cloud formation beside the sun read as a bunch of grapes.

**Fixes**

- Dolphins are 8.5 m. The hero leap is at 55 m with an 8 m arc; pod A is at 100 m with 5.5 m arcs.
- Ray colours are tinted per palette (warm in the afternoon, cool in electric blue), and the rays control is capped
  at x2.
- The grape formation became a long lit ribbon.

**Round 2** (`passG-round2.png`, `passG-leap-round2.png`)

- The leap reads as the brief's full gesture: the fin cutting the surface on the approach, the rise, a silhouetted arc
  across the glitter, the apex, the dive and a splash.
- The electric sky keeps its blue under the rays.

**Left for Pass 3 (the water) and noted**

- Under heavy bass and at the peak, the near swell's crest light and foam merge into a white slab.
- The hero wave reads as a thin dark line rather than a rising, foaming swell.
- At intermediate mood positions under loud music the sky can drift pale lavender (two palettes half-blended plus
  rays).
