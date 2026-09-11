# The 2D composition

Decision: ADR-081. Model in `src/comp/`, GPU in `src/rendering/composition_renderer.cpp` and
`shaders/composite.wgsl`, editor in `src/ui/composition_panel.cpp`.

The 3D render is **one compositing source**, not the whole picture. Above it sits an ordered stack
of 2D layers — text today, shapes today, images and video later — drawn into the tone-mapped
frame. The compositor knows nothing about the scene behind it.

```
Audio -> Analysis -> Signals -> Parameters -> 3D Scene -> 3D Render
                                                       -> 2D Composition -> Display / Video
```

## The authoring loop

Open **Composition** (right-hand dock, open by default).

1. **+ Add Layer -> Text.** A layer appears with a default string, starting at the playhead.
2. Type into the text box. The picture updates as you type.
3. Drag **position** to place it. `(0.5, 0.5)` is the centre of the frame at every resolution.
4. Pick a font — the combo filters as you type, because a Mac has several hundred families.
5. Move the playhead. Click the **key dot** to the left of any row. That records a key for that
   parameter at the playhead, on the project's own timeline.
6. Move the playhead again, change the value, click the dot again.
7. Play.

A lit key dot means the parameter is already automated; the slider beside it is then the *base*
value, and the timeline is writing over it every frame (ADR-018).

## Coordinates

Resolution independent, and deliberately two rules:

| what | units | why |
|---|---|---|
| `position` | normalised per axis, origin **bottom left**, y up | `(0.5, 0.5)` is the centre at any size or aspect; `(0.1, 0.9)` is "10% from the left, 90% from the bottom" and stays there |
| `size`, `scale`, `cornerRadius`, `strokeWidth`, glow, tracking | **height units** — a fraction of the frame height, on both axes | a circle stays circular and a square stays square when the frame gets wider |
| `rotation` | degrees, counter-clockwise, about the anchor | |
| `anchor` | fraction of the layer's own box; `(0.5, 0.5)` is its middle | what it turns and scales about |

Text size is per em: `size = 0.09` means one em is 9% of the frame height. Tracking, outline
width, glow radius and shadow offset are in **em**, so they follow the type when it resizes.

## The layer model

Every layer has: `id`, `name`, `enabled`, `blend`, `start`, `end`, `position`, `scale`,
`rotation`, `anchor`, `opacity`, `color`. A layer is drawn when it is enabled and
`start <= t < end`; an `end` at or before `start` means "until the end".

`enabled` is the eye toggle and is the only visibility flag — a separate `visible` would be the
same boolean twice, and whether a layer is *currently* on screen is a computed thing
(`Layer::liveAt`), not a second field to keep in sync.

**Text** adds: content, font (family / PostScript name / weight / italic / fallback), `size`,
`align`, `tracking`, `lineSpacing`, `outlineWidth` + `outlineColor`, `glow` + `glowColor`,
`shadowOpacity` + `shadowOffset`.

**Shape** adds: `shape` (rectangle / ellipse / line), `size`, `cornerRadius`, `strokeWidth` +
`strokeColor`, `glow` + `glowColor`. A rectangle with a transparent fill and a stroke is a border,
which is the commonest thing a composition wants from a shape.

**Blend modes:** `normal`, `add`, `screen`, `multiply` — one blend state each, no extra pass.

Compositing order is list order: index 0 is drawn first, nearest the 3D render. The Layers panel
shows the list top-down, so the top row is the top of the stack. Drag a row to reorder it.

## Animation and modulation

There is no second animation system. Every animatable property is a registered parameter:

```
every layer   layers/<id>/{position, scale, rotation, anchor, opacity, color}
text          layers/<id>/{size, outlineWidth, outlineColor, glow, glowColor,
                           shadowOffset, shadowOpacity}
shape         layers/<id>/{size, cornerRadius, strokeWidth, strokeColor, glow, glowColor}
```

The things that are **not** parameters are the things that rebuild geometry: the words, the face,
the alignment, the tracking, the line spacing, the shape kind, and the layer's in and out times.
They are authored, saved and undoable, but they do not keyframe. A title that spreads its letters
apart is `size`, not `tracking`.

Which means, with no extra code anywhere:

* **Keyframes.** `params::Timeline` tracks name these paths like any other. Interpolation is the
  existing set: `step` (hold), `linear`, `easeIn`, `easeOut`, `easeInOut`, `smooth`, `bezier`.
  Time base is seconds or beats.
* **Modulation.** A route from `audio.bass` or `music.beat` to `layers/3/scale` makes text pulse
  on the beat. This is the ordinary route system; nothing text-specific exists.
* **Presets, the parameter panel, projects.** All of it, for free.

