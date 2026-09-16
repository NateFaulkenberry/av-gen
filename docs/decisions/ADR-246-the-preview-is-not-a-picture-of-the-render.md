# ADR-246: The preview is not a picture of the render, it is the render

**Status:** Accepted
**Date:** 2026-09-16
**Context:** The output-preview and canvas-integration specification
**Follows:** ADR-076 (the editor shell and its canvas), ADR-084 (the canvas render scale),
ADR-092 (the world editor draws over the world, never into it), ADR-186 (offline lifts the distance
detail limits, live playback does not), ADR-212 (an offline render may spend pixels), ADR-225 (a
setting the application does not keep is not a setting), ADR-170 (one GPU, one run), ADR-182 (a
probe must be shown capable of failing)

## Context

The canvas expands into whatever the editor's centre dock happens to be. That is the right
behaviour for authoring and it is the wrong behaviour for judging a shot, because the canvas's
aspect ratio is not the deliverable's. On this machine, with panels open on both sides and the
sequencer up, the centre dock is about **1.50:1**. A 1920x1080 deliverable is **1.78:1**. Those are
not the same picture: `Camera::projection(aspect)` is `glm::perspectiveRH_ZO` on a **vertical**
field of view, so aspect widens the frustum horizontally and leaves the vertical extent alone. A
narrower canvas therefore shows *less to the left and right* than the render will, at the same
height. Everything an artist put near the edges was invisible until the render came back.

Three facts from the code decided the shape of the answer, and all three are worth stating because
each one removed a design that would otherwise have looked reasonable.

**1. Aspect ratio enters the engine in exactly one place, and it is an extent.** The renderer is
never told an aspect ratio. `SceneRenderer` computes `aspect = hdr_.width() / hdr_.height()` from
its own HDR target, and `Composition::setViewport(w, h)` takes the same pair. Both are fed from
`Application`'s `renderWidth_` / `renderHeight_`, which until now were the canvas's pixels. So
"make the preview show the deliverable's framing" is not a new feature at all: it is *changing one
pair of integers*.

**2. There is no camera to choose.** This engine has one camera — `scene::Scene::camera` — rewritten
every frame by `Composition::applyParameters` from the `camera/*` parameters. There is no camera
list, no camera entity, no selection. Both shot systems (`app::Sequence::toTimelineTracks` and
`seq::Sequence::bake`) *bake* their cameras down to keys on `camera/position` and `camera/target`.
`scene::Scene::cameras` exists but is a one-shot copy at glTF import and is cleared outright by
`Composition`. A directed shot and a hand-flown viewport therefore differ in **what writes the
parameters**, never in which camera is read. The offline job proves it from the other end:
`RenderJob::renderOne` renders `engine_->scene()` — the same object, through the same
`SceneRenderer::render`.

**3. The canvas is already an offscreen texture in an ImGui window.** `drawCanvasWindow` has always
done `ImGui::Image(finalView_, contentRegion)`. The scene never touches the swapchain; the swapchain
gets a clear and the ImGui pass. Letterboxing is therefore a matter of where the `Image` is placed,
not a new presentation path.

## Decision

**The preview renders at an extent carrying the output's aspect ratio, and everything else follows.**

Three view modes on one canvas. `Workspace` is byte-for-byte the editor that existed before this —
the extent is the canvas's, and it is the default in a fresh `PreviewViewState`, so nobody who never
asked for this feature has their editor changed. `Output Frame` and `Output Preview` compute the
frame's geometry from the canvas and the project's output size, and hand the resulting extent to
`renderer_->resize` and `engine_->setViewport`.

That one substitution is the whole of the correctness claim. The preview's projection is not
*similar to* the render's, it is built by the same function from the same camera at the same
aspect ratio. Nothing is stretched, nothing is cropped, and the only thing that differs is how many
samples the picture is taken at — which the toolbar reports rather than hides.

### What was NOT built, and why

**Overscan.** The spec's §4.2 and §4.4 ask for the surrounding scene to remain visible outside the
output frame ("the surrounding area is workspace, not output"). This is not implemented and the
outside of the frame is editor chrome — plain, dimmed or filled.

It was rejected on evidence rather than on effort. Showing real scene outside a correctly-framed
rectangle requires rendering a *wider* frustum than the deliverable's and drawing a guide inside it.
Because the vertical FOV is fixed, that is only free when the canvas is **wider** than the output,
and the editor's centre dock is narrower (1.50 against 1.78) in the ordinary layout — so the free
version would have been wrong in the common case. Doing it properly means scaling the camera's
effective vertical FOV by the fit factor, which means writing to `scene_.camera` *after*
`applyParameters` has derived it — precisely the derived-copy antipattern this project has an
ADR-shaped scar about — and it would feed a different frustum to the froxel grid, the terrain LOD
and the importance selector. And the picture inside the guide would then only be *framing*-exact:
bloom, vignette and defocus are all resolution- and frame-dependent, so the thing labelled "this is
your shot" would not be your shot. A preview that is 95% honest is the failure this feature exists
to end. It is recorded as not done rather than half-built.

