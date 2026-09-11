# ADR-083: A 2D composition above the 3D render

Status: accepted
Date: 2026-09-10

## Context

The engine could make a picture. It could not make a **finished piece**. Everything it produced
was the 3D render and nothing else: no titles, no lyrics, no captions, no framing, no plate behind
a credit. The gap between "this looks extraordinary" and "this is a music video" was entirely
made of things that live *above* the render, not inside it.

The obvious shortcut — a text feature bolted to the renderer — is the wrong shape twice over. It
would make text a special case of 3D geometry, which it is not; and it would put the compositor
inside the thing it is supposed to be able to replace. The base image has to be able to become a
video, a still, a shader or another composition later, and nothing above it should notice.

## Decision

A **layer stack** that sits above the finished frame and knows nothing about what made it.

```
Audio -> Analysis -> Signals -> Parameters -> 3D Scene -> 3D Render -> [2D Composition] -> Output
```

### The name

`avgen::scene::Composition` already exists and means the *3D* authoring tree — nodes flattened
into a `Scene`. A second `Composition` would be indistinguishable from it at a call site, and the
CLI already spends `--composition` on the first one. So the new type is
**`avgen::comp::LayerStack`**, which says what it is, and the word "composition" keeps its artist
meaning in the UI, in the project file (`"composition"`) and in the documentation. The namespace
is `comp`, and no header in `comp/` includes anything from `scene/`.

### The one hook

`rendering::FrameOverlay` — a single virtual call that `SceneRenderer::render` makes after the
tone map has written the target and before the frame timeline resolves. Six lines in
`scene_renderer.cpp` and a null pointer by default. That is the whole coupling. Because the hook
is inside `render()`, every path the renderer already has — the editor window, the offline job,
`renderToImage`, screenshots, projection outputs — gets the composition without any of them
knowing it exists.

### Colour: display-referred, after the tone map

The HDR chain is `RGBA16Float` end to end with AgX tone mapping, and exposure runs before bloom.
Compositing into that buffer would put every layer through AgX: `#FFFFFF` would come out a
desaturated off-white whose exact value depends on the scene's exposure that frame. An artist who
picks white and gets grey is an artist fighting the software.

So layers composite **after** the tone map, in display-referred space, with premultiplied alpha.
White is white, a 50% grey plate is a 50% grey plate, and a colour picked in the inspector is the
colour on the screen at every exposure. Nothing is tone mapped twice.

The cost of that choice, stated plainly: a layer cannot bloom, because bloom happens upstream of
where it is drawn. `FrameOverlay` is deliberately shaped so a second hook before the post chain
could add an HDR "emissive" route later for layers that want real bloom; v1 does not have one.
What v1 has instead is a per-layer **glow** taken from the same distance field the glyph is drawn
from, which costs no extra pass and is controllable, and which is what the look actually wanted.

EXR output is the scene-linear image from *before* the tone map and therefore does not contain
the composition. The render job says so out loud when a project with layers is rendered to EXR,
rather than shipping a sequence somebody discovers is missing its captions in a grade.

### Coordinates: normalised position, height-relative size

Two rules, because one rule cannot do both jobs.

* **Position** is normalised per axis, origin at the **bottom left**, y up. `(0.5, 0.5)` is the
  centre of the frame at 1280x720, at 3840x2160 and at any aspect ratio; "10% from the left, 90%
  from the bottom" is `(0.1, 0.9)` and stays there.
* **Size** — font size, shape extents, corner radii, stroke widths, tracking, glow — is in
  **height units**: a fraction of the frame's height, on *both* axes. A circle stays circular, a
  square stays square, and type keeps its proportion of the picture when the frame gets wider.

Normalising everything per axis turns circles into ellipses. Normalising everything by height
makes "centre" move when the aspect changes. This is the only combination that gets both.

Angles are degrees counter-clockwise about the anchor; the anchor is a fraction of the layer's own
box, so `(0.5, 0.5)` turns about its middle whatever the text says.

### Animation: the existing timeline, not a second one

Every animatable property is a registered `params::Parameter` under `layers/<id>/<property>`.
That is the entire mechanism. A layer therefore:

