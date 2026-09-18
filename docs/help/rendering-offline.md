---
id: rendering/offline-render
title: Offline Rendering
category: Rendering
summary: Rendering to PNG, EXR or video — the settings, the queue, and the things that catch people out.
order: 57
tags: render, export, video, png, exr, codec, queue, offline
keywords: how do i export a video; render to file; png sequence; what codec; render queue; my render is missing the captions
related: rendering/overview, sequencer/overlays, start/projects, performance/diagnosis
features: panel.render, subsystem.rendering
---

# Offline Rendering

The **Render** panel holds the settings, the progress and the queue. From the command line,
`--render <out>` does the same thing headless.

An offline render **loads the saved project** and runs on its own; the live view keeps playing.

## Settings

| Field | Default | Notes |
|---|---|---|
| size | 1920 × 1080 | 1 – 16384; **video needs even dimensions** |
| fps | 60 | up to 1000 |
| range | 0 to −1 | an end below zero means the audio's duration, then the timeline's, then 10 s |
| output | PNG sequence | or EXR sequence, or video |
| path | | a directory for a sequence, a file for video, relative to the project |
| pattern | `frame_{:06d}.png` | sequences only; must contain `{}` |
| codec | `h264` | video only |
| backend | `auto` | `auto`, `native` or `ffmpeg` |
| quality | 80 | 0 – 100, video encoder quality |
| mux audio | on | |

The codec combo offers `prores4444`, `prores422`, `h264`, `hevc`, `libx264`, `libx265`,
`prores_ks`, `libvpx-vp9`. The first four are native on macOS; the rest need ffmpeg.

The panel validates live and shows any problem in red before you can start.

## The three output kinds

**PNG sequence** — display-referred 8-bit, after tone mapping. Includes composition layers.

**Video** — same image, encoded. Includes composition layers.

**EXR sequence** — **scene-linear half floats from the HDR target, before tone mapping.** For
grading. It does **not** include composition layers, and AV Gen warns you when a render starts with
both, rather than letting you discover it in a grade.

## Watching the frames go out

**Show output frames**, under the progress bar, puts the frame the render just wrote into the
panel. It is not a second render of the same moment: it is the encoder's own pixels, read where the
render hashes them, and the caption under it gives the frame number and that frame's hash so a
picture can be tied to a specific frame of the deliverable.

It is off by default and it is remembered between sessions. From the command line,
`--render-preview` forces it on for one session whatever the settings file says, and
`--render-in-app <path>` starts a render *in the window* rather than headless, which is the only way
to reach this state from a script or a capture.

What it costs: the frame is point-sampled down to at most 480 px on its long axis, which at
1920 × 1080 reads 1 pixel in 16 and copies 518 KB instead of 8.3 MB. Measured over 24 frames at
1920 × 1080, that is 0.19 ms a frame — under 1% of the render. Only the newest frame is kept; the
panel says how many went by unshown.

Two things it is honest about rather than hiding:

- **It aliases.** Point-sampling a 1920-wide frame to 480 shimmers on fine detail. The file does
  not.
- **An EXR is scene-linear**, so it has no display appearance of its own. The preview clamps it to
  0–1 and sRGB-encodes it, and says so on the panel. The project's tone map, exposure, vignette and
  grain are **not** applied — those live in the GPU's tone-mapping shader, and a second CPU copy of
  them would be free to drift from the picture it claims to be of.

## The queue

**Add to queue** requires a saved project: it saves first, then queues the project path together
with the current settings. **Run queue** starts the front of it, and each job that finishes without
error starts the next.

From the command line, `--queue <file>` runs a queue document headless.

## Things that catch people out

> [!WARNING]
> **The quality tier is not raised automatically.** A render uses whatever tier the process was
> started with — `realtime` by default. For the best shadows and ambient occlusion, start AV Gen
> with `--tier offline`.

> [!NOTE]
> **`quality` is the video encoder's quality, not the renderer's.** There is no sample count and no
> supersampling setting. Anti-aliasing is `post/output/antialias`, which is FXAA; there is no MSAA
> and no temporal AA.

**Renders are warmed up first.** Two throwaway frames are rendered before the real ones, because
the first frames drawn with freshly compiled pipelines differ by a least-significant bit in
scattered pixels. Set `AVGEN_NO_WARMUP` to skip it and reproduce the difference.

**Frame rate changes the result.** A 24 fps and a 48 fps render of the same second are not
byte-identical, because passes with temporal history — ambient occlusion, exposure adaptation — have
seen a different number of frames. Frame 0 is identical at both.

## Reporting

When a render finishes the panel reports the frame count, the elapsed time and a hash of the
sequence, which is the quickest way to tell whether two renders produced the same pixels.
