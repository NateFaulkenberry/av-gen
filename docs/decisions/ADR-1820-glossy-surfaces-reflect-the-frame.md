# ADR-1820: Glossy opaque surfaces reflect the frame (Hi-Z screen-space reflections)

- Status: Accepted (eng/render-borrow, 2026-10-29). Filament evaluation borrow item A1, approved by the owner.
- Reads ADR-1603's refraction copy (read-only, owner-approved); shares no code with the water's SSR (ADR-1286).
- Code:
  - `shaders/glossy_ssr.wgsl` (new): the Hi-Z march, the roughness blur, the trust.
  - `shaders/hiz_reduce.wgsl` (new): one min-depth pyramid level from the one below.
  - `src/rendering/glossy_ssr.{hpp,cpp}` (new): the pyramid, the SSR draws' group 3 and pipeline layout.
  - `shaders/pbr_shade.wgsl`: `override LIT_SSR` and `ssrSurface`; `shaders/pbr.wgsl`: `fs_main_ssr`.
  - `src/rendering/scene_renderer.{hpp,cpp}`: the eligible list, the pyramid after the linear depth, the copy also
    when glossy SSR runs, the SSR draws after the copy, a second copy for glass in such a frame, `noglossyssr`.
  - `src/scene/scene_types.hpp`, `src/scene/composition.cpp`: `environment.glossyReflections` (read and saved).
  - `src/rendering/render_quality.hpp` (`QualitySettings::glossySsr`), `quality_policy.hpp`; the ladder and the
    optimizer: `src/app/interactive_resolution.{hpp,cpp}`, `live_optimize.cpp`, `live_profile.{hpp,cpp}`,
    `live_profile_run.cpp`, `live_performance_host.cpp`.
- Tests: `tests/rendering/test_glossy_ssr_gpu.cpp`, `tests/unit/test_interactive_resolution.cpp`,
  `tests/unit/test_cosmic_key.cpp` (`[adr1820]`).

## Problem

An opaque glossy surface reflected only the environment map: the S3 lab's polished floor showed the moonlit sky, not
the glowing arch standing on it, where Filament's frame shows the arch in the floor. Only the cinematic water had a
screen-space reflection (ADR-1286), and it is the water's own.

## Decision

- **Opt-in, then the ladder.** A scene asks with `environment.glossyReflections` (default off). The quality lever
  `QualitySettings::glossySsr` then decides: on in Offline, Realtime and High (Ultra and High rungs), off in the
  Medium, Low and Emergency rungs of every strategy, the PERFORMANCE profile and the preview tier. The optimizer offers
  `noglossyssr` when a frame drew it and keeps it as a project ceiling; `--ab noglossyssr` measures it.
- **Which surfaces**: static opaque entity meshes, without a material program, lit (not unlit or toon), with
  roughness under 0.45. They stay in the opaque pass as they were and are drawn **again** after the refraction copy,
  additively, with `LIT_SSR` set: the shading runs as usual up to the environment reflection (no direct lights) and
  `fs_main_ssr` adds (reflected x weight - environment) x trust, through the fog's transmittance. The weight is the
  one the environment reflection used (the split sum, specular occlusion, ADR-1818's compensation, AO), so a full
  trust replaces the environment's reflection by the frame's, exactly.
- **The march** (Uludag's Hi-Z, the min-depth pyramid Godot's SSR builds): this frame's linear view depth at full
  resolution and a min-depth pyramid above it (7 levels, padded so each level halves exactly and a cell of level k is
  2^k pixels). The ray is marched in screen space with perspective-correct depth: a cell the ray crosses wholly in
  front of everything in it is skipped and the next is looked at one level coarser; otherwise one level finer; at a
  pixel the ray hits if it is not behind the surface by more than its thickness (0.03 x depth, at least 0.15 m) and
  passes under it otherwise. 64 steps, 30 m of reach, the ray started a few millimetres off the surface.
- **The hit** is read from the refraction copy (the opaque frame before the water and the glass), blurred over the
  lobe's footprint there (alpha = roughness^2 plus the reflection's turn per pixel, times the distance travelled, in
  the hit's screen size; 13 taps, none under 2 px).
- **The trust** falls to the environment, smoothly: with roughness (full below 0.25, none at 0.45); with how fast the
  reflection turns from pixel to pixel (full below 0.6 degrees a pixel, none at 1.7: a small curved surface's
  reflection is undersampled by one ray and crawls as the camera moves); toward the screen's edge (ADR-1821: a
  gradient over a quarter of the screen); toward the ray's
  reach; for a ray turning toward the camera (it would find the back of what it hits); and for a hit deep in the
  thickness. A miss is the environment.
