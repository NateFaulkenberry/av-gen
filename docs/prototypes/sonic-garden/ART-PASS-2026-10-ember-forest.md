# Art pass 2026-10: Ember Forest

The art agent, 2026-10-15, branch `art/sonic-vfx-pass`. The owner's note: "feels incomplete, do the same" (find the
concept, establish a stronger direction, do an art pass).

## The concept found

- `SCENE-CATALOG.md` §13: "The morning after the fire: the bass is the wind breathing on the embers, the snare a
  flare-up in the black trunks, the melody one ember dancing through the smoke." It specifies the following:
  - black trunk silhouettes in receding planes, smoke layers, ground glowing in seams;
  - the horizon's fire glow behind the last trunk plane;
  - a sideways camera track;
  - the palette: soot black, smoke grey-brown, ash white, ember orange to yellow-white.
- `ART-RESTART-ANALYSIS.md` §12 found the following:
  - the focal point is unclear (the dancing ember is lost);
  - the trunks are "identical branchless cylinders. No fallen log, stump or root";
  - the ground's glowing squiggles "read as neon worms";
  - the colour is "orange haze", one material and one colour;
  - the instinct worth keeping is the receding planes of trunks.

The before stills confirm all of it: everything is the same orange, under a flat bright band of "fire".

## Research

Wildfire-aftermath night photography (fire-crew reports and night footage of burned slopes, such as Jasper 2024),
and the physics of smouldering:
- **What still burns is local.** After a front passes, large fuels (stumps, logs, roots) keep burning under their
  surface for hours or days. At night the glow "comes into focus" as points and patches. A burned slope at night is
  pocked with fire like a city seen from the air.
- **What has burned is cold.** Snags are black and the ash is grey. The smoke is lit by the moon and the sky, and is
  warm only near flame.
- **The front**, seen from the burned side, is a broken line of flame crawling along a ridge, backlighting the smoke.
- **Burning logs** show char checking: an alligator pattern of plates, with the glow in the cracks between them.
- **Coal beds** are glowing chunks of differing heat, with dark gaps, under patchy ash.

## The art direction

**"The night after the burn."** A dead pine forest still smouldering under a cold sky, while the fire front crawls
along the far ridge.
- **Two temperatures.**
  - Cold: a blue-black night sky with faint stars, a moon from above-left, slate smoke, grey ash.
  - Hot: points and lines of fire.
  - Nothing is uniformly orange: warm light comes only from what is burning, and from the front behind the trees,
    which throws the trunks' shadows toward the camera.
- **Real dead trees.** Quaternius dead pines (CC0) in three receding planes, and four snags close to the lens that
  frame the shot. The near snags' bark is charred, and some still glow in cracks low on the trunk.
- **Smouldering wood.** A fallen log and stumps (Polyhaven scans) burning from inside, with alligator-checked char and
  glowing cracks.
- **Coal beds, not lines.** Ragged beds of glowing chunks on the ash, each chunk its own heat, with dark gaps between
  them.
- **The front** on the far ridge: a broken, flickering line of flame along the crest.
- **Embers on the wind and ash falling.** Embers drift downwind; grey flakes fall through the cold light.

## What changed

All of it in `tools/sonic_vfx/scenes/ember_forest.py`.

- **Trees.**
  - The cylinders are gone. In their place are `dead_tree` meshes (Quaternius variants 1–5): planes at 22, 48 and
    78 m, plus four placed snags.
  - The far planes are unlit silhouettes, which costs less.
  - Only the near snags carry the bark program, which now chooses which trunks burn with a low-frequency noise.
- **Wood.** A new `efWood` program (Voronoi char plates, glowing gaps, a heat noise) on `log_big`, `log`, `stump` and
  `stump_b`. Each is mesh-budgeted to 12k triangles; the 1k scans are 40–100k.
- **Ground.** The squiggles are gone. `efAsh` now makes:
  - coal beds: one Voronoi cell in five, with a soft centre;
  - chunks inside the beds: a finer Voronoi, each chunk's brightness its hash cubed, with dark gaps;
  - ragged edges from a noise.
- **The front.** A dark ridge mass, and the front on its crest. Both take the same world-space noise deformer, so
  the flame sits on the ridge. A new `efFront` program breaks the line into burning runs and flickers them.
