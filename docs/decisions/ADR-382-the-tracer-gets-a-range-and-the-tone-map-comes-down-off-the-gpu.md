# ADR-382: The tracer gets a range, and the tone map comes down off the GPU

Status: accepted
Date: 2026-09-19
Branch: `agent/mbackend2`
Relates to: ADR-020 (render jobs, native video), ADR-039 (AgX the default), ADR-351 (the path
tracer and its naming), ADR-366 (`PathTraceSettings`), ADR-372 (AgX's wrong inverse, and the mirror
that carried it), ADR-182 (a probe that cannot fail proves nothing),
`docs/offline-backend-audit.md` steps 5, 6 and the R1 decision

*Written as 380 and renumbered to 382: two other branches staged 380 and 381 while this was in
flight. Seventh collision this session.*

## What the audit found, and what this does about it

Three of that document's findings, taken together because they are one thread:

* **W2** — the output layer is *already* renderer-agnostic. `assets::VideoWriter` and
  `assets::writeExr` take pixel spans and know nothing about who made them. What is welded to the
  rasteriser is the loop *above* them.
* **W4/R1** — AgX, the ACES fit, Reinhard, Khronos PBR Neutral and clamp existed only in
  `shaders/tonemap.wgsl`, and `render_job.cpp` refused to write a second copy. The consequence
  nobody had written down: **a path-traced frame could not become a video frame without a GPU**,
  which spends the exact property that makes the tracer usable on a machine three agents share.
* The Render panel said, in as many words, *"One frame, not a sequence -- a path-traced sequence is
  a queue of these and is not built yet."*

## 1. The frame-range driver

`src/app/frame_range.{hpp,cpp}`. No GPU, no ImGui, no renderer.

`FrameRange` is the arithmetic: resolve an end, count frames, name the time of frame f. The end is
**exclusive**, which is where every off-by-one in a render lives. `RenderSettings::frameCount` and
`::resolvedEnd` now delegate to it rather than carrying a second copy — the behaviour is unchanged
and the assertions in `test_render_settings.cpp` that predate this are what say so.