- **Deterministic**: no jitter, no noise, no history. A seek lands on the frame play drew; a still is the same every
  run (tested, and S3's sequence hash is equal across two runs).
- **The copy**: taken also when glossy SSR runs (the only change to its schedule); what it holds and where it is taken
  are unchanged, and the water reads it exactly as before. In a frame with glossy SSR **and** an imported glass, the
  frame is copied once more after the reflections and the water, and the glass reads that: without it a glass sphere
  on a reflecting floor showed the floor unreflected through it.
- **Byte-identical without it**: `LIT_SSR` is an override, false in every other pipeline, so its code is compiled out;
  the SSR draws, the pyramid and the copy run only when a scene asks and the tier allows.

## Consequences

- S3 (the Filament reflection lab, with `glossyReflections`): the arch, the gold sphere, the pillars and the wall
  reflect in the floor, as in Filament's frame; the gold sphere keeps its environment reflection (its own reflection
  turns too fast per pixel to be trusted at this size).
- **Against Filament** (`a1-glossy-ssr/s3-off-on-filament.png`): the arch visibly reflects in the floor. Filament's
  reflection is short and streaky near the arch's foot; this one is the whole mirrored arch, because the floor is
  near-mirror (roughness 0.05) and the engine's environment weight (below) is stronger at grazing angles.
- **Cost: +0.95, +0.94 and +1.00 ms at 1080p** on S3 (`--ab noglossyssr`, 5 interleaved pairs each, noise floors 2.2-2.6%;
  baseline 4.5 ms). Under the evaluation's 1.5 ms bar. It is the pyramid (7 small passes), the four glossy surfaces drawn
  again (the floor covers most of the frame) with up to 64 steps a pixel, and in S3 the second copy for its glass. A
  scene that does not ask pays nothing.
- **Unchanged without it**: the five reference scenes (offline, 960x540) against the capture before this ADR: Aurora,
  Event Horizon, Rainbow World and Talking Heads SAME, Rift within 1 level (its run-to-run variation). The GPU test
  also checks that a scene that does not ask, and the lever off, draw the same frame hash.
- **Deterministic**: S3's 2 s render (60 frames) has the same sequence hash in two runs; the GPU test renders twice and compares.
- **Flicker** (a 4 s eased dolly-and-pan across S3, 120 frames at 1080p, each frame its own render;
  `a1-glossy-ssr/s3-camera-move-*.mp4`, the numbers in `flicker-measure.txt`). Two measures of luma:
  - the temporal second difference |f(t+1) - 2f(t) + f(t-1)|: on the floor band, pixels over 16 levels 1.11% with the
    SSR against 0.24% without. Most of it is the reflection's own moving edges (the arch, the sphere, the pillars),
    which an environment-only floor does not have; its per-frame count follows the camera's speed (ease in and out),
    not a noise floor;
  - oscillation, a pixel that goes up then down by more than a threshold on consecutive frames (a flip that comes
    back, which a moving edge does not do): over 32 levels, 66.6 per million pixel-frames attributable to the
    reflection (81.4 with it, 14.2 without); over 16, 195 per million. They sit on reflected silhouettes (the arch's
    edge, the sphere's outline, a glass's edge over a reflection), which are stepped by the depth buffer's pixels.
  - What brought it down: the curvature hand-over (93.6 -> 52.6 per million at 32 levels, mostly the gold sphere's
    rim arc) and the silhouette trust (69.0 -> 66.6). More march steps did not change it (256 against 64), and a hit
    refined within its pixel did not either. The second copy for glass added reflections inside the glass, and with
    them their edges (52.6 -> 69.0).
- **Limitations**:
  - what is drawn after the copy (glass, water, particles, blended meshes, the Environment renderers) is not reflected;
    the floor shows what is behind a glass where the glass would be;
  - skinned meshes, material programs and surfaces rougher than 0.45 keep the environment reflection;
  - no temporal filter: a reflected silhouette is stepped by the depth buffer's pixels, and its steps crawl as the
    camera moves (measured below); a small curved surface hands over to the environment rather than alias;
  - the engine's split sum weights the environment by F_roughness(n.v) x A + B where Filament uses F0 x A + B, so at
    grazing angles its reflections are stronger than Filament's (by an estimated 2-3x at n.v = 0.2, not measured);
    the SSR uses the same weight, consistent with the environment it replaces, so the S3 floor reflects the wall
    more strongly than Filament's. Not changed here.
  - the camera-move frames were each rendered alone (a seek), which is the same picture play draws here: nothing in
    this pass has history.
