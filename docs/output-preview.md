# The output preview

How to see what the render will produce, without leaving the canvas.

This describes what is **implemented and reachable**. Where something is deliberately not done, it
says so and says why. The reasoning behind it is
[ADR-246](decisions/ADR-246-the-preview-is-not-a-picture-of-the-render.md).

Related: [rendering.md](rendering.md) for the offline render itself, and
[world-editor.md](world-editor.md) for everything you do *to* the world rather than *look at* it.

---

## The problem it solves

The canvas grows into whatever space the editor's centre dock has. That is what you want while you
are building a world, and it is not what you want while you are judging a shot, because the canvas's
shape is not the video's shape.

This matters more than it sounds. The camera's field of view is **vertical**, so changing the aspect
ratio changes what you see to the left and right and leaves the top and bottom exactly where they
are. With panels open on both sides and the sequencer up, the canvas is about 1.50:1 and a 1080p
deliverable is 1.78:1 — so the canvas was showing you **less** than the render would, and you found
out when the render came back.

---

## The three view modes

The selector is at the top left of the canvas. It is the only control visible until you leave
Workspace.

**Workspace** — the canvas you have always had. The world is rendered at the canvas's own size and
fills it. This is the default, and a fresh install or an old settings file lands here.

**Output Frame** — the world is rendered at the *output's* shape and placed inside the canvas.
Everything you can do in Workspace you can still do: selection, gizmos, the ghost, camera
navigation, the context menu. This is the mode to author a shot in.

**Preview** — the same frame with the editor's overlays out of the way. The gizmos and outlines stop
drawing; navigation and selection still work. Use it to look at the picture.

> **What "the output's shape" means.** In Output Frame and Preview the renderer is given a render
> target whose aspect ratio is the output's, and the camera's projection is built from that target.
> The framing in the frame is not an approximation of the render's framing — it is the render's
> framing, produced by the same code from the same camera. Nothing is stretched to fit.

---

## The output resolution

The size selector in the toolbar edits **the project's render settings** — the same `width` and
`height` the Render panel edits and the same ones `--render` uses. There is one output
configuration, not a preview-shaped copy of one.

Presets: Full HD 1920x1080, 4K UHD 3840x2160, HD 1280x720, Vertical 1080x1920, Square 1080x1080.
Below them is a custom width and height.

The aspect ratio beside it is **derived** and is not separately editable. An aspect ratio that can
disagree with the pixel dimensions is one that eventually will.

A custom size is validated before it is applied, and a rejected one is *rejected* — it never reaches
the render settings, so no GPU resource is allocated for it and the last size you had keeps
rendering. The message says what went wrong. The per-axis maximum quoted is your adapter's own
reported `maxTextureDimension2D`, not a number compiled in.

---

## Zoom, and what it does not do

**Fit** scales the frame to the canvas; **25% / 50% / 100% / 200%** are fixed scales, where 100%
means one output pixel per screen pixel. On a Retina display that is half a point, so a 1920-wide
frame at 100% is 960 points across.

Zoom changes **only how large the frame is displayed**. It does not change the output resolution, it
does not change the camera's projection, and it does not touch the scene. If the frame is larger
than the canvas you can drag it; the drag is bounded so the frame can never be thrown off screen.
Returning to Fit always re-centres, whatever pan you left behind.

---

## Preview quality

A separate question from zoom: *how many pixels the frame is rendered at*, as against how large it
is shown.

| rung | what it renders at |
|---|---|
| **Draft** | half the displayed frame's screen pixels in each axis |
| **Realtime** | one render pixel per displayed screen pixel |
| **Output resolution** | exactly the configured output size, displayed scaled |

Every rung is a real render-target extent at the output's aspect ratio. None of them is a label, and
none of them reduces draw distance, object counts or visual quality to hit a number. The toolbar
always says which extent is in force, and colours it as a warning whenever it is below the output's
own size.

Use **Output resolution** to inspect anything that depends on the output's pixel count — grain,
sharpening, fine foliage, thin geometry.

---

## Guides

All of them are editor overlays. They are drawn into the canvas window after the picture and they
cannot reach a render: the offline renderer loads the project from file in its own engine and never
runs any of this code.

- **safe** — title-safe and action-safe boxes, SMPTE ST 2046-1 / EBU R 95: the centred 93% of each
  axis for action, 90% for title. A fraction of *each axis* rather than a fixed inset, which is what
  keeps them correct for portrait and square as well as landscape.
- **thirds** — the rule-of-thirds grid.
- **centre** — a small centre crosshair.
- The frame's own border is always drawn, dark under light, so it is findable over a bright sky and
  over a dark forest alike.