`FrameSequenceDriver` is the loop: bounded stepping with the same return-true-on-complete contract
`RenderJob::step` established (the editor's frame loop is written against it), cancellation that
**keeps** partial output, the FNV-1a hash chain in frame order, honest progress, and a bounded
producer/consumer with real backpressure.

A renderer meets it as `FrameSource` — `begin`, `submit`, `collect`, `end` — and that is the whole
interface.

### What this deliberately does NOT do

**`app::RenderJob` is not rebuilt on top of it, and that is a decision rather than an omission.**
Twenty GPU tests pin its behaviour — `test_render_job.cpp` alone pins the exact `step()` contract,
the readback ring's hash agreeing frame-for-frame with the synchronous path, AOVs staying out of the
hash chain, and the ADR-320 preview arriving after `resolveToOutput`. Every one of them needs an
adapter, the GPU lock and a quiet machine, and the machine has two other agents on it finishing the
Tree of Life.

Rewriting the rasteriser's sequencing without being able to run those tests would be exactly the
refactor the audit warned against starting on an unverified assumption. So the driver is **used by
the new capability and shares the arithmetic with the old one**, and `RenderJob`'s adoption is
recorded as owed work with the reason it is owed. The duplication that remains is the loop; the
duplication that mattered — two independent ideas of what frames a range contains — is gone.

## 2. The tone map, on the CPU, anchored against something that is not itself

`src/scene/tonemap.{hpp,cpp}` — the five operators, `retainChroma`, the vignette, the sRGB encode,
and `tonemapImage` for a whole frame.

It is **promoted, not written**: the corrected mirror already existed in
`tests/rendering/test_image_formation_gpu.cpp`, and that file now calls the engine's copy. Leaving
it there as well would be the second copy ADR-372's defect lived in.

**The trap, and the defence.** ADR-372: the shader undid its own sRGB encode with `pow(v, 2.2)`,
which is not the inverse of the piecewise curve it re-encodes with — nine code values of 255, worst
in the toe — and the CPU mirror carried *the same wrong inverse*, so the comparison confirmed the
shader agreed with itself. A parity test between two copies of one mistake is not a parity test.

So the anchor is a third party: **byte values measured off the shipped GPU pipeline and written
down**, neither of them derived from this code.

| source | anchor | this port |
|---|---|---|
| `docs/hdr-lab/README.md` §2 grey ramp, AgX, exposure 1, chroma 0, bloom off | 127 / 173 / 201 / 223 / 238 / 254 / 255 | **all seven exact** |
| `docs/image-formation.md`, scene-linear (8, 1, 0.2) | AgX (255, 209, 174), ACES (255, 232, 149) | **both exact** |

And the control, which is what makes those tables evidence rather than decoration:
`test_tonemap.cpp` carries **the historical wrong inverse** and requires it to produce ADR-372's
*old* byte column — 7, 21, 40, 66, 95, 128, 160 against the shipping 16, 27, 45, 68, 96, 127, 158.
It does, to the byte. The anchor is therefore proven to have the resolution to catch exactly the
defect that got through last time.

**Grain is not bit-comparable and the header says so.** `fract(sin(x) * 43758.5453123)` depends on
`sin` of a large argument, which is not specified to the same precision on a GPU as in libm, and the
multiply turns the last bits into a different random number. Parity is asserted at grain 0 and a
path-traced movie renders with grain off — adding noise to an image somebody has spent minutes per
frame denoising is the wrong default anyway.

### The control found a defect in this file, which is the point of having one

The first version had a single `srgbByte`, documented as applying the sRGB OETF and not applying it.
`tonemapPixel` already encodes (it is `fs_main`, which ends in `linearToSrgb`); `tonemapOperator`
does not (it is `agx()`, which does not). One function for both silently encodes one of them twice.
The control arm caught it by producing **1 where ADR-372's table says 7**. It is now `quantise8` and
`srgbByte`, and the header says why they are two.

## 3. Path-traced sequences

`src/app/trace_sequence.{hpp,cpp}`. Deliberately **not** a queue of `TraceJob`s, for one measured
reason: `TraceJob::execute` constructs an `app::Engine` and calls `loadProject` *inside its own
body*, so a hundred frames would parse the project and rebuild every mesh and texture a hundred
times. The project is loaded once and the engine is walked forward, exactly as `RenderJob` walks the
rasteriser.

A capability that falls out for free and could not exist before: `buildSnapshot(scene, &previous)`
wants two evaluations of the same composition, which a single-frame job can never have. A sequence
has the frame before this one, so **motion vectors are available to a path-traced sequence and are
not to a path-traced frame**.

End to end, measured on this machine, headless, **with no GPU involved at all**:

```
avgen --headless --project examples/world/glowmere-valley-2-multicam.json \
      --pathtrace /tmp/seq.mov --range 12.0:12.4 --fps 10 --pt-samples 2 --size 96x54
```

```
video: native backend, prores4444 96x54 @ 10 fps -> /tmp/seq.mov (+ audio from Rebuild.mp3)
sequence: 4 frame(s) written, sequence hash bf6235c499246d45
```

and `ffprobe` on the result: `prores`, 96x54, `nb_frames=4`, `r_frame_rate=10/1`,
`color_primaries=bt709` / `color_transfer=bt709` / `color_space=bt709` (ADR-365), plus a
`pcm_s16le` audio stream. Naming it anything else writes one scene-linear EXR per frame into a
folder, which is the same rule `--render` follows.

`--range a:b` and `--fps` are **the flags `--render` already uses**, not a `--pt-range`: it is the
same question about the same timeline, and a second vocabulary for it is a second thing to get
wrong.

### Reachable from the Render panel, not only from a flag

A `range` checkbox beside the second, with an end and an fps and a live frame count. Ticking it
proposes a second of footage rather than an empty range, so the control does something the moment
it is used. The job runs on its own thread for the reason `TraceJob`'s does, and progress reports
**both levels** — "frame 12 / 240" and "96 / 128 samples on this frame" — because a path-traced
frame takes long enough that a frame counter alone reads as a hang.

`TraceSequence` is on `Application::~Application`'s destruction list, cancel-then-join-then-destroy.
ADR-364 earned that lesson three weeks ago by finding `job_` and `ptJob_` missing from it; adding
the third job to the list at the moment it was created rather than after the next crash is the whole
value of having written that down.

## Refusals, and why each is a refusal rather than a clamp

* A **PNG sequence** from a path trace is refused, not silently tone-mapped. The tracer's output is
  scene-linear radiance and 8-bit frames throw that away for nothing a video does not already give;
  if somebody wants one it should be a decision with a reason.
* A range of **more than 18,000 frames** is refused. At 24 fps that is over twelve minutes of
  footage and a path tracer spends minutes per frame: a typo in an end time should not commit the
  machine for a month without saying anything. A test checks the refusal *and* that 17,940 frames is
  allowed, so it is a limit rather than a blanket objection to long renders.
* An output path that is not `.exr` for a single frame was already refused (ADR-366) and still is.

## Tests

`test_tonemap.cpp` (6 cases) — the two documented anchors, monotonicity for the operators nobody
published a ramp for, exposure/chroma/vignette behaviour each with a control that must *not* move,
`tonemapImage` agreeing with the per-pixel path on a deliberately odd 7x5 image, and the historical
wrong inverse.

`test_frame_range.cpp` (9 cases) — the arithmetic including the exclusive end; `RenderSettings` and
`FrameRange` agreeing across a grid of audio/timeline inputs; the loop against a fake renderer that
can be made to **lag three frames behind the submitter**, which is what a three-slot readback ring
does, with the hash chain required to come out identical either way; bounded stepping and cancel;
the finish ordering that would otherwise drop the tail; failure on either side of the queue; and
progress refusing to estimate before eight frames.

`test_trace_sequence.cpp` (5 cases) — a real range writing one EXR per frame, determinism across two
runs, a one-frame range still being one frame, the refusals, and **the control that matters most**:
three frames a second apart must produce *different* frames, because everything else in the file
would pass just as happily for an implementation that traced one moment three times and wrote it out
under three names.

None of these needs a GPU. That is the point of the shape: a sequence that is a frame short, or
hashes out of order, or drops its tail on a clean finish, is not a graphical defect, and a device
makes it harder to find rather than easier.

## 4. Two of the audit's "not met" items, since they were in the way

**G4 — the codec list advertised codecs the machine may not have.** A hardcoded array of eight, four
of them ffmpeg-only, offered whether or not this build has a native encoder and whether or not an
ffmpeg exists anywhere. The brief's own rule is "do not expose unavailable backends" and the
shipping UI broke it one layer below where the brief was looking: picking `libx265` with no ffmpeg
installed is a render that fails when the output is opened, **after the project has been saved**.

`ui::availableCodecs(native, haveFfmpeg, backend)` is a pure function of three facts, so a test asks
it every combination without an encoder — including the control that the four ffmpeg-only names
really do disappear, which an implementation that returned everything would fail. A project that
names a codec this machine cannot produce is shown **selected, with a warning**, rather than quietly
rewritten: somebody's authored choice is not the laptop's to overrule.

**G3 — three settings of the deliverable with no widget at all.** `supersample` (ADR-212, the
documented answer to foliage shimmer — 2.80% chroma noise at 720p against 1.86% at 1440p),
`aovs` (ADR-242) and `encoderThreads` were CLI-only. All three are in the Render panel now. The AOVs
are checkboxes over `aovNames()` rather than a text field, because a typo in a free-text list
silently exports nothing, and they are read back through `aovList()` rather than by substring — a
checkbox that lies about its own state is worse than no checkbox.

## Still owed

`RenderJob` adopting the driver (above). `aovs`, `supersample` and `encoderThreads` are still
CLI-only. The codec combo still advertises codecs the machine may not have. HDR video — 10/12-bit
through `writeFrame` — is unblocked by the CPU tone map but not built. The Metal prototype remains
a decision to be taken on measurement, not a backend to be built.
