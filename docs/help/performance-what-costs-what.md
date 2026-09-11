---
id: performance/what-costs-what
title: What Costs What
category: Performance
summary: Where a frame actually goes on this engine, with measured numbers and the things already ruled out.
order: 71
audience: expert
tags: performance, cost, canvas, draws, shadows, ecology, resolution
keywords: what is slow; draw calls; too many triangles; canvas scale; resolution; is it the draw calls
related: performance/diagnosis, performance/instruments, rendering/overview, rendering/shadows
features: subsystem.performance
---

# What Costs What

Every number here was measured on an Apple M2 Max. Ratios travel between machines; absolute figures
do not.

## The canvas is not the window

The editor renders the world into the dock tree's centre region **at the display's backing scale**.

| Window, in points | Canvas, in pixels | Megapixels | Versus a 1440×900 benchmark |
|---|---|---|---|
| 1440 × 900 | 2880 × 1166 | 3.36 | **2.6×** |
| 1920 × 1200 | 2466 × 1766 | 4.36 | **3.4×** |

This is the first thing to check when a scene feels slower in the editor than its published numbers
suggest. Every run logs the canvas size once, on frame 60.

Aspect ratio matters as much as pixel count: the 3.36 Mpx canvas above is *slower* than the 4.36
Mpx one, because it is much wider and its frustum holds more of the world — a million triangles
against seven hundred thousand.

The **Canvas scale** slider at the bottom of the Control panel renders the world at a fraction of
the canvas's pixels and shows it stretched. It changes nothing about a render. A setting of 0.9
buys about 12% for a difference that is hard to see on a still frame.

## Where the lit pass goes

Shader-level A/B on a dense world, smallest configuration, to isolate the terms:

| Removing | Saved from the scene pass |
|---|---|
| all direct lighting | −16.5 ms of 21.4 |
| the shadow term (keeping the contact march) | −10.3 ms |
| the contact march (keeping shadow maps) | −6.6 ms |
| clustered local lights | −3.4 ms |
| the ambient occlusion fetch | −0.9 ms |

**Lighting is the frame.** After the shadow-mask work, the contact march is about 19% of the scene
pass and clustered bioluminescent lights about 18%.

## The geometry floor

Below roughly 1.3 megapixels the number of fragment invocations is set by the **triangle count**,
not the pixel count: a dense ecology submits 436,000 triangles against 324,000 pixels, and a
triangle smaller than a quad still costs a quad. Above that, screen coverage takes over. At the
editor's canvas you are past the crossover, which is why resolution scaling helps there and barely
helps at a small window.

## Density, not optimisation

A generated world at 13.4 ms and an authored one at 25.2 ms are not a comparison of two
implementations. The generated world places 9,860 candidate instances over 420 m; the authored one
places 114,289 over 640 m — 0.056 instances per square metre against 0.279. **It is sparser, not
better.**

## Things already tried and rejected

Do not spend time on these; each was measured.

| Idea | Result |
|---|---|
| Dynamic resolution | saturates *above* the frame target — half the linear scale still leaves the frame at 19 ms |
| Computing the contact march at half resolution | 30% of pixels change; contact shadows are exactly the signal that does not survive it |
| More taps in the shadow mask | 8.50% of pixels differ against 8.42%, for a full millisecond |
| Merging instances to reduce draw calls | a recorded draw costs 1.46 µs; a 120-draw frame spends 0.24 ms on submission. **Stop optimising the submission path** |
| Billboard impostors as currently built | +11.6 ms at an identical drawn count |

## Draw calls are not your problem

All of a dense world's indirect draws together cost about 0.23 ms. If you are counting draw calls
on this engine, you are counting the wrong thing. Count triangles, lights and pixels.

## The editor's main thread

On a full world, idle: the UI build costs 0.044 ms and allocates nothing; the engine update costs
0.38 ms; command recording 0.64 ms; ImGui recording and submit together about 1.96 ms. Total
main-thread work is around 3 ms against an 8.33 ms budget at 120 Hz. The rest of the frame is
waiting for the display.

Two things do cost real time on the main thread:

**Dragging a structural parameter.** Dragging something that changes geometry — a hierarchy depth,
an instance count — rebuilds procedural content every frame. This is budgeted: an interactive
rebuild gets 2 ms per frame in the live editor and the full rebuild happens when the drag settles.
Dragging a *material* parameter on the same scene costs 0.37 ms.

**A full composition rebuild.** Adding a material program, adding a hero node or swapping the
environment map re-flattens the entire world — around 390 ms, synchronously, on the main thread.
This is the largest remaining freeze in the editor and it is known.