- **Light.**
  - The fire light is lower and casts no fog light: its forward-scatter blob in the 10-step smoke was a noisy smear.
  - The moon (cold, from above-left) is the fog's light.
  - The point light for the coals is gone: it pooled the ground in a flat orange disc.
- **Smoke.** Cold slate, thinner and shorter (95 m, 10 steps).
- **Particles.**
  - Embers on the wind; ash flakes (alpha, lit cold).
  - Kick sparks from the near snag.
  - Snare flames up the torch tree.
  - Hat glints off the coal.
  - Note embers with trails.
  - The "one dancing ember" is gone: the restart analysis found it lost, and the melody now places light along the
    ground instead.
- **Two bugs found in the process** (both mine, both fixed before commit):
  - A `voronoiEdge` result used as a mask must be swizzled to one channel first. Remapped as a vec4, it coloured the
    emission per channel, and the coals came out pink with blue specks.
  - A red-heavy, dim emission (`#ff5a00` at about 1) tonemaps toward pink under cold fog. The coal colour is
    `#ffa040` at 2.6–4.

## The mappings (`sonicScene.vocabulary`)

| signal | what the world does |
|---|---|
| `response.note` / `notes.lastPitch` | each note lifts an ember from the ground at the pitch's place, left (low) to right (high), rising on the heat with a trail |
| `notes.lastVelocity` | how many embers |
| `response.bass` | the wind breathes on the coals: the beds, the log and the stumps glow up, embers stream off downwind, and the smoke stirs |
| `response.kick` | the near snag cracks: a throw of sparks from its base, and every glowing crack flashes |
| `response.snare` | a tree torches: flame bursts up a midground trunk, and its light floods the smoke round it |
| `response.hat` | glints off the coal |
| `response.sustain` | the fire front on the ridge grows, and its glow climbs the sky |
| silence | a still, smoking forest under a cold sky; the coals breathe faintly; ash falls |

## Frame time

GPU at 1080p, realtime tier, Trench from 60 s, 600 frames, under `tools/gpu-lock.sh`.

| | p50 | p95 | costliest passes |
|---|---|---|---|
| before | 21.6 ms | 21.9 ms | scene 11.7, volume 7.6 |
| after | 26.3 ms | 26.7 ms | scene 17.2, volume 4.4, shadow 2.3 |

The scene is inside the 30 fps live target (33.3 ms). It was never inside 60 fps. Cost by ablation on the way:
- the first build was 44.6 ms;
- the dead trees' lit shading was the largest share (about 7 ms for 80 trees). That was fixed by fewer trees, unlit
  far planes, and no bark program on the planes;
- the ground program costs about 5 ms;
- the smoke went from 8 to 4.4 ms.

What is left to spend, if 60 fps is ever wanted for it, is the fire's shadow (2.3 ms) and the ground program.

## Media

`~/Desktop/av-gen-review/49-sonic-vfx-art/ember-forest/`:
- `before/`: stills with Trench at 12, 62 and 112 s;
- `after/`: stills with Trench at 12, 40, 62 and 112 s, and with the full test mix and MIDI at 4, 9 and 14 s;
- `ember-forest-trench-50-80s.mp4` and `ember-forest-full-midi-0-18s.mp4` (1080p30, with audio).

## Open

- The torch flare (snare) is a modest flash at this distance. A larger flame card or a closer torch tree would make
  it read harder.
- The assets are gitignored (CC0, restorable). Without them the scene loads without its trees, logs and stumps, which
  is the kit's documented behaviour.

## Suites (end of the four-scene pass, on 2fb8cfa5)

- `tools/ci/run-suite.sh cpu-assets` (under `tools/gpu-lock.sh`): **PASS**, exit 0. 4227 passed, 0 failed, 19
  skipped, 1 expected failure; 4247 of 4247 ran, in 19 min 25 s.
- `tools/ci/run-suite.sh gpu --bin build/release/tests` (the engine and shader change of ADR-1350): **PASS**, exit 0.
  655 passed, 0 failed, 1 skipped; 656 of 656 ran, in 30 min 54 s.
- `python3 tools/check_project_integrity.py`: exit 0.