**A "final preview" quality rung.** The spec's §7.3 suggests one. `SceneRenderer::setQuality(tier)`
*replaces the whole `QualitySettings` object*, so a toolbar that switched tiers would silently
discard any quality arm or custom setting in force. Three rungs are offered instead and every one of
them is a real render-target extent (`Draft`, `Realtime`, `Output resolution`), which satisfies the
rule that actually matters: "do not present a quality option that merely changes a label."

### The two ways the preview still differs from the deliverable, said out loud

Neither is fixable from the editor and both are in the toolbar's tooltip rather than in a footnote:

- **ADR-212.** An offline render may supersample. `previewRenderExtent` clamps its scale to 1.0 and
  never goes above the output's own pixel count — an interactive preview that started supersampling
  because a zoom control was dragged would be a surprise, not a feature.
- **ADR-186.** An offline render lifts the distance detail limits; live playback does not. A wide
  shot genuinely has more in it in the deliverable. The existing `--viewport-matches-render` flag
  (and its "viewport matches the render" checkbox) is the lever for that and the tooltip names it.

### Ownership

| what | where | why |
|---|---|---|
| output width, height, fps | `app::RenderSettings`, project JSON `"render"` | it already lived there; §5.1's "do not introduce a second incompatible configuration" is satisfied by *not writing one* |
| output aspect ratio | derived, never stored | an aspect that can disagree with the pixel dimensions is one that will |
| view mode, zoom, guides, dimming, quality, toolbar state | `app::AppSettings`, `<prefs>/settings.json` under `"outputPreview"` | it describes how *this person on this machine* is looking at the piece; opening someone else's project must not change it |
| pan, fullscreen | deliberately not persisted | a pan only means anything against the canvas size it was made at; a session that exits in fullscreen must not reopen with every panel closed and no record of which ones they were |
| undo | nothing new | `EditHistory` covers scene-graph edits only, and render settings have never been on it. A view toggle creating an undo entry would be the novelty here, not its absence |

### The coordinate answer (§10.2), in one substitution

Every world-to-screen conversion in the editor goes through `toScreen(canvas, ndc)` and every
screen-to-world through `editorInputFromImGui(canvas)`. Both take a `CanvasRect`. They are now handed
`frame.asCanvasRect(...)` instead of the window's rect — one line, in `drawViewportEditor`. In
Workspace mode the frame *is* the window, so nothing changes. This is why letterboxing cannot
introduce a gizmo offset: there is no second conversion to keep in step.

## What was measured

Hardware: Apple M2 Max, Metal via Dawn. Everything GPU under `tools/gpu-lock.sh` (ADR-170).

### The claim, on the device

`tests/rendering/test_output_preview_gpu.cpp` renders one scene three ways and compares on a common
64x36 grid (mean absolute difference per channel, 0..1):

| arm | extent | difference from the 1920x1080 deliverable |
|---|---|---|
| the preview, sized by the production code from a 600x400-point canvas at 2x | 1200x676 | **0.0030** |
| **control: the canvas's own aspect ratio** — what this editor rendered at before | 1200x800 | **0.0894** |

**30x.** The assertion is on the ratio between the arms, not on either number alone, because a
similarity threshold with nothing to compare it against passes whatever the code does. At the
`Output resolution` rung the difference is **0.000** — the preview is the deliverable.

### The claim again, on a real piece

Glowmere Valley 2, six frames at t = 4..9 s, against its own deliverable (1280x720, tier offline,
limits unlimited, supersample 2.0 — what `--render` actually produces for this project). Rendered
through the queue under the lock; `renders/output-preview/MEASUREMENTS.txt`.

| arm | extent | tier / limits / supersample | mean diff vs the deliverable |
|---|---|---|---|
| **D** the preview's extent | 1200x676 | the deliverable's | **0.00182** |
| **E** the canvas's aspect | 1200x800 | the deliverable's | **0.04118** |
| **B** the preview as it really runs | 1200x676 | realtime / live / 1.0 | 0.01200 |
| **C** the old behaviour as it really ran | 1200x800 | realtime / live / 1.0 | 0.04278 |

D against E isolates framing — same tier, same limits, same supersample, and the extent's aspect
ratio is the only thing that moves. **22.6x.** B against D prices the two differences this ADR says
it cannot remove: living with ADR-186's detail limits and without ADR-212's supersampling costs
0.01018, which is *most* of what separates the real preview from the real deliverable. That is the
useful shape of the result: what is left between them is detail and sampling, which the toolbar
names, and not framing, which it no longer has to.

Two 30-second movies are rendered from the same project for eyes rather than arithmetic —
`A-deliverable-1280x720.mov` (sequence hash `5be5207682ca025f`) and
`B-preview-extent-1200x676.mov` (`f0feaaabfbdb2a45`), both with audio muxed.

