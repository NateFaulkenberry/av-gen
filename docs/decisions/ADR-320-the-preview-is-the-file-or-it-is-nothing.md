# ADR-320: The preview is the file's own pixels, or it is nothing

Status: Accepted
Date: 2026-09-18

## Context

The Render panel could say how many frames a render had done and how fast, and nothing about what
was in them. The request was to be able to *see* the frames coming out.

There are two ways to answer that and only one of them is worth anything. The editor already
renders the scene continuously in the canvas, so the cheap answer is to point the canvas at the
render's current second and call that a preview. It would be wrong in every way that matters: a
different tier, a different clock, a different render extent, a different set of distance limits
(ADR-186 lifts them for a render and not for the viewport), and at bottom a different frame. The
whole value of watching a render is watching **what is going in the file**, which the live viewport
cannot show by construction.

## Decision

Tap the frames where they already exist in CPU memory: `RenderJob::handleFrame`, between the frame
hash and the `std::move` that hands the image to the encoder queue.

That point is forced, not chosen. Earlier in the function the EXR image is still the unresolved
supersampled intermediate (ADR-251), so a tap above `resolveToOutput` would show a frame twice the
size of the one written. Later the image has been moved into the encoder queue and `frame` is
empty. Between the two, the pixels are final, they are the encoder's, and `lastHash_` -- the
deliverable's own per-frame hash -- has just been computed from them, so the copy can carry the
hash of the frame it is a copy of. That is what makes "is this the frame in the file?" a question
with an answer rather than a matter of trust.

Four rules follow from "the render is the deliverable and this is a convenience":

1. **Off costs one relaxed atomic load.** `previewEnabled_` is checked in `handleFrame` and
   nothing else happens. Off is the default and it is what a headless `--render` always is.
2. **Point-sampled, not filtered.** An integer stride down to at most 480 px on the long axis. A
   box filter reads every source pixel -- 2,073,600 of them per frame at 1080p -- where this reads
   129,600, and the factor grows with the resolution because the output is capped. The cost is
   aliasing in a thumbnail, and the panel says so.
3. **Newest wins, one slot.** A queue would grow whenever the UI is slower than the render, which
   is every render. The copy is built outside the lock into a scratch buffer that keeps the last
   frame's allocation; the lock covers a swap of two vectors and four scalars. The panel reports
   how many frames went by unshown, because a number that is quietly dropping frames and a number
   that is quietly broken look the same.
4. **`capturePreview` takes its frame by const reference.** The obvious way to build the EXR
   preview without an allocation is to clamp and encode `frame.imageF` in place, and that image is
   the one the encoder is about to write. A const reference makes that a compile error instead of a
   code review.

## What it costs

24 frames at 1920x1080, minimum of 3 interleaved repeats, load average 44.7 (ADR-170; five other
agents were on the machine):

| | |
|---|---|
| source pixels per frame | 2,073,600 |
| pixels the tap reads | 129,600 (1 in 16, stride 4) |
| bytes copied per frame | 518,400 of 8,294,400 |
| render, preview off | 0.543 s |
| render, preview on | 0.549 s (+1.17%) |
| inside the tap | 0.190 ms per frame, 0.839% of the render |

The scene renders at 22.6 ms a frame, which is the pessimistic arm: the tap's cost is fixed per
frame and a real scene's frames cost several times that, so the share falls as the render gets
heavier. `RenderJob::previewSeconds()` exists because the alternative was subtracting two noisy
end-to-end wall times on a shared machine and calling the difference the answer.

## The float format, and where the preview stops being the file

For PNG and video the preview is a byte copy. `tonemap.wgsl` writes `linearToSrgb(mapped)` into an
RGBA8Unorm target, and `writePng` and the video writer are handed those bytes unaltered, so every
pixel in the panel is bit-identical to a pixel of the file.

An EXR has no display appearance to be faithful to: it is scene-linear float, and something has to
map it. The preview clamps to 0-1 and sRGB-encodes -- the tone mapper's `clamp` operator at
exposure 1, with no chroma retention, no vignette and no grain. It deliberately does **not**
reimplement ACES, AgX, Reinhard or Khronos Neutral: those are a GPU shader, and a second CPU copy
of them would be a preview free to drift from the picture it claims to be of, which is the one
failure this feature cannot survive. So the panel says, in the panel and not only in a document,
that the tone map is not applied and that this is what is in the file rather than what a graded
view of it looks like.

## ADR-250, and the check that would not have caught it

The instrument must not become something the render depends on, and that was proved rather than
asserted: the same project rendered twice, watched and unwatched, compared on the sequence hash, on
every per-frame hash, **and on the bytes on disk**.

The third of those is not belt and braces. Writing the in-place EXR conversion described above and
running the probe failed 20 assertions, every one of them the file comparison; the sequence hash and
all ten frame hashes matched exactly. They had to. The hash is taken in `handleFrame` *before* the
tap runs, so a tap that corrupts the image afterwards corrupts the file and the hash agrees with
it. **A determinism check upstream of the thing it is checking proves nothing about it.** Anything
tapped after the hash has to be checked against the artefact.

## What is not reproducible, and why it is not this

`--render-in-app` runs the same job the Render button does, and two runs of it on the same project
produce different sequence hashes -- measured: `82412c8c9f504c65` and `83ef7bcd356efb83` with the
preview off, before it was ever turned on. That is ADR-091's two-tier determinism, not a defect
here: an in-app render saves a snapshot of the *live* session, which has been running for a
variable number of frames with live audio and LFO state in it. So the application path cannot be
the instrument for a byte-identity claim; the fixed project file in `test_render_job.cpp` is.

## Two options this needed, and why they are not screenshot scaffolding

`--render-in-app <path>` starts the render in the window instead of headlessly, and
`--render-preview` forces the toggle on whatever the settings file remembers. The Render panel's
mid-render state -- the progress rows, the estimate, Cancel, and now this -- was reachable only by
a human clicking a button, so it could not be photographed, profiled, scripted or captured by
anything. That is ADR-262's shape exactly, and `--preview-mode` already carries the second
argument in its own comment: a capture or a benchmark has to be able to say which state it is in
rather than depending on how a machine's settings happen to be left.

## Consequences

- The toggle is `AppSettings::renderFramePreview`, application-global and persisted (ADR-225). Not
  in the project: opening someone else's project must not change how you are watching your own
  render, and the render's own settings must not grow a field about an instrument.
- One 512x512 RGBA8 texture, created once and written into in place for the life of the process.
  A texture per render would make ImGui's WGPU backend cache a bind group per texture id, and the
  only way to release those is `ImGui_ImplWGPU_InvalidateDeviceObjects`, which throws away the
  pipeline and the font atlas with them. 480 caps the preview, so every output shape fits a corner
  of one texture and the panel is given the uv.
- A render that ends, fails or is cancelled leaves its last frame on the panel, labelled as the
  last frame it wrote. A still picture cannot tell you which of those happened and a stale frame
  under a stopped progress bar is the exact lie a preview exists not to tell. Starting a new job
  clears it before the first frame of the new one arrives.
