---
id: performance/diagnosis
title: Diagnosing Performance
category: Performance
summary: A method for finding out why a scene is slow, rather than a list of things to switch off.
order: 70
tags: performance, profiling, diagnosis, method, benchmark, a/b
keywords: why is my scene slow; how do i make it faster; frame rate is low; profiling; what is making it slow; performance
related: performance/what-costs-what, performance/instruments, rendering/overview
features: subsystem.performance
---

# Diagnosing Performance

Switching things off makes a scene faster and tells you nothing. The point of a performance pass is
to find out **which subsystem owns the time**, and then to decide whether that is reasonable.

This is the method that has actually worked on this engine, in order.

## 1. Establish which half of the machine you are in

Look at the status bar. `ms cpu` is main-thread work. `ms gpu` is the GPU's whole frame. `ms frame`
is wall time between frames and includes waiting for the display.

If `ms gpu` is close to `ms frame` and `ms cpu` is small, the GPU owns the frame. If `ms cpu` is
close to `ms frame`, the CPU does. If both are small and `ms frame` is large, you are waiting for
the display, which is not a problem.

> [!TIP]
> Frame *rate* and frame *latency* are different questions and a frame-rate graph cannot see the
> second. The editor once ran at a perfectly good frame rate with an input-to-present latency of
> 17 ms, because the main thread sampled input and then stood still for 15 ms waiting for a
> swapchain image. Reordering the loop took it to 1.4 ms. Nothing about the frame rate moved.

## 2. Baseline, and value a negative result

Measure something simple before you measure something complicated. An empty scene with an idle UI
should be at the display's refresh with under a millisecond of UI work. If it is, a whole class of
explanation is ruled out on the first measurement.

## 3. Measure a reference scene alongside, every time

Machine contention is real and it is large. In one session the same simple scene read 8.4 ms in the
morning and 27.4 ms in the afternoon with nothing changed, and the complex scene read 97 ms where
it had read 52. Read on its own that looks exactly like a catastrophic regression, and the next
hour goes on bisecting a change that was never at fault.

Keep a cheap reference scene in the run. If it moved, throw the numbers away and come back later.

## 4. Use the right statistic

Over 300 headless frames, the same three configurations run three times gave medians differing by
about threefold while their **minima** agreed to a tenth of a millisecond.

**Contention is never negative, so the minimum is the statistic** for comparing configurations. A
longer run does not average contention out; it collects more of it.

For **pacing** — how the application feels — use order statistics rather than the mean. A sequence
of 16, 16, 16, 120, 16, 16, 80, 16 has a fine average and is a terrible experience.

## 5. Know your noise floor before claiming a saving

Before this engine timed its own frames, six identical runs spread over 3.6 ms. A claimed saving
under about 3 ms could not be distinguished from noise, and one comparison very nearly recorded an
eleven-millisecond saving that turned out, once the arms were interleaved and repeated, to be 1.1.

With the in-engine timers and interleaved runs the floor is now around ±0.3 ms.

## 6. Isolate by removal, and log which arm you are in

`--disable shadows,ao,volume,post,shadowmask`. The run prints `A/B: phases disabled for this run:`
and the list, so two halves of a comparison can never be confused after the fact.

## 7. Prove the edit applied

An A/B whose two arms are identical reports "no effect" in exactly the same voice as a real null
result. One investigation concluded that removing a scene's ecology changed nothing; the edit had
silently deleted nothing at all. Redone properly, the ecology was **two thirds of the frame**.

Print something from inside the edit — an instance count, a draw count — and check that it changed.
A headless run prints a `workload:` line for exactly this purpose.

## 8. Check that a number responds to its own workload

If a pass timer claims 39 ms and halving its step count moves the frame by 2, the timer is wrong.
A good instrument tracks its own knob: raising the volumetric step count eightfold raised both the
pass time and the frame time by the same six and a half milliseconds.

## 9. Attribute the difference between the pass timer and the A/B

They rarely match exactly, and the difference is information. Turning ambient occlusion off saves
more than the AO pass costs, because the lit pass then stops sampling the AO texture. When the A/B
saves more than the pass, look for what else stopped happening.

## 10. Measure at the resolution the thing actually runs at

See [What costs what](help://performance/what-costs-what) — the editor's canvas is much larger than
the window size suggests, and above roughly 1.3 megapixels the cost model changes character.

## 11. Never remove a diagnostic to improve a number

A number that improved because you stopped measuring it has not improved.
