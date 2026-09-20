# ADR-521: The warm-up never reached the renderer that produces the output

- Status: Accepted (2026-09-20)
- Extends ADR-147 (the tier reaches the render job), ADR-182 (a probe that cannot fail),
  ADR-360 (the bounded particle warm-up).

## Problem

ADR-360 traded strict particle determinism for a set of mitigations, and one of them is
`--particle-warmup <n>`: step the pools *n* frames before the first frame of a render, so a shot
does not open on an empty field. It was built, it was capped at 240 frames, it validated its
argument, and on the path that produces every deliverable it did nothing at all.

`Application::init` calls `renderer_->setParticleWarmUpFrames(options_.particleWarmUpFrames)` on
the interactive renderer. `RenderJob::start` builds **its own** `SceneRenderer`, and nothing ever
told it.

Measured:

```
avgen --headless --composition examples/labs/particle-vfx-lab.scene.json \
      --size 640x360 --range 2:2 --particle-warmup 120 --render out
```

sequence hash `e52b7fefaa395bcc` — byte-identical to the same command with no warm-up at all, and
a frame in which three of the lab's four systems are empty. Only `burst` appears, because a burst
is added to the emit count directly while a continuous rate over one frame's `dt` is one sixtieth
of a second of emission into a pool that starts at zero.

This is the **fourth** time this exact defect has been found in this file, and the previous three
each left a paragraph in `RenderSettings` saying so:

- `tier` (ADR-147): a batch frame came out byte-identical to an interactive one, so every promise
  the Offline tier makes was stated in the tier table and not kept.
- `disablePasses`: `--disable water` on a `--render` produced a byte-identical sequence — an
  attribution arm that cannot fail, which is worse than no arm, because its null result reads as
  "this subsystem is innocent".
- `qualityArms`: the same, one flag over.

## Decision

`RenderSettings::particleWarmUpFrames`, filled from the options in
`Application::renderSettingsFromOptions()` beside `disablePasses` and `qualityArms`, and applied in
`RenderJob::start()` beside `setQuality`. After: hash `d9333ef947a1b1e3`, and all four of the lab's
systems are in the frame.

It is a property of the deliverable rather than a diagnostic. A single-frame render of a continuous
emitter without it is not a shot at *t*; it is a shot of the first sixtieth of a second of a field
that should have been running for minutes.

## The larger thing this was hiding

`--range t:t` is the idiom the whole repository uses to look at one frame — the Quality Lab uses
it, `labs::reproduceCommand` emits it, and every README example is one. For **every continuous
emitter in every scene**, it was showing the first sixtieth of a second of the field rather than
the field. Nobody had noticed because a sparse particle field and a correct one both look like
particles.

## The one next to it, NOT fixed here

`FixedStepClock::tick()` returns `deltaTime = 0` for the first tick. That is defensible — the
first frame has no previous frame — and it means that on the first frame of every render
`shutterSeconds` is 0, so **neither ADR-040's velocity stretch nor ADR-037's motion blur exists on
it**. Rain rendered at `--range 3:3` is a field of round dots; the same rain at
`--range 2.91:3`, frame 5, is a field of streaks.

It is not fixed here because a fixed-step clock does know its step, so the honest fix is to report
`1/fps` on the first tick — and that moves the first-frame hash of every render in the repository,
including several agents' live baselines. It is written down instead, and the weather scenes carry
the workaround in their `_note`: render a short range and take the last frame.
