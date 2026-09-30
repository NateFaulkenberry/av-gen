# Procedural Space POC: progress

Resume from here. Branch `proto/procedural-space` in `../av-gen-space`. The ADR block is 1000-1019;
1000, 1001 and 1002 are used.

## Rules in force

- The GPU belongs to the owner until about 23:45 on 2026-09-29. After that, all GPU work, **including
  `avgen_tests`**, goes through `tools/gpu-lock.sh`.
- Build: `cmake --preset release && cmake --build --preset release`. Reconfigure after adding a test
  file.

## Status

| phase | state | notes |
|---|---|---|
| 1 architecture | done | `ARCHITECTURE.md`, ADR-1000: the POC is hosted on the existing ADR-027 SDF path as composition `sdf` nodes |
| 2 research | done | `RESEARCH.md`: A (the existing ray marcher) plus E, which comes free; F (a compiled SDF) is the fallback if measurement condemns the interpreter |
| 3-6 foundation | engine and CPU tests done; GPU verification pending | ADR-1001 (morph, fold, recurse, names, `count`/`axis`), ADR-1002 (look, march cap, step statistics), `examples/space` |
| 4 instrumentation | code done; numbers pending | see "Performance" |
| 7, 8, 11 look, presets, evaluation | the art agent's | see "For the art agent" |

## What exists (engine)

- **New SDF kinds** (`src/spatial/sdf.*`, `shaders/sdf.wgsl`):
  - `morph`: structural states on one float, evaluating at most two states.
  - `fold`: a plane reflection.
  - `recurse`: up to 8 levels of fold, rotate, scale and offset, unioned. This gives nested
    architecture, and runs as an interpreter loop.
- **Named nodes**: `sdf/<object>/node/<name>/<field>`. `count` (int) and `axis` are now parameters.
- **Limits**: depth 12 and 96 nodes (were 8 and 64). The 8-entry interpreter stacks are unchanged and
  still validated.
- **March and look parameters on every SDF object**: `march/{maxSteps, epsilon, stepScale,
  maxDistance}` and `look/{ao, edge, shadow}/...`. Each look term is off at 0.
- **Step statistics**: sampled on every 4th pixel in x and y. `SdfStats` carries `avgSteps`,
  `maxSteps`, `hitRatio` and `exhaustedRatio`. They appear in the Render stats lines and in the
  headless benchmark's "sdf march (median ...)" line.
- **Tests**:
  - `tests/unit/test_sdf_space.cpp`: tree against packed parity, Lipschitz bounds for morph, fold and
    recurse, names, parameters, JSON.
  - `tests/unit/test_space_example.cpp`: the example loads, every route and preset target resolves,
    the states move the morph, the eye is in open space in every state, and two engines pack the same
    program.
  - GPU parity cases for morph, fold and recurse have been added to `tests/rendering/test_sdf_gpu.cpp`.
    **They have not been run yet** (the GPU was the owner's).

## The example: `examples/space/space.json` (also in the Examples menu under Lab)

- **Scene** (`space.scene.json`): one raymarched `sdf` node named `space`, 54 nodes. The root chain is
  `twist` > `bend` > `fold` (disabled) > `morph "state"`. The morph's children are:

  | amount | state | structure |
  |---|---|---|
  | 0 | `hall` | a corridor along +x: floor, ceiling, mirrored walls with repeated doorways, mirrored repeated columns, repeated half-torus ribs |
  | 1 | `rotunda` | polar-repeated inner and outer colonnades, stacked rings |
  | 2 | `cathedral` | `recurse "nest"`, a chamber of columns, crown and slab nested inside itself |
  | 3 | `lattice` | 3D-repeated beams: mathematical space |

- **Look**: a dark blue-violet material, cyan edge emission, SDF AO, and surface distance fog
  (`volumeDensity` 0.018, with the volumetric march off: `volumeMaxDistance` 0). The light rig is
  `space.rig.json`: a cool high key with shadows, a violet rim, and a faint green under-fill.
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
  16 bars (quantised to the bar, with a 4 s smooth morph). A `music.drop` jumps to Lattice.
- **Presets**:
  - `state/hall|rotunda|cathedral|lattice`: the morph amount only.
  - `camera/still`: the stationary observer, the default.
  - `camera/forward`: the forward LFO on.
  - `camera/orbit`: camera mode 0.

## For the art agent

- **Add a preset:** add an entry to `space.json`'s `presets` with `"values": {"<param path>":
  [value]}`. The paths are those in the Parameters window, e.g. `sdf/space/node/state/amount`,
  `sdf/space/look/edge/color`, `post/bloom/intensity`. Recall it from the Presets panel, a scene
  state, or a timeline cue. A new structural state is one more child under `morph "state"` in
  `space.scene.json`, plus a preset setting `amount` to its index. Only the one or two states the
  amount sits between are evaluated, so extra states are free while inactive.
- **Drive a rule from audio:** add a route
  `{"source": "audio.mid", "target": "sdf/space/node/<name>/<field>", "amount": x, "op": "add",
  "chain": {"attackMs": .., "decayMs": ..}}`. Name a node (`"name": "..."`) to address it. Discrete
  changes should go into ints (`count`), `enabled`, a scene state, or `random.<name>` (sample and hold
  on an event such as `music.bar`, `music.drop` or `beat.pulse`).
- **Capture a frame or a video (GPU, so through the lock):**

  ```
  tools/gpu-lock.sh ./build/release/src/avgen --headless --project examples/space/space.json \
    --render /tmp/space --range 30:30.05 --format png --size 1920x1080
  tools/gpu-lock.sh ./build/release/src/avgen --headless --project examples/space/space.json \
    --render ~/Desktop/av-gen-review/22-procedural-space/clip.mp4 --range 0:30 --format video
  ```

  To use the owner's song, add `--audio ~/Desktop/Rebuild.mp3`, for owner-review captures only.
- **Measure:**

  ```
  tools/gpu-lock.sh ./build/release/src/avgen --headless --project examples/space/space.json \
    --frames 240 --size 960x540
  ```

  This prints the GPU pass medians (`sdf=`) and the "sdf march" steps line.
- **Known limits:**
  - One material per SDF object. A second material is a second `sdf` node, and a second full-screen
    march.
  - Occlusion and the soft shadow darken the whole shaded colour, emission and fog included.
  - Twist and bend distances are bounds, so keep `march/stepScale` below 1 when you use them.
  - `repeat` content must stay inside its cell.
  - Orbit (camera mode 0) integrates over time, so it does not scrub.
  - The graph editor does not offer the new kinds.

## Performance

*Pending: the GPU is the owner's until about 23:45.*

## Next steps

1. After 23:45, under the lock: run `avgen_render_tests "[sdf]"` for the new parity cases, then run
   the full `avgen_tests` and `avgen_render_tests` and judge them by their exit codes.
2. Benchmark each state at 1920x1080, 1440x810, 960x540 and 634x356 (render scales 1, 0.75, 0.5 and
   0.33). Record the `sdf` pass time, the frame time and the steps.
3. If the interpreter cannot hold a frame at 0.5, compile the tree (ADR-1000 fallback), or first check
   whether the depth prepass's second march is the cost.
4. Capture a first frame per state for the art agent.
