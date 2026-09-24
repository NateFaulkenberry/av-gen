# Catalog: Stylization

Part of the [Effect Library](README.md). The master table is in [catalog.md](catalog.md). Tags are
defined in [shared-infrastructure.md](shared-infrastructure.md).

## How the family splits

The spec asks for Stylization to be split into material, entity and camera/post effects. Each
effect below is placed at the lowest level that can express it.

| Effect | Material | Entity | Camera / Post |
|---|---|---|---|
| Fresnel | **exists** (program `Fresnel` op) | FXL rim lane | – |
| Rim Light | – | **FXL** (directional rim) | – |
| Dissolve | program opacity (breaks shadows, see below) | **FXL clip lane** | – |
| Hologram | – | **FXL + blended re-route** | – |
| Scanlines | – | (part of Hologram) | **FXPOST** |
| Chromatic Aberration | – | id-masked FXPOST mode | **exists** (lens CA); DF chroma for local |
| Pixelation | – | id-masked FXPOST mode | **FXPOST** |
| Dithering | – | screen-door fade: **exists** in the depth pass (ADR-701) | **FXPOST** |
| Toon-ish Edges | – | inverted hull (REDRAW) | **FXPOST** (depth/normal/id edges) |
| Color Cycling | **exists** (program `HueShift` / `Palette`) | FXL hue lane | exists (`post/hueShift` + a route) |

**Material-level** stylization is already covered by material programs. A program is a per-fragment
op interpreter with Fresnel, Ramp, Palette, HueShift, Voronoi and Noise, and inputs for Time, Audio
and BeatPhase (`src/scene/material_program.hpp:1-161`). Its limit is that a program is **shared by
every user of the material** (`src/scene/material_params.hpp:3-5`). So the effect library adds
*entity-scoped* versions through FXL, and no new material ops except Worley F2.

---

## Fresnel  (`fresnel`)

- **2.1 Definition.** A view-angle-dependent colour or opacity term that is strongest at grazing
  angles. It has two modes:
  - **Additive:** emission at the rim.
  - **Opacity:** the edges become opaque while the centre stays transparent (the x-ray look). This
    mode needs the owner to be blended.
- **2.2 References.** Schlick's approximation, which the engine already uses for BRDF Fresnel
  (`fresnelSchlick`, `shaders/pbr_shade.wgsl:72`). The artistic form is `pow(1−N·V, p)·scale + bias`.
- **2.3 Visual anatomy.** A soft coloured outline that follows the silhouette and stays consistent as
  the camera moves.
- **2.4 Implementation.** FXL lane `fxRim = (color.rgb, power)` with `fxRim2 = (bias, scale, mode,
  -)`, added to emission and to the emission target. Glow's rim uses the same lane, so the two share
  one lane. Opacity mode uses the ADR-385 alpha-mode promotion path, which Composition already uses to
  fade nodes.
- **2.5 Targets.** Entity. Material works through the program op, which exists.
- **2.6 Parameters.** `color`, `power`, `scale`, `bias`, `mode`.
- **2.7 Modulation.** `audio.treble → scale`, `owner.cameraDistance`.
- **2.8 Animation model.** Stateless.
- **2.9 Compositing.** Part of the surface. It is fogged and bloomed.
- **2.10 Stack behaviour.** `Material`. Rims from several effects sum, and the colours are weighted by
  scale.
- **2.11 Performance class.** Very Low.
- **Presets.** X-Ray, Soft Edge, Ghost.

## Rim Light  (`rimLight`)

- **2.1 Definition.** A stylised *back-light* rim. It is lit rim only where the surface faces away
  from the camera **and** toward a rim direction, like a cinematographer's kicker light. It differs
  from Fresnel, which is view-only and has no light direction.
- **2.2 References.**
  - Cinematography's kicker or rim light.
  - Stylised shading in GGXrd [R20].
  - The engine's styled-path rim (`pbr_shade.wgsl:386`).
- **2.3 Visual anatomy.** A crisp bright edge on one side of the silhouette, as if a light sat behind
  and to the side. It stays attached to that side as the object turns.