**show / dim / hide** controls the canvas outside the frame.

---

## Fullscreen preview

The **fullscreen** button closes every open panel and leaves the canvas the whole window; leaving
puts back exactly what was open. **Esc** also leaves.

It is a view-state change inside the existing window, not an OS fullscreen mode — the dock tree, the
project, the transport and the selection are all untouched, so there is nothing to lose. Entering it
from Workspace arrives in Preview, because a frame with nothing else on screen should be the
deliverable's shape rather than the canvas's at a larger size.

It is deliberately **not** remembered across launches: a session that ended in fullscreen must not
reopen with every panel closed and no record of which ones they were.

---

## Which camera you are looking through

The indicator at the right of the toolbar says `shot camera` or `viewport camera`.

There is exactly one camera in this engine — the scene's — and the shot system drives it by writing
keys on `camera/position` and `camera/target`. So the preview, the viewport and the deliverable
cannot be looking through different cameras; there is only the one to look through. What the
indicator tells you is *what is driving it*: the timeline's baked shot keys, or your own navigation.

Everything follows from that. The preview reflects the playhead because the scene does; scrubbing,
playing and pausing move it because they move the one timeline; a shot transition is reflected
because the transition is in the keys. There is no second clock and no second world.

---

## The two things the preview cannot show you

Both are named in the toolbar's tooltip rather than buried here, and neither is a defect.

**An offline render may supersample** ([ADR-212](decisions/ADR-212-an-offline-render-may-spend-pixels.md)).
Glowmere's project asks for 2x. The preview never supersamples — a preview that started spending
four times the pixels because a zoom control was dragged would be a surprise, not a feature. So the
deliverable is finer than the preview, always.

**An offline render lifts the distance detail limits and live playback does not**
([ADR-186](decisions/ADR-186-an-offline-render-lifts-the-limits-playback-needs.md)). A wide shot genuinely has
more in it in the deliverable: real geometry in the far field instead of billboards, distant
characters posed every frame, nothing frozen past 120 m. To put the viewport on the render's terms,
use **Render → "viewport matches the render"** (or `--viewport-matches-render`). It costs frame rate;
that is the trade.

Measured on Glowmere Valley 2 against its own 1280x720 deliverable, over six frames, as mean
absolute difference per channel on a common grid:

| | mean difference |
|---|---|
| the preview's extent, at the deliverable's tier and supersampling | **0.00182** |
| the canvas's own aspect ratio, same tier and supersampling | **0.04118** |
| the preview as it really runs (live tier, no supersample) | 0.01200 |

Framing alone accounts for a **22.6x** gap. What is left between the real preview and the real
deliverable is almost entirely the two items above.

---

## What is not done

**You cannot see the scene outside the frame.** The area outside the output frame is editor chrome —
plain, dimmed or filled — not surrounding world. Showing real scene out there means rendering a
wider camera than the deliverable's, and that would make the picture inside the guide only
*approximately* your shot: bloom, vignette and defocus all move with the frame. The reasoning is in
[ADR-246](decisions/ADR-246-the-preview-is-not-a-picture-of-the-render.md); it is recorded as not
done rather than half-built.

---

## Where the settings live

| setting | file | why |
|---|---|---|
| output width, height, frame rate | the **project**, under `"render"` | it is a property of the piece |
| view mode, zoom, quality, guides, dimming, toolbar state | `<prefs>/settings.json`, under `"outputPreview"` | it is how *you on this machine* are looking at the piece; opening someone else's project must not change it |
| pan, fullscreen | nowhere, on purpose | a pan only means anything against the canvas it was made at |

Changing the output resolution is a settings change and follows the existing settings conventions —
it does not go on the undo stack, and neither does any view toggle. Moving an object while the
output frame is visible is an ordinary scene edit and undoes exactly as it always did.

---

## Driving it without a mouse

`--preview-mode workspace | outputFrame | outputPreview` opens the canvas in that mode, outranking
whatever the settings file remembers. It exists so the preview can be driven by the
scripted-interaction driver (`--ui-script`) and compared in a benchmark arm — a UI mode that can
only be entered by hand is a UI mode nobody measures.

The canvas logs the extent it rendered at, which is how you check the mode did what it says:

```sh
./build/release/src/avgen --example Hyperspace --preview-mode outputFrame \
  --ui-script panels,tabs,scrub --frames 120 --size 1200x800 | grep "world rendered"
```

| mode | extent | aspect |
|---|---|---|
| `workspace` | 1242x952 | 1.305:1 — the canvas's own shape |
| `outputFrame` | 1242x698 | 1.779:1 — the project's 1920x1080 output |
| `outputPreview` | 1242x698 | 1.779:1 |
