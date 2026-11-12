# ADR-2022: Blended meshes drawn into the water's frame copy (`reflectBlended`)

- Status: Accepted (2026-11-11), eng/water-blend-reflect.
- Request: [docs/requests/2026-11-11-water-blend-reflection.md](../requests/2026-11-11-water-blend-reflection.md), item
  4, from av-agent: option C of [ADR-2021](ADR-2021-water-reflection-follow-ups.md), after their option A (the fall
  exported as `MASK`) turned the close views' fall into a hard, opaque slab and could not work for a spray whose
  alpha never passes 0.18.
- Code:
  - key: `WaterSettings::reflectBlended` (`src/scene/water_surface.hpp:178`), read at `src/scene/composition.cpp:11916`;
  - the gate: `src/rendering/scene_renderer.cpp:5260`;
  - the extra pass and draw: `src/rendering/scene_renderer.cpp:5303-5316`, the draw itself at `:5313`;
  - the A/B arm: `nowaterblend`, `src/rendering/scene_renderer.cpp:2086`.
- Tests: `[adr2022]` in `tests/rendering/test_gltf_water_gpu.cpp` and `tests/unit/test_gltf_water_materials.cpp`.

## Context

The cinematic water (ADR-1286) reflects a copy of the frame, taken just before the water is drawn. Everything
alpha-blended is drawn after the water: blended meshes, particles, ribbons. So a BLEND waterfall is never in the
copy and never in the lake.

## Decision

A water key, `reflectBlended` (boolean, default false). When any cinematic surface in the frame has it, and the frame
has blended meshes to draw, the blended meshes are drawn a second time in a render pass of their own. That pass runs
after the copy is taken and before the water, and its colour target is the copy. The march and the sky mirror then
see the fall, translucency and all. The frame itself is drawn exactly as before.

- **Same draws.** The pass reuses the frame's blended draw (`drawItems(blended, true)`): the same pipelines,
  back-to-front order, object slots and depth test against the opaque depth. The fall in the copy is the fall in the
  frame.
- **No scratch targets.** Every blended pipeline writes the colour target only (ADR-035 masks the four auxiliary
  targets on the lit, skinned and compiled blend variants). So the pass binds the frame's own auxiliary targets,
  which come out untouched. ADR-2021 estimated about 48 MB of scratch targets at 1080p; the actual extra memory is
  none. The copy texture gains `RenderAttachment` usage, and only while the switch is on.
- **Frame-wide.** A frame has one copy, so any cinematic surface asking for it puts the blended meshes in the copy
  every surface reads. Refractive glass and glossy SSR, which read the same copy, see them too in such a frame.
- **Only meshes.** Particles, ribbons and shells stay out of the copy. The request was for meshes.
- **Off is free.** No pass is recorded, the copy is created as before, and no draw count changes.
- **Profiling.** The pass has its own timestamp label, `waterblend`. A quality arm, `--ab nowaterblend`, measures it
  interleaved against itself. No tier turns it off.

## Measured

On av-agent's alpine scene (their 7f81a5ed run `avgrun_000mwcudpn9c37080`), rendered from their manifest's arguments.
Their files were read only; output went to scratch.

**Off is 7f81a5ed.** With the key absent, this build's frames are byte-identical to the PNGs their 7f81a5ed binary
wrote:
- ground, closeup, footing and elevated match outright;
- their stored hero PNG is a 1280x720 crop of the 1280x1010 render, and it matches at row offset 1.

A 7f81a5ed binary built here reproduces their ground frame byte for byte too, so the comparison is across builds, not
luck.

**On.** Fall and spray appear in the lake's reflection on ground, hero and elevated, soft and translucent like the
frame's own fall.

| view | pixels that change | on the lake | off the lake |
|---|---|---|---|
| ground | 2,192 | 2,191 (max 49/255) | 1 (1 level) |
| hero | 1,767 | 1,745 (max 19) | 22 (1 level) |
| elevated | 1,253 | 1,249 (max 18) | 4 (1 level) |
| closeup, footing | 0 | | |

The few off-lake pixels move by one 8-bit level. They come from post (bloom and FXAA spread the lake's change). The
GPU test holds every HDR pixel above the waterline identical with post off. The close views are byte-identical to
7f81a5ed with the key on, because their lake shows no reflection of the fall.

**Cost** (their ground view with the key on, `--headless --live-frame --size 1920x1080`, back to back):

| run | GPU frame | `waterblend` pass |
|---|---|---|
| `--ab nowaterblend`, 8 interleaved pairs | +0.39 ms on (26.40 against 26.00; pairs +0.35 to +0.45) | 0.39 ms |
| key off, `--ab none` | 25.60 ms | absent |
| key on, `--ab none` | 26.18 ms | 0.39 ms |

The tool's calibrated 2% floor calls a 1.5% effect "not a result". The pair-to-pair spread is 0.37%, and the
`waterblend` timestamp measures the pass directly at 0.39 ms: the two meshes again, with their fragment shading. On
a scene with more blended meshes the cost grows with them.

**Memory:** no new allocation. The pass renders into the frame copy, which already exists for the cinematic water
(1920x1080 RGBA16F, 16.6 MB), and reuses the frame's auxiliary targets. ADR-2021's 48 MB estimate assumed scratch
targets that turned out to be unnecessary.

## Consequences

- A blended mesh below the waterline now appears twice: refracted through the water (from the copy) and drawn over
  it by the frame's own blended pass, as before. Only scenes that turn the key on are affected. Alpine's fall and
  spray are above the lake.
- The cost scales with the frame's blended draws, not with the water. A scene with many blended meshes (leaf cards,
  glass) pays for all of them again when one surface asks.

## Rejected alternatives

- **Draw the blended meshes before the copy, once.** No extra draw, but then the water (opaque at `maxOpacity` 1)
  draws over any blended thing in front of it: spray at the fall's foot, glass, leaf cards.
- **Scratch auxiliary targets for the second draw.** Unneeded: the blend pipelines never write them.
- **Write-masked twins of every blended pipeline.** Unneeded for the same reason.
- **A float `amount` instead of a boolean.** The copy either has the meshes or it does not. A fraction would be a
  second blend of the copy, with nothing to say about what it means.

## Revisit triggers

- A scene that wants particles or ribbons in the reflection too.
- A scene with many blended meshes where the second draw costs too much: per-mesh opt-in (a node key) would then
  be the next step.