- **2.4 Implementation.**
  - FXL lane: `rim = pow(1−N·V, p)·saturate(dot(N, L_rim))·color·intensity`.
  - `L_rim` is either camera-relative (fixed in view space, the default, so it frames well) or taken
    from a named Light endpoint's direction.
  - The `threshold` / `softness` pair gives a toon-crisp edge.
  - It is not a real light, so it casts no shadow and has no light cost.
- **2.5 Targets.** Entity. A Light owner applies rim to entities in its radius, which is a later wave.
- **2.6 Parameters.** `color`, `intensity`, `power`, `direction` (view-space angles), `source` (Camera
  / Light), `threshold`, `softness`.
- **2.7 Modulation.** `beat.pulse → intensity`, `audio.bass`.
- **2.8 Animation model.** Stateless.
- **2.9 Compositing.** A lighting-like term inside the lit fragment, before fog. Bloom weight is
  optional.
- **2.10 Stack behaviour.** `Material`.
- **2.11 Performance class.** Very Low.
- **Presets.** Kicker, Neon Edge, Toon Rim.

## Dissolve  (`dissolve`)

- **2.1 Definition.** The owner disintegrates, or materialises, through a noisy threshold with a
  glowing burning edge. Optional embers leave the edge.
- **2.2 References.** The standard noise-threshold alpha test with an edge band. The alpha test must
  also run in the depth prepass and the shadow pass, otherwise the shadow and depth of dissolved
  parts remain. The engine exhibits this today for program-driven opacity (`fs_depth`,
  `shaders/pbr.wgsl:63-95`, does not run programs), and ADR-701 hit the same class of bug with fading
  shadows.
- **2.3 Visual anatomy.** Holes open in a noise pattern and grow until the object is gone. The rims
  of the holes glow hot and fade to dark char. Sparks fly off.
- **2.4 Implementation.**
  - FXL clip lane `fxClip` in mode Noise: `coord = fbm3(p_object·scale)` combined with a direction
    bias.
  - The fragment discards where `coord < progress`. The edge band adds `edgeColor·edgeEmission`.
  - The same test runs in `fs_depth`, because FXL lives in object uniforms and not in the material
    program.
  - The lane is shared with Growth (Reveal mode). Embers come from EMIT with a Box emitter at the
    owner's bounds. Emitting exactly from the edge would need surface sampling, and that is deferred.
- **2.5 Targets.** Entity. Material cannot dissolve correctly as a program until `fs_depth` runs
  programs, and it should not.
- **2.6 Parameters.** `progress`, `scale`, `edgeWidth`, `edgeColor`, `edgeEmission`, `direction`,
  `directionBias`, `charWidth`, `embers`.
- **2.7 Modulation.** Timeline `progress`, TRIGGER plus `duration`, `music.drop`.
- **2.8 Animation model.** Stateless: a parameter, or a function of age.
- **2.9 Compositing.** The owner is opaque with discard, so depth, shadows and velocity are all exact.
  The edge blooms.
- **2.10 Stack behaviour.** `Geometry` (clip) and `Material` (edge).
- **2.11 Performance class.** Low. Discard disables early-Z for this draw in the prepass.
- **Presets.** Burn Away, Teleport Out (blue, upward), Materialise (reverse).

## Hologram  (`hologram`)

- **2.1 Definition.** The owner renders as a projected hologram: translucent, additive, with scan
  bands, Fresnel edges, flicker and glitch jitter.
- **2.2 References.** Common game practice: unlit additive, world-space scanlines, Fresnel, and
  temporal flicker. There is no canonical paper.
- **2.3 Visual anatomy.**
  - A tinted, see-through figure with a brighter silhouette.
  - Horizontal lines scroll upward.
  - Brightness flickers.
  - Horizontal slices occasionally shift sideways (the glitch).
  - Internal faces are visible.
  - An optional projector beam (Light Beam) sits beneath it.
