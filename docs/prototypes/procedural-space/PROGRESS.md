# Procedural Space POC: progress

Resume from here. Branch `proto/procedural-space` in `../av-gen-space`. The ADR block is 1000-1019;
1000 to 1003 are used.

## Rules in force

- All GPU work goes through `tools/gpu-lock.sh`, **including `avgen_tests`**. The art agent shares
  the GPU.
- Build: `cmake --preset release && cmake --build --preset release`. Reconfigure after adding a test
  file.
- A shader or scene edit while a suite is running mixes two versions into one run; don't do it.

## Status (2026-09-30, about 01:10)

| phase | state | notes |
|---|---|---|
| 1 architecture | done | `ARCHITECTURE.md`, ADR-1000: the POC is hosted on the existing ADR-027 SDF path as composition `sdf` nodes |
| 2 research | done | `RESEARCH.md`: A (the existing ray marcher) plus E, which comes free; F (a compiled SDF) was the fallback, and measurement made it necessary (ADR-1003) |
| 3-6 foundation | done and verified on the GPU | ADR-1001 (morph, fold, recurse, names, `count`/`axis`), ADR-1002 (look, march cap, step statistics, prepass and shadow switches), ADR-1003 (compiled trees), `examples/space` |
| 4 instrumentation | done, measured | see "Performance" |
| 7, 8, 11 look, presets, evaluation | the art agent's | see "For the art agent" |

## What exists (engine)

- **New SDF kinds** (`src/spatial/sdf.*`, `shaders/sdf.wgsl`):
  - `morph`: structural states on one float, evaluating at most two states.
  - `fold`: a plane reflection.
  - `recurse`: up to 8 levels of fold, rotate, scale and offset, unioned. This gives nested
    architecture.
- **Named nodes**: `sdf/<object>/node/<name>/<field>`. `count` (int) and `axis` are now parameters.
- **Limits**: depth 12 and 96 nodes (were 8 and 64). The 8-entry interpreter stacks are unchanged.
- **Compiled trees** (ADR-1003, `compile: true` on an SDF object): straight-line, kind-specialised
  WGSL. Node values stay live, and only a structural change (kind, child, `enabled`) recompiles, at
  about 3 s the first time a structure is seen. It is about 15x faster than the interpreter on the
  example. The interpreter remains the default and the reference.
- **March and look parameters on every SDF object**: `march/{maxSteps, epsilon, stepScale,
  maxDistance}` and `look/{ao, edge, shadow}/...`. Each look term is off at 0. The edge term is the
  angle between the surface normal and a normal taken at `edgeWidth`.
- **`depthPrepass` and `castShadows`** (both on by default) skip the second march in those passes.
- **Step statistics**: sampled on every 4th pixel in x and y. `SdfStats` carries `avgSteps`,
  `maxSteps`, `hitRatio` and `exhaustedRatio`. They appear in the Render stats lines and in the
  headless benchmark's "sdf march (median ...)" line.
- **The lit pass and the depth prepass share one march**, and the lit depth is written 4 ulps
  nearer. An earlier version had separate loops, and about a third of the surface failed the depth
  test by an ulp, showing as speckle.
- **Tests**:
  - `tests/unit/test_sdf_space.cpp`: tree against packed parity, Lipschitz bounds, names, parameters,
    JSON, and the compiler's source, table and key.
  - `tests/unit/test_space_example.cpp`: the example loads with audio, every route and preset target
    resolves, the states move the morph, the eye is in open space in every state, and two engines
    pack the same program.
  - `tests/rendering/test_sdf_gpu.cpp`: GPU parity for morph, fold and recurse, and "compiled trees
    render like the interpreter".
- **Suites**:
  - `avgen_tests`: binary exit 0; 3,876 passed, 19 skipped, 1 failed as expected (the slope lean).
  - `avgen_render_tests "[sdf]"`: 10 of 11 pass. The failure is the pre-existing hidden probe "A
    raymarched SDF does not reach the shadow map", which fails until that defect is fixed.
  - Full `avgen_render_tests`: see "Next steps".

## The example: `examples/space/space.json` (also in the Examples menu under Lab)

