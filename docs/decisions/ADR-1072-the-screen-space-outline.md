# ADR-1072: The screen-space outline: depth, normal and object edges in one HDR pass

- **Status:** Accepted (2026-10-02), proto/sonic-garden (stylized engineering for the abstract direction)
- **Code:** `src/scene/post_outline.*` (settings, ten parameters, the `post` block keys); `fs_outline` in
  `shaders/post.wgsl`; stage 1a of `PostProcessor::run`; `PostFrameInputs::normal`.
- **Tests:** `tests/unit/test_toon_shading.cpp` (`[outline][adr1072]`), `tests/rendering/test_toon_gpu.cpp`
  (`[gpu][outline][adr1072]`).

## Context

Cel shading wants ink lines; the abstract direction asks for outlines and silhouette rendering on meshes and
procedurals. SDFs have their own edge light (ADR-1047); nothing drew lines round anything else. The scene pass already
writes everything an edge detector needs (ADR-035): depth, the normal target and the identifier target (object id in
the low 16 bits).

## Decision

One full-resolution pass, `post/outline`, after the exposure and before the atmospheric look, the defocus, motion blur
and bloom -- so a line hazes, defocuses, smears and blooms with the surface it is drawn round. Never encoded while
`amount` is 0 (byte-identical, tested). Each pixel compares itself with four opposite pairs of neighbours at half the
width (both sides of an edge draw, so the line is `width` wide):

- **depth:** the second difference of 1 / view depth across the pixel, relative to the pixel's own. 1 / view depth is
  linear on screen across any plane, so a floor at a grazing angle draws nothing (tested), where a first-difference
  test lines the whole far floor;
- **normal (creases):** 1 - cos of the angle to a neighbour's normal;
- **object:** a neighbour of a different object id, or the background.

`silhouette` 1 drops the creases (the outer outline and where one object passes in front of another). The colour is
exposed scene-linear light, so `intensity` above 1 glows and blooms. The line fades out between `fadeStart` and
`fadeEnd` metres (the nearer surface's distance).

| parameter | `post` key | range | default |
|---|---|---|---|
| `post/outline/amount` | `outlineAmount` | 0..1 (0 = off) | 0 |
| `post/outline/color` | `outlineColor` | colour | black |
| `post/outline/intensity` | `outlineIntensity` | 0..1000 | 1 |
| `post/outline/width` | `outlineWidth` | 0..64 px at 1080 lines | 1.5 |
| `post/outline/depthThreshold` | `outlineDepthThreshold` | 0.001..10 | 0.08 |
| `post/outline/normalThreshold` | `outlineNormalThreshold` | 0.001..2 | 0.35 |
| `post/outline/silhouette` | `outlineSilhouette` | 0/1 | 0 |
| `post/outline/objectEdges` | `outlineObjectEdges` | 0/1 | 1 |
| `post/outline/fadeStart` | `outlineFadeStart` | metres | 0 |
| `post/outline/fadeEnd` | `outlineFadeEnd` | metres (<= start: no fade) | 0 |

Widths are authored at 1080 lines and scale with the output (the ADR-917 rule), so a 4K render matches its preview.
UI: the Parameters panel, `post` group, **outline** section (beginner layer, beside bloom and the display family).

## Consequences

- Measured (M2 Max, 1920x1080, `bench-outline.scene.json`, width 2): `post/outline` 0.72 ms median.
- Procedural instances share one object id, so a line between two instances comes from depth or normals, not ids.
- Blended surfaces and particles write no depth or ids and get no line. The water and the sky do not either.
- Taps are whole pixels: below one pixel the line's strength is its coverage, so it thins out rather than aliasing.