- **2.4 Implementation.**
  - An FXL flag re-routes the owner's draw item from the opaque list to the blended list with a
    `hologram` pipeline variant: additive blending, no depth write, unlit.
  - Scene bucketing already does this kind of re-route: ADR-385 promotes fading nodes to Blend, and
    ADR-701 handles the depth-pass consequence.
  - Fragment: `tint·(fresnel + scan(worldY·density − t·speed))·flicker(t)`.
  - Vertex: a glitch shifts vertices within hashed Y bands by `hash(floor(t·rate), band)`.
  - The owner stops casting shadows while it is a hologram.
- **2.5 Targets.** Entity.
- **2.6 Parameters.** `tint`, `opacity`, `fresnel`, `scanDensity`, `scanSpeed`, `scanStrength`,
  `flicker`, `glitchRate`, `glitchAmount`, `emission`.
- **2.7 Modulation.** `audio.onset → glitchAmount`, `beat → flicker`, `audio.treble → scanSpeed`.
- **2.8 Animation model.** Stateless.
- **2.9 Compositing.**
  - Additive after opaque, so it does not occlude and is not sorted. Additive blending is
    order-independent.
  - Fogged.
  - It writes emission, so it blooms.
  - It writes velocity from the blended path. Skinned owners must use the skinned blended pipeline,
    which exists: `skinning_->litPipeline(blend, doubleSided)`, `scene_renderer.cpp:3645-3655`.
- **2.10 Stack behaviour.** `Material` (pipeline route) and `Geometry` (glitch).
- **2.11 Performance class.** Low to Medium, depending on overdraw of internal faces.
- **Presets.** Sci-Fi Projection, Corrupted Signal, Ghost Recording.

## Scanlines  (`scanlines`)

- **2.1 Definition.** Screen-space CRT-style horizontal lines. Optional extras are phosphor mask,
  roll, curvature and interlace flicker.
- **2.2 References.** CRT emulation shaders (the libretro CRT family): scanline intensity modulated by
  pixel brightness, which is a beam-width model, plus an aperture grille mask.
- **2.3 Visual anatomy.** Fine dark lines across the frame. They thin on bright pixels, a slow roll
  bar passes, and the edges are slightly curved.
- **2.4 Implementation.** An FXPOST pass, Camera-owned. Line count scales with output height. Band
  darkening is `mix(1, profile(y), strength·(1 − luma^γ))`. Curvature reuses the existing `distort()`
  in `post.wgsl`. It runs **after** tonemap-space conversion if it needs to look right in display
  space. The recommendation is to run it on the HDR before bloom, as a luminance modulation, so bloom
  softens the lines the way real phosphor bleed does.
- **2.5 Targets.** Camera. Entity scanlines are part of Hologram.
- **2.6 Parameters.** `density`, `strength`, `beamWidth`, `rollSpeed`, `rollStrength`, `mask`,
  `curvature`.
- **2.7 Modulation.** `beat → rollStrength`, `music.break → strength`.
- **2.8 Animation model.** Stateless.
- **2.9 Compositing.** Post, before bloom.
- **2.10 Stack behaviour.** `PostProcess`.
- **2.11 Performance class.** Very Low.
- **Presets.** CRT, Surveillance, VHS (with Chromatic Aberration and grain).

## Chromatic Aberration  (`chromaticAberration`)

- **2.1 Definition.** The colour channels separate. There are three kinds:
  - **Lateral (lens):** radial from the centre. This already **exists** as the lens property
    `post/lens` chromatic aberration (`shaders/post.wgsl:276-290`).
  - **Localised:** around a distortion. This is DF's `chroma` term.
  - **Glitch:** channel offsets confined to an entity or to screen bands.
- **2.2 References.** Brown–Conrady lateral CA. Kawase-style spectral sampling improves on 3 taps by
  using N-tap spectral weights, which avoids the hard RGB split.
- **2.3 Visual anatomy.** Red and blue fringes at high-contrast edges, growing toward the frame edges
  (lateral), or jittery RGB offsets over one entity (glitch).
