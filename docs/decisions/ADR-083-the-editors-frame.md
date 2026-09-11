# ADR-083: The editor's frame

**Status:** accepted (2026-09-10)
**Context:** `docs/application-performance.md`

## Context

"The whole UI feels sluggish."

Measuring it produced a result that did not match the complaint. On the world scene the main thread
did **1.6 ms of work per frame** and then stood still for **15.6 ms** waiting for a swapchain image.
The UI build cost 0.044 ms and, at idle, allocated nothing at all. On an empty scene the editor ran
at the display's full 120 Hz. By every measure of throughput the editor was fine.

By one measure it was not: **input-to-present latency was 17.2 ms**, and the work in that 17.2 ms was
1.6. The rest was a wait sitting in the wrong place.

The frame was ordered: poll input → build the UI → block for a swapchain image → record → submit →
present. So the picture that reached the screen had been built from input sampled a whole wait
earlier, and every millisecond the GPU fell behind was also a millisecond of staleness under the
pointer. A frame-rate readout cannot see this — it reports how often the picture changes, not how
old the picture is. That is why the complaint was real and the numbers all looked fine.

Two other things were doing the same kind of damage: work that was legitimate, placed where it took
the frame away.

- Dragging a parameter that changes a procedural object's *structure* regenerated the geometry inside
  the frame, on every frame of the drag: 145–216 ms per frame, 5–7 fps, for as long as the drag
  lasted.
- Opening a scene with an environment map flattened the entire world twice, freezing the editor for
  775 ms.

## Decisions

### 1. The swapchain wait comes before input is sampled, not after

The queue is pumped twice per frame. Pass 1, at the top, takes the window-level events that have to
be acted on before a surface image is asked for at all — resize, close, dropped files. Pass 2, the
instant the image is acquired, takes input. The engine update and the UI build move after the wait
with it, so a camera drag still lands in the frame that reads it.

Nothing is dropped: both passes go through the same handler and the same ImGui backend. A resize
arriving in pass 2 is carried to the next frame explicitly, because this frame's surface is already
configured — the `FrameEvents` that used to carry that flag is a per-frame local, so it would
otherwise vanish rather than be deferred.

**Rejected:** reducing the swapchain queue depth, or switching away from Fifo. Both trade throughput
or tearing for latency. This trades nothing — the wait was going to happen either way; the only
question was what the application did with the time, and "sample input" is a better answer than
"nothing".

### 2. An expensive procedural regeneration may wait for the drag to stop

An object whose last regeneration exceeded a budget keeps two timers. **Settled** measures time since
its inputs last moved and restarts whenever they move again, so a drag never reaches a regeneration
through it; it fires at 90 ms, about five frames, short enough that letting go of a slider feels
immediate. **Held** measures how long the object has been wrong and only resets on an actual
regeneration, so a drag lasting seconds still gets a refresh; it fires at `max(90 ms, 4 × last
cost)`, so nothing spends more than about a fifth of its time regenerating and a cheap object is not
starved to protect an expensive one.

One timer cannot express this, and the first attempt proved it: a moving target resets the only
timer there is, so the refresh ceiling was unreachable and a long drag showed nothing changing.

**The budget is zero by default and only the live editor sets it.** The deferral reads a wall clock,
and a wall clock has no business deciding what a deterministic render contains. Offline takes the
same unconditional loop it always did, and a `--headless --capture` is byte-identical before and
after.

**Rejected:** moving regeneration to a worker thread. The composition is not thread-safe, the
flattened scene is read by the renderer in the same frame, and introducing that ownership problem to
save milliseconds is the trade the brief explicitly warns against. Deferring is cheap, local, and
reversible.

**Rejected:** rebuilding at a fixed interval. A fixed interval either starves the cheap objects or
hands the expensive ones the frame back. Scaling the ceiling by measured cost is self-correcting and
needs no per-scene tuning.

### 3. `dirty_` must mean something changed

`Composition::setEnvironmentMap` set the rebuild flag unconditionally, and the load path called it
with the path the scene had just been loaded with. `dirty_` is not a request to re-read a file — the
registry caches images by path — it is a request to re-flatten everything: 256 terrain chunks,
~260k scatter cells, every procedural cloud, a full vertex scan for bounds.

The guard compares **resolved** paths, because the two sides differ in form: the scene file stores a
relative path, the engine hands back the absolute one it resolved in order to load the image. A
guard on the raw strings never fires, which is how the first version of this changed nothing and was
caught only because the measurement did not move.

### 4. The editor decides how many pixels the world costs, so it gets a control

The editor renders the world at the canvas's size times the display's backing scale. In a
1920×1200-point window on this machine that is 2466×1766 — **4.36 Mpx against the 1.30 Mpx of the
"1440×900" the renderer benchmarks quote**. The world is three times the size everyone thinks it is,
and nothing in the interface said so.

A canvas render scale, 0.25 to 1.0, default 1.0 — every canvas pixel, exactly what the editor always
did. At 0.75 the editor stops being GPU-bound and starts being display-bound: 48 fps → 111 fps.

This is an editor decision, not a renderer one. Nothing about the picture changes, only how many
pixels of it are computed before it is shown, and an offline render is untouched. Sharpness for
responsiveness is a trade the person doing the work should get to make, and it was not previously
theirs to make.

Every run also logs the canvas's real size once, so no measurement taken in this application has to
guess at it again.

## Consequences

- Input-to-present latency: **17.2 → 1.4 ms** (world), **8.3 → 0.38 ms** (empty). Frame time
  unchanged — this bought latency, not throughput, which is what the complaint was about.
- Frames over 50 ms during a parameter drag: **221/500 → 24/500**. `engine.update` P95: **215 → 41 ms**.
- Opening a scene with an environment map: **775 ms of frozen main thread → 0.4 ms**; wall-clock open
  2.38 s → 1.62 s.
- A canvas-scale control that moves the editor from 48 to 111 fps on a real world.
- Offline rendering is bit-identical, verified by capture rather than by argument.
- Geometry can now lag a slider by up to 90 ms after release, or by the refresh ceiling during a
  drag. `Composition::proceduralsAwaitingRebuild()` exists so the editor can say so; nothing shows it
  yet, and it should.
- The instruments stay: `core::PhaseProfiler`, the interposed allocation counters, and
  `--ui-script`. They are compiled in, cost less than the clock's own resolution, and the next
  person to ask this question should not have to build them again.

## What this ADR is not about

The GPU's own frame. That is ADR-051, ADR-077, `docs/renderer-2-architecture.md` and
`docs/performance.md`. The one thing this ADR contributes to it is §4: the editor has been asking
for two to three times the pixels every GPU benchmark was measuring.
