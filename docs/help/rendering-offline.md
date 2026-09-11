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