- **2.4 Implementation.**
  - **Camera:** do not build a second lateral CA. The Camera effect is a *preset of routes* onto the
    existing lens parameter, for example `beat.pulse → post/lens/chromaticAberration`. It adds only
    two things: a centre (for off-centre CA) and spectral taps, both upgrades to `fs_lens` itself.
  - **Entity (Glitch):** an FXPOST pass masked by `ids == owner`. The id target exists (R32Uint,
    `aux-ids`); its only post consumer today is the sharpen mask (`post.wgsl:391-403`). The pass
    applies per-band hashed channel offsets.
- **2.5 Targets.** Camera (lens), Entity (glitch). Localised CA belongs to DF effects.
- **2.6 Parameters.** `amount`, `center`, `spectralTaps`, `mode`, `glitchRate`, `bandHeight`.
- **2.7 Modulation.** `beat.pulse → amount` (the most common AV route), `music.drop`,
  `owner.speed`.
- **2.8 Animation model.** Stateless.
- **2.9 Compositing.** The lens pass sits in the post chain as it does today. Glitch runs in FXPOST
  before bloom.
- **2.10 Stack behaviour.** `PostProcess`.
- **2.11 Performance class.** Very Low (lens). Low (masked glitch, which reads the id target).
- **Presets.** Beat Fringe, Lens Edge, RGB Glitch.

## Pixelation  (`pixelation`)

- **2.1 Definition.** The image, or one entity, is quantised into large pixels.
- **2.2 References.** Block quantisation that samples at block centres. For stability under camera
  motion, snap the block grid to screen space and accept swimming, or use world-anchored blocks,
  which cost more.
- **2.3 Visual anatomy.** Mosaic blocks, with optional palette reduction and optional grid lines.
- **2.4 Implementation.**
  - An FXPOST pass: `uv' = (floor(uv·res/block) + 0.5)·block/res`.
  - **Entity mode:** the mask is taken from the id *at the block centre*, so the whole block switches
    together. The pass samples the id target at `uv'`.
  - Palette reduction is shared with Dithering.
- **2.5 Targets.** Camera, Entity (masked).
- **2.6 Parameters.** `blockSize` (px at 720p, scaled like other post radii), `mode`, `paletteLevels`,
  `gridLines`.
- **2.7 Modulation.** `beat → blockSize` (a pixel-crush hit), `music.break`.
- **2.8 Animation model.** Stateless.
- **2.9 Compositing.** Post. Bloom applied after it blooms the blocks. That is intended; the
  alternative ordering is available by priority.
- **2.10 Stack behaviour.** `PostProcess`.
- **2.11 Performance class.** Very Low.
- **Presets.** 8-bit, Censor (Entity), Beat Crush.

## Dithering  (`dithering`)

- **2.1 Definition.** Colour-depth reduction with an ordered dither pattern, as a stylised 1-bit or
  low-palette look.
- **2.2 References.**
  - Bayer ordered dithering.
  - Lucas Pope's *Return of the Obra Dinn* dev log [R21]: screen-space dither swims under camera
    motion. He anchored the pattern to the camera's view sphere so it stays stable under rotation.
  - For per-entity fades, the engine already uses stochastic, screen-door transparency in the depth
    pass (ADR-701).
- **2.3 Visual anatomy.** A limited palette (1-bit, 2-bit, or a custom palette) with a regular
  cross-hatch dither in gradients.
- **2.4 Implementation.**
  - An FXPOST pass *after* tonemap, in display space, because dithering is a display-quantisation
    look. This needs an FXPOST hook post-tonemap (see rendering-architecture).
  - Luminance or palette distance selects the levels.
  - The Bayer 8×8 threshold is indexed by screen pixel. In **Stable** mode it is indexed by
    view-sphere direction instead (Pope).
- **2.5 Targets.** Camera.
- **2.6 Parameters.** `levels`, `palette` (choice), `colorA`, `colorB`, `matrixSize`, `stable`,
  `pixelScale`.