* keyframes on `params::Timeline`, the same timeline the camera and the scene are on, with the
  same interpolation modes, the same seconds-or-beats time base and the same evaluation;
* is a modulation target for the existing signal routes, so text scale follows the bass through
  the route system that already exists and **no text-specific audio reactivity was written**;
* appears in the parameter panel and in presets;
* is saved by the existing parameter serialiser as well as by its own block.

The registration happens in `Engine::installController`, alongside everything else that must
survive a scene swap, and **before** `rebind()`. That ordering is the whole point: a track naming
a parameter that does not exist yet binds to nothing and then does nothing, silently, which has
already cost this project two features (ADR-075, ADR-080). Deleting a layer now removes its
parameters *and* the tracks aimed at them, and `Timeline::unboundTargets()` puts whatever is left
unbound on screen instead of only in a log line.

The layer id, not its name, is in the parameter path, so renaming a layer cannot orphan a track.

### Text: outlines to a signed distance field, once

CoreText, because it is native, gives real system fonts and needs no third-party dependency.
Glyphs are rasterised from their **outline** (`CTFontCreatePathForGlyph` filled by CoreGraphics)
rather than drawn with `CTFontDrawGlyphs`: drawing goes through hinting, font smoothing and
subpixel positioning, all of which are tuned for a screen and vary with system preferences, and
an atlas that depends on the user's appearance settings is an atlas that breaks determinism.
Filling an outline is pure geometry.

The coverage bitmap is rasterised at 4x and turned into a signed distance field by an exact
Euclidean distance transform, with the *distances* averaged down rather than the coverage.
Averaging distance keeps corners to a quarter of a texel and still reconstructs a hard edge at
any magnification; averaging coverage gives an antialiased bitmap that goes soft the moment it is
scaled.

Fields are cached by face and glyph id — **not by size**. So a keyframed font size, or a scale
driven by the bass, re-uses the same texels and rebuilds nothing, and the outline, the glow and
the drop shadow all come out of that one field in the same pass.

Apple TBDR means `multisample.count = 1` everywhere; text edges come from the field, not from the
framebuffer, which is the other reason this is an SDF and not a bitmap.

### Fonts are referenced, never bundled

No font file is copied into this repository or into an exported bundle. A project stores family,
PostScript name, weight, slant and a declared fallback; the machine that opens it supplies the
face. When the requested face is not installed, the layer logs an **error** naming what was asked
for and what was used, and the inspector says so in red — because a render whose type silently
changed is a render whose determinism claim is false. This is honest, and it is the limit of the
font portability this version has: a project moved to a machine without the font will render with
different type, loudly.

### Cost

One pipeline, one pass, one storage buffer of per-frame styles, one shared atlas. Geometry is
built in the layer's own local units and cached; everything that animates lives in a 96-byte item.
Runs that are usually switched off — the drop shadow of a layer that has none — are packed at the
end of the vertex buffer, so skipping them does not break the contiguity of everything else and a
hundred plain text layers merge into **one draw call**.

Measured at 1920x1080 on the showcase machine: 1 layer 0.016 ms, 10 layers 0.016 ms, 100 layers
0.101 ms, 200 layers 0.219 ms of GPU pass time; per-frame CPU 0.005 ms at 100 layers. With no
layers the pass is not encoded at all.

## Consequences

- The 3D renderer is now one compositing source rather than the whole picture.
- `examples/composition/glowmere-lyrics.json` renders to the same sequence hash on repeated runs,
  and a frame taken through the offline job is byte-identical to the same frame taken through the
  interactive encode path.
- v1 ships Text and Shape. Image, Video, Shader and Nested Composition are absent, and the
  abstraction that would carry them — `Layer` with its geometry/item split — is the same one Text
  and Shape already use.
- A phrase with a start time and an end time is the natural unit of the data model, which is the
  shape an LRC / SRT / WebVTT import wants. That import is not written.
- Blend modes are Normal, Additive, Screen and Multiply: one blend state each, no extra pass.
- Layers do not bloom, EXR does not carry them, and there is no viewport direct manipulation,
  no masks, no parenting and no graph editor. All deliberate.