### The feature, end to end, through the real editor

`--preview-mode` exists so the mode can be entered by something other than a hand on a toolbar. The
same window, the same scene, the same scripted interaction, 120 frames each, differing only in that
flag — and the extent the editor reports is readable without seeing the editor:

| `--preview-mode` | the canvas reported | aspect |
|---|---|---|
| `workspace` | 1242x952 px | **1.305:1** — the canvas's own shape, as before |
| `outputFrame` | 1242x698 px | **1.779:1** |
| `outputPreview` | 1242x698 px | **1.779:1** |

The project's output is 1920x1080 = 1.7778:1, so both frame modes land on it to within the one pixel
of even rounding. 0 GPU errors and 0 logged errors in all three. The `workspace` row is the control:
the probe can tell the modes apart, and the mode nobody opted into is untouched.

### The probes, shown capable of failing (ADR-182)

Seven arms, each a plausible wrong implementation, each rebuilt and run:

| arm | probe | result |
|---|---|---|
| render extent sized from the canvas, not the output | "the preview renders at the output's aspect ratio, never the canvas's" | FAILED |
| frame placed at the canvas origin instead of centred | "the frame is centred in the canvas wherever the canvas is" | FAILED |
| extent rounded to the nearest pixel, not the nearest even pixel | "the render extent is even in both axes" | FAILED |
| resolution validated in the 32-bit type the dimensions arrive in | "a pixel count that has already overflowed 32 bits is still refused" | FAILED |
| safe areas inset by a fraction of the long side | "safe areas are the same fraction of each axis in every format" | FAILED |
| aspect not re-derived when a device budget clamps one axis | "a budget that cannot hold the output still holds its shape" | FAILED |
| the view state left out of the settings document | "the output preview's view state survives a save and a load" | FAILED |

None of the seven still passed. The even-extent one is the least obvious and the most load-bearing:
`SceneRenderer::resize` rounds each axis to even before allocating the HDR target and takes the
projection's aspect from *that* target, so an odd extent is silently nudged by a pixel — and a pixel
of nudge is a different framing from the one the preview is claiming to show.

### Suites

| suite | before | after |
|---|---|---|
| unit | 1854 cases / 1,934,850 assertions, 3 skipped | **1884 cases / 1,935,265 assertions, 3 skipped** |
| render | 298 cases / 410,033 assertions, 1 skipped | **301 cases / 410,167 assertions, 1 skipped** |

### What each number would have been if the change were wrong

If the extent had kept coming from the canvas, the GPU framing probe would read **0.0894** instead
of **0.0030** and the ratio assertion would fail outright — that arm was built and run, it is the
control row above. If the coordinate conversion had kept dividing by the window rather than the
frame, the centring probe fails (arm 2) and, on screen, every gizmo handle in Output Frame mode
would sit off by half the letterbox — 0 points in Workspace and up to 94 points at the top and
bottom of a 1.50:1 canvas showing a 1.78:1 frame. If the view state were not persisted, the
round-trip probe fails (arm 7) and the mode would reset to Workspace on every launch.

## Consequences

**The editor is unchanged until someone asks.** `PreviewViewState` defaults to `Workspace`, fit zoom,
guides off. A settings file written before this change loads onto those defaults rather than being
refused, and there is a test for that specifically because it is the one that decides whether this
can ship.

**One real defect was fixed on the way past.** The Render panel's size control was a bare
`InputInt2` clamping silently to `[2, 16384]`: typing 1 gave you 2 and typing 20000 gave you 16384,
with nothing said either way. Both surfaces now go through `drawOutputResolutionControls`, which
rejects with a message, names the **adapter's** reported `maxTextureDimension2D` rather than a
compiled-in constant, and keeps the last valid size.

**The preview costs a preview's pixels.** `Draft` and `Realtime` size from the frame as displayed,
so a small frame is a small render; only `Output resolution` allocates the deliverable's extent.
Nothing draws the world twice — there is one `SceneRenderer` with one HDR target, and the preview is
that render at a different extent, not a second one.

**`canvasRenderScale` does not compound with the preview's quality rung.** ADR-084's scale is
"soften the workspace to keep the frame rate up" and it is deliberately not applied while a frame is
on screen: two multiplying scales would make the extent the toolbar reports a lie.

**What is left undone, and named rather than implied.** Overscan (above). A gizmo drawn in the
letterbox is clipped to the canvas window as before, so an object off the side of the frame can be
selected and moved but its outline stops at the canvas edge — correct, and worth knowing. And the
one thing this ADR cannot claim: **none of the ImGui surface has been seen.** The toolbar, the
letterbox, the dimming, the guides and the fullscreen gesture are asserted by construction and by
the geometry tests underneath them, and by nothing else. The framing claim is measured on the
device and does not depend on that; the appearance claims do.