- **2.7 Modulation.** `music.break → levels`, timeline.
- **2.8 Animation model.** Stateless.
- **2.9 Compositing.** After tonemap and before the 2D composition pass (ADR-083).
- **2.10 Stack behaviour.** `PostProcess`, at the post-tonemap slot.
- **2.11 Performance class.** Very Low.
- **Presets.** 1-Bit Obra, Gameboy, Newsprint.

## Toon-ish Edge Treatment  (`toonEdges`)

- **2.1 Definition.** Ink outlines at silhouettes and creases. There are two modes:
  - **Screen:** the whole frame, from G-buffer discontinuities.
  - **Hull:** per entity, from an inverted hull.
- **2.2 References.**
  - [R19] Saito & Takahashi 1990: edges from depth and normal discontinuities in G-buffers.
  - Inverted hull (Motomura, GDC 2015 [R20]): draw back faces pushed along normals, in black.
- **2.3 Visual anatomy.** Dark lines of controlled width at object silhouettes. They are optionally
  thinner at distance, with crease lines at sharp normal changes and optional colour-tinted lines.
- **2.4 Implementation.**
  - **Screen (FXPOST):** a Sobel or Roberts operator on the linear depth (`aux-linear-depth`, R32F),
    the normal target (`aux-normal-roughness`, octahedral), and **id discontinuities** (`aux-ids`,
    which are robust silhouettes).
    - Depth edges are thresholded relative to depth, so distant hills do not ink.
    - Line width is in pixels, scaled by height.
    - The output is composited before bloom, or after, for crisp ink.
  - **Hull (REDRAW):** front-face-culled draw, vertices pushed by `width·viewDepth` along the normal,
    flat colour, depth-tested.
- **2.5 Targets.** Camera (Screen), Entity (Hull, or Screen masked by id).
- **2.6 Parameters.** `width`, `color`, `depthThreshold`, `normalThreshold`, `useIds`,
  `distanceFade`, `mode`.
- **2.7 Modulation.** `beat → width`, `audio.bass`.
- **2.8 Animation model.** Stateless.
- **2.9 Compositing.** Screen mode ignores transparency, because particles and blended surfaces write
  no id or normal, so they have no ink. Hull mode is real geometry.
- **2.10 Stack behaviour.** `PostProcess` (Screen), Geometry (Hull).
- **2.11 Performance class.** Low (Screen: 9 taps × 3 targets). Medium (Hull: redraw).
- **Presets.** Ink, Comic, Glowmere Linework (coloured lines).

## Color Cycling  (`colorCycling`)

- **2.1 Definition.** Hue or palette animation over time, uniform or as a moving spatial wave. It
  comes from classic palette cycling (Mark Ferrari's animated 8-bit scenes).
- **2.2 References.** Palette-index cycling. A hue rotation in a perceptual space; `hueRotate` exists
  in `post.wgsl:345`.
- **2.3 Visual anatomy.** Colours shift through the spectrum. With a spatial term a rainbow band
  travels across the object.
- **2.4 Implementation.**
  - **Entity:** FXL lane `fxHue = (speed, range, spatialFreq, channel)`. The fragment rotates the hue
    of base colour, emission or both, with the phase set by `t·speed + dot(p, axis)·freq`.
  - **Material:** the program `HueShift` op with a `Time` input already does this. No effect is
    needed.
  - **Camera:** `post/hueShift` exists. The Camera effect is a route preset: `lfo → post/hueShift`.
- **2.5 Targets.** Entity, plus Material and Camera through existing params.
- **2.6 Parameters.** `speed`, `range` (degrees), `spatialFrequency`, `axis`, `channel`, `palette`
  (optional stepped).
- **2.7 Modulation.** `beat.phase → phase` (beat-locked), `audio.centroid → speed`.
- **2.8 Animation model.** Stateless. The phase is a function of t.
- **2.9 Compositing.** Surface term.
- **2.10 Stack behaviour.** `Material`.
- **2.11 Performance class.** Very Low.
- **Presets.** Rainbow Wave, Psychedelic, Slow Drift.