The path uses the layer's **id**, not its name, so renaming a layer never orphans a track.
Deleting a layer removes its parameters and the tracks aimed at them. Tracks left naming a
parameter this build does not have are listed at the top of the Composition panel — silence there
is what made two earlier features do nothing without saying so (ADR-075, ADR-080).

## Colour

Layers composite **after** the tone map, in display-referred space, with premultiplied alpha. So
`#FFFFFF` is white, a picked colour is the colour on screen, and nothing is tone mapped twice.

The cost: a layer cannot bloom, because bloom happens upstream of where layers are drawn. Use the
per-layer `glow` instead — it comes out of the same distance field the glyph is drawn from, costs
no extra pass, and is controllable. EXR output is the scene-linear image from before the tone map
and therefore does **not** contain the composition; the render job warns when you ask for that
combination.

## Text rendering

CoreText shapes each line (so kerning and ligatures are the platform's) and gives glyph outlines.
Each glyph's outline is filled by CoreGraphics at 4x and turned into a signed distance field by an
exact Euclidean distance transform, with the distances averaged down. Fields live in one shared
2048x2048 R8 atlas, keyed by face and glyph id — **not by size**.

So one rasterisation serves every size: an animated font size costs a 96-byte upload and rebuilds
nothing. The fill, the outline, the glow and the drop shadow all read that one field in one pass.

Geometry is rebuilt only when the content, the face, the alignment, the tracking or the line
spacing changes. Everything else is per-frame item data.

### Fonts are referenced, never bundled

No font file is in this repository. A project stores a family and a PostScript name; the machine
that opens it supplies the face. If it is not installed, the log says so at error level and the
inspector says so in red, naming what was asked for and what was used. **Font portability is not
solved**: a project moved to a machine without the font renders with different type. It just does
not do it quietly.

## Realtime and offline

`SceneRenderer::render` calls the overlay once, after the tone map and before the frame timeline
resolves. Every path through the renderer therefore gets the composition: the editor window, the
offline render job, `renderToImage`, screenshots, projection outputs.

The composition is evaluated at `Engine::timelineClock().seconds` — the same clock the timeline
and the cues use — in both the live loop and the render job. Nothing is driven by a wall clock, a
frame counter or a delta accumulation, so frame *f* is the same frame however it was reached.
Verified: a frame produced by the real `RenderJob` is byte-identical to the same second produced
through the interactive encode path
(`tests/rendering/test_composition_gpu.cpp`), and the showcase renders to the same sequence hash
on repeated runs.

## Cost

One pipeline, one pass, one atlas, one storage buffer. Adjacent layers that share a blend mode and
whose geometry is contiguous merge into a single draw, so a hundred plain text layers are one
draw call. Runs that are normally off (the shadow of a layer that has none) are packed at the end
of the vertex buffer so that skipping them does not break that contiguity.

Measured at 1920x1080 (`avgen_render_tests "[composition][performance]"`):

| layers | GPU pass | per-frame CPU | draws | items | glyphs |
|---:|---:|---:|---:|---:|---:|
| 0 | pass not encoded | 0.000 ms | 0 | 0 | 0 |
| 1 | 0.016 ms | 0.001 ms | 1 | 2 | 11 |
| 10 | 0.016 ms | 0.001 ms | 1 | 20 | 11 |
| 100 | 0.101 ms | 0.005 ms | 1 | 200 | 11 |
| 200 | 0.219 ms | 0.007 ms | 1 | 400 | 11 |

Cold geometry (shaping and rasterising from nothing) is authoring-time: 9 ms the first time a
face is touched in a process, then 0.37 ms for a hundred layers.

## Serialisation

The project document gains an optional `"composition"` block with its own version. The envelope
version is unchanged at 4, because a project written before this existed simply has no such key
and needs no migration. See `docs/project-format.md`.

The block is read *before* the `"parameters"` block, so `layers/3/opacity` exists when its saved
value arrives, and before the timeline binds.

Layer values live in two places — the block and the parameter table — and are kept in agreement by
pulling the parameter bases back into the authored fields before every save.

## Extension points

* **Another layer kind.** Derive from `comp::Layer`: implement `buildGeometry` (local units,
  cached), `buildItems` (per frame, 96 bytes each), `unitScale`, `boxLocal`, the five small
  protected hooks and JSON. Add an `ItemShader` value and a branch in `composite.wgsl`, or sample
  a texture the layer owns. Image and Video are this plus a texture binding.
* **Another compositing source.** `Layer 0` of the picture is whatever wrote the target before the
  overlay runs. Replacing it with a video, a still or a nested composition needs nothing here.
* **HDR / bloom for layers.** Add a second `FrameOverlay` call before the post chain and an
  emissive route per layer. The hook is shaped for it; v1 does not have it.
* **Timed text import.** A phrase with a start and an end is already the unit of the model, which
  is what LRC / SRT / WebVTT / CSV want. Not written.
