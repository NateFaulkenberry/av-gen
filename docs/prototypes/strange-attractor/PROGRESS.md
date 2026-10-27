# The Strange Attractor: progress

Branch `scene/strange-attractor` in `../av-gen-attractor`, cut from main 3ea9b678. ADRs 1850-1869 (used: 1850).
Review media: `~/Desktop/av-gen-review/101-strange-attractor/gate<N>/`.

The brief is `00-brief.md`, the research `01-research.md`, the plan `02-plan.md`.

**Owner rules that override the brief:**
- one agent, with no subagents;
- art before optimisation;
- no examples index or live switcher entry: open it with `--composition`;
- every avgen run under `tools/gpu-lock.sh`;
- audio only from the current signals.

## Where it stands

| Gate | Stages | State |
|---|---|---|
| 1 | 0 research, 1 skeleton | **done, awaiting the owner**: commit (see git log), media in `gate1/` |
| 2 | 2 recursive landmarks, 3 branching and the attractor | not started |
| 3 | 4 art direction, 5 audio | not started |
| Final | 6 tiers and performance, 7 polish, tests and docs | not started |

## How to see it

```
tools/gpu-lock.sh build/release/src/avgen --headless --composition examples/strange-attractor/strange-attractor.scene.json \
    --render <dir> --format png --range 0:0 --size 1920x1080
```

- Variants: copy the scene file and change `attractorWorld.seed`, `resolution` (2048 live, 4096 offline),
  `strataFold`, or `look.palette` (0 Dusk Mineral, 1 Nocturne, 2 Chromatic).
- Nocturne also wants `sunElevation` 2.5, `sunIntensity` 0.7, `ambient` 2.0 and `atmosphere` 1.5.
- `--scene` loads glTF only; a scene file goes through `--composition`.

## Code map

- `src/scene/attractor_world.cpp` holds the generator:
  - the layout (frame, valley trunk, composed masses, strata seed);
  - pass 1a: the field F and the valley distances;
  - pass 1b: the heights (metric cliffs, tiers, bench, gorge, terraces);
  - pass 2: gullies;
  - pass 3: cavity and the masks;
  - the camera search;
  - JSON and parameters.
- `shaders/attractor/world.wgsl` holds the march, the strata colour, the shading, the sky and the fog. The palettes
  are in `src/rendering/attractor_renderer.cpp`.

## Decisions (each with its evidence in 01-research.md §5)

- Canyon country from one erosion-front field, not masses on a plain.
- Metric cliffs (`F / |grad F|`), with a wide stencil and a gradient floor.
- Strata at three scales; ACES rather than AgX; strataFold 0.45 by default.

## Known weaknesses at Gate 1 (honest)

- The image is a convincing canyon, not yet surreal. The impossible geology (nested arches, the attractor ribbon,
  strata bending around it) is Gate 2's job.
- The near foreground is weak in the primary composition: a shapeless lumpy talus mound at lower left.
- The cliffs show vertical "curtain" streaks: stretched texels on near-vertical faces, with flutes on top.
- The gorge walls show comb-like highlights at 2048 (better at 4096).
- The Chromatic palette is loud: its teal accent lands on talus rubble.
- A seed change regenerates on the main thread (about 1.3 s at 2048).

## Log

- 2026-10-27: Stage 0 research and plan; the Environment, generator, pass and tests (5 cases, 106 assertions, pass);
  Gate 1 media rendered.