- **Scene** (`space.scene.json`): one raymarched, compiled `sdf` node named `space`, 54 nodes. The root
  chain is `twist` > `bend` > `fold` (disabled) > `morph "state"`. The morph's children are:

  | amount | state | structure |
  |---|---|---|
  | 0 | `hall` | a corridor along +x: floor, ceiling, mirrored walls with repeated doorways, mirrored repeated columns, repeated half-torus ribs |
  | 1 | `rotunda` | polar-repeated inner and outer colonnades, stacked rings |
  | 2 | `cathedral` | `recurse "nest"`, a chamber of columns, crown and slab nested inside itself |
  | 3 | `lattice` | 3D-repeated beams: mathematical space |

- **Look**: a dark blue-violet material, cyan edge emission, SDF AO, and surface distance fog
  (`volumeDensity` 0.018, with the volumetric march off). The light rig is `space.rig.json`: a cool
  high key (no shadow map), a violet rim, and a faint green under-fill.
- **Routes** (rules, not bounce):

  | source | target | effect |
  |---|---|---|
  | bass | `wallPlane/offset`, `ring/radius`, `nest/scale` | widens |
  | lowMid | `bend/amount`, `twist/amount` | bends and twists |
  | mid | `columns/size`, `ribs/size`, `lattice/size` | tightens repetition |
  | highMid | `nest/count` | adds a recursion level; an int, so it snaps |
  | treble | `look/edge/intensity` | lights the edges |
  | energy | `look/edge/width` | widens the edges |
  | `random.symmetry` (a new value per `music.bar`, seeded) | the radial column counts | changes the symmetry |
  | `lfo.forward` (saw) | camera x | forward travel |

- **Scene states**: Hall → Rotunda → Cathedral → Lattice → Hall. Each moves on a new section, or every
  16 bars (quantised to the bar, with a 4 s smooth morph). A `music.drop` jumps to Lattice. This was
  verified in the 60 s render: hall, then rotunda, then lattice on a drop, then a transition back.
- **Presets**:
  - `state/hall|rotunda|cathedral|lattice`: the morph amount only.
  - `camera/still`: the stationary observer, the default.
  - `camera/forward`: the forward LFO on.
  - `camera/orbit`: camera mode 0.
- **Review media** (`~/Desktop/av-gen-review/22-procedural-space/`):
  - `space-foundation-night-shift-0-60s.mp4`: 720p, the project as authored, with the night-shift
    score.
  - `still-<state>-1080p-t30.png`: one still per state.

## Performance (compiled; M-series Mac; the owner's editor was open in their checkout, so the lower resolutions are noisy)

