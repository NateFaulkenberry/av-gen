# Hyperspace: how the shot is built, and what went wrong first

Hyperspace is the first of the four master scenes to be re-authored as a shot rather than as a
pattern. This note records the composition, the lighting, and — more usefully — the mistakes that
took the longest to find, because every one of them is a trap the other three scenes can fall into.

## The shot

Sixty seconds, one continuous move down a corridor towards a single distant source.

| Beat | Time | Intent |
|---|---|---|
| Approach | 0-18 s | Wide, quiet, mostly empty. The source is small and far. |
| Descent | 18-41 s | The corridor closes in, structure thickens, the source grows. |
| Arrival | 41-60 s | The source takes the frame; haze, bloom and the anamorphic streak arrive with it. |

The three beats are cue presets on the project timeline, morphing over 3, 9 and 12 seconds. They
move corridor radius, structure amplitude, source emission, haze density and the post chain
together, so the beat is a single artistic decision rather than ten sliders.

## Composition

The corridor runs down -Z. Five node kinds carry five distinct jobs:

- **pylons** — five very large beams at radius 74, running *along* the corridor. They exist only
  for scale: without something that reads as huge, the fine detail has nothing to be fine against.
- **gates** — a slow helix of heavy plates at radius 46. These replaced concentric torus rings.
- **tunnel** — a spiral of slats at radius 27-41 (the cue moves it) with gaps between them, so the
  eye follows a spiral rather than reading a stack of circles.
- **shards** — small dark debris near the camera path, the near depth layer, in silhouette.
- **core** — the subject. A displaced sphere at z=-540, the only bright thing in frame.

The camera follows a spline (`camera.mode` 2, `camera.spline` "path"): a Catmull-Rom weaving
through the corridor from (14, 9, -10) to the core. `camera/splineT` and `camera/lookAhead` are
keyed on the timeline, so the shot accelerates through the descent and shortens its look-ahead as
it arrives. This is the change that stopped the frame reading as concentric rings: a camera on the
corridor axis sees a bullseye whatever the geometry does, and one that weaves sweeps the walls
across frame and swings the vanishing point off centre. Four slow LFOs add a drift the audio never
touches.

## Lighting

`examples/lightrigs/void-core.rig.json` is a backlight rig: the key sits past the focal point and
shines back towards the camera, so structure reads as silhouette. Two cool rims at ±110 degrees
azimuth put a cold edge on the flanks, which is the only reason lit and unlit surfaces read as
different things. Ambient is 0.022 — enough to keep silhouettes off pure black, no more.

Exposure is **manual**. The automatic meter is metering an intentionally black frame, so it opens
to its floor and blows out everything the key touches. An authored shot wants an authored stop.

## Five mistakes worth remembering

**1. Cue presets outrank the scene file.** Editing a material in the `.scene.json` does nothing
if a cue preset names the same parameter — the preset wins from its cue onward. Hyperspace's gate
plates rendered as flat cream slabs through a dozen iterations of material edits because a preset
was pinning their emission to 1.5. The precedence is right and it no longer needs grepping for:
since 2026-09-10 loading a project prints the paths its cue presets will take over, and this
project's line names `procedural/gates/material/emissive` among ten others.

**2. `sourceTransform` was a silent no-op on the GPU.** It was applied by
`ProceduralGeometry::instanceMatrix()` and ignored by the vertex shader, so the gate rings kept the
torus generator's default axis (major circle in XZ, axis +Y) and lay across the lens. Fixed, with a
regression test.

**3. A scene's `lightRig` was only read inside `environment`.** Every example naming one at the top
level had been running on the default key light. Now accepted in both places.

**4. Volumetric fog is not atmosphere.** At `volumeDensity` 0.006 over a 420-unit corridor the haze
raised the black floor to mid-grey and destroyed every value relationship in the frame. It now runs
at 0.00004-0.00024, and the visible scattering belongs to a small practical at the source, so the
haze is a halo around the light rather than a veil over the shot.

**5. A low albedo does not make a surface dark.** With `baseColor` at 0.02 and a warm key, the
shards still rendered as flat tan cards — the visible term was the *specular* lobe, not diffuse,
and a narrow lobe on a broad flat face is a bright flat card whatever its albedo. Roughness 0.92
turned the same surfaces into composite. If something is too bright and lowering its colour does
nothing, the highlight is specular: raise roughness or move the light.

**6. The `disc` particle emitter spawns in the XZ plane** by construction and uses only
`extent.x`. Edge-on to a corridor running down Z, it produced a flat horizontal line. A `box`
emitter with zero depth is the cross-section this shot needed.

## Measuring, without pretending to measure beauty

`tools/image_stats.py` reports luminance mean, RMS contrast, percentiles, the fraction of the frame
in shadow/mid/highlight, clipping, mean saturation and the centroid of the bright mass. These do
not say whether a frame is good. They say whether two versions of it differ in the way the change
intended, which is the only thing a number can honestly do here. The arc above measures as a rising
mean (0.10 → 0.09 → 0.11 → 0.21) with rising contrast and no clipped pixels at any point, which is
what "the light gradually takes the frame" should look like as numbers.