GPU frame p50 in ms, with the `sdf` pass in parentheses. The render scale is relative to 1080p (the
editor's canvas scale, `--canvas-scale`, gives the same pixel counts). Each run is 240 frames from
t = 30 s.

| state | 1.0 (1920x1080) | 0.75 (1440x810) | 0.5 (960x540) | 0.33 (634x356) | steps avg / max | hit | out of steps |
|---|---|---|---|---|---|---|---|
| hall | 20.3 (17.2) | 12.4 (10.4) | 8.7 (7.1) | 6.9 (5.7) | 20.7 / 160 | 99.9 % | 0.1 % |
| rotunda | 24.1 (19.7) | 14.5 (11.7) | 9.9 (7.9) | 8.4 (6.5) | 26.6 / 160 | 99.8 % | 0.2 % |
| cathedral | 34.3 (27.4) | 20.9 (16.7) | 11.4 (8.8) | 12.5 (9.4) | 24.5 / 160 | 84 % | 0.1 % |
| lattice | 19.9 (16.8) | 12.5 (10.4) | 12.6 (10.3) | 7.9 (6.4) | 28.7 / 160 | 100 % | 0 % |

- **CPU frame p50** (1080p): 21.8 ms (hall), 26.5 (rotunda), 38.2 (cathedral) and 23.5 (lattice). The
  CPU frame includes waiting on the GPU.
- **The depth prepass** is 2.2-6.4 ms of those frames at 1080p (`depthPrepass: false` removes it; GTAO
  then does not see the SDF).
- **The interpreter**, for comparison, at 0.5: hall 105 ms, rotunda 104, cathedral 254 and lattice
  58.
- **Offline**: the 60 s 720p render ran at 67 fps.
- **The step budget.** A max of 160 in every state means a few rays always exhaust the budget (0.1 %).
  These are grazing rays; the averages are low.

## For the art agent

- **Add a preset:** add an entry to `space.json`'s `presets` with `"values": {"<param path>":
  [value]}`. The paths are those in the Parameters window, e.g. `sdf/space/node/state/amount`,
  `sdf/space/look/edge/color`, `post/bloom/intensity`. Recall it from the Presets panel, a scene
  state, or a timeline cue.
  - A new structural state is one more child under `morph "state"` in `space.scene.json`, plus a
    preset setting `amount` to its index. Only the one or two states the amount sits between are
    evaluated.
  - A morph from state 3 to state 0 passes through states 2 and 1 on the way (the 45 s frame of the
    review video). To go directly between two states, put them next to each other, or nest a second
    two-child morph.
  - A structural edit (a new node, a kind, toggling `enabled`) makes the compiled path compile again,
    which takes about 3 s the first time. Values never do.
- **Drive a rule from audio:** add a route
  `{"source": "audio.mid", "target": "sdf/space/node/<name>/<field>", "amount": x, "op": "add",
  "chain": {"attackMs": .., "decayMs": ..}}`. Name a node (`"name": "..."`) to address it. Discrete
  changes should go into ints (`count`), `enabled`, a scene state, or `random.<name>` (sample and hold
  on an event such as `music.bar`, `music.drop` or `beat.pulse`).
- **Capture a frame or a video (GPU, so through the lock):**

  ```
  tools/gpu-lock.sh ./build/release/src/avgen --headless --project examples/space/space.json \
    --render /tmp/space --range 30:30.4 --format png --size 1920x1080
  tools/gpu-lock.sh ./build/release/src/avgen --headless --project examples/space/space.json \
    --render ~/Desktop/av-gen-review/22-procedural-space/clip.mp4 --range 0:30 --format video
  ```

  - Take the last PNG of a short range, not the first: the first frame has no temporal history.
  - To use the owner's song, add `--audio ~/Desktop/Rebuild.mp3`, for owner-review captures only.
  - To isolate one state, copy the project, delete `states`, and set
    `"sdf/space/node/state/amount"` in `parameters`.
- **Measure:**

  ```
  tools/gpu-lock.sh ./build/release/src/avgen --headless --project examples/space/space.json \
    --frames 240 --size 960x540 --range 30:
  ```

  This prints the GPU pass medians (`sdf=`, `depth=`) and the "sdf march" steps line.
- **The other look knobs are ordinary parameters:**
  - fog: `scene/volumeDensity` (the surface fog's extinction), `scene/fogColor`, and
    `scene/volumeMaxDistance` (> 0 turns on the volumetric march, which costs frame time);
  - light: `lightrig/SpaceNocturne/keyIntensity|ambientIntensity|ambientColor` and
    `lightrig/SpaceNocturne/<key|rim|accent>/intensity|azimuth|elevation`;
  - material: `sdf/space/material/{baseColor, emissiveColor, emissive, roughness, metallic}`, and a
    material program (ADR-030) by name through `material.program` in the scene file;
  - post: `post/bloom/*`, `post/grade/*`, `camera/exposure/compensation`.
- **Known limits:**
  - One material per SDF object. A second material is a second `sdf` node, and a second full-screen
    march.
  - Occlusion and the soft shadow darken the whole shaded colour, emission and fog included. The soft
    shadow is a second march per pixel, so measure it before you rely on it.
  - Twist and bend distances are bounds, so keep `march/stepScale` below 1 when you use them.
  - `repeat` content must stay inside its cell.
  - Orbit (camera mode 0) integrates over time, so it does not scrub.
  - The graph editor does not offer the new kinds.
  - **Raymarched SDFs cast no shadow-map shadow.** This is a pre-existing defect, documented by the
    hidden probe in `test_sdf_gpu.cpp`: the shadow pass reuses the camera's screen rect. For local
    shadowing use `look/shadow/*`. The example sets `castShadows: false`.
  - The cathedral misses 16 % of its rays (it has open sky). They show the background colour, so the
    art agent may want a backdrop.

## Next steps (engineering)

1. The full `avgen_render_tests` run was started at about 01:10. Record its binary exit code here.
2. Possible optimisations, measured and not yet needed:
   - drop the depth prepass for the SDF (2-6 ms at 1080p);
   - over-relaxation (Keinert) for the exact states;
   - lower `maxSteps` for the 0.1 % exhausted rays.
3. §34 offline and timeline playback work (the 60 s render). Creative Critic runs are the art agent's.
