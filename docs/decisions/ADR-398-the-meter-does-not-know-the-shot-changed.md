# ADR-398: The meter does not know the shot changed

- Status: Accepted (2026-09-20)
- Builds on ADR-037 (the physical camera and auto-exposure), ADR-397 (what a jump costs),
  ADR-385 (a stated reason is not evidence), ADR-383 (the frame-range driver).
- Found by the temporal-media agent while auditing; verified here against `main` at 9084db5d.

## Problem

Two defects, same one-shot, same family as ADR-395 and ADR-396: a comment describing behaviour that
does not exist.

### 1. A seek does not re-seed the exposure meter

`scene::PostSettings::exposureReset` (`src/scene/post_settings.hpp:100`) documents itself:

```cpp
bool exposureReset = false;                // re-seed the meter (scene change, timeline seek)
```

Only the first half was true. The flag is raised by `Engine::resetCameraState()`
(`engine.cpp:423`), and a whole-tree search finds **exactly one caller**: the composition install
at `engine.cpp:218`. `Engine::seekSeconds` resets the modulators, the audio sources, the music
detector, the beat clock phase, the cue state, the director and every entity — and leaves the meter
on its pre-seek reading.

So scrubbing from a night interior to a noon exterior gave a first frame exposed for the interior,
which then walked out of it over the meter's adaptation time. The focus tracker (`focusState_`,
reset by the same call) is the same story one lens along.

### 2. In an offline render the single reset is consumed by a frame nobody keeps

`render_job.cpp:156-183` renders two throwaway frames on a throwaway `SceneRenderer`, because the
first frames drawn with freshly compiled pipelines in a process differ by 1 LSB (Metal replaces the
pipelines' GPU binaries shortly after creation). That is a **pipeline** warm-up, and it is not the
history warm-up of ADR-395 or the pre-roll of ADR-397 — three different things that would be easy
to confuse, so: it is about shader binaries, not about state.

But it drives the *real* engine to get a scene to render, and `Engine::update` consumes the
one-shot (`engine.cpp:3811-3812`). So `renderOne()`'s first real frame saw `exposureReset == false`
and opened the render on whatever the throwaway frame had left in the meter.

It was harmless, for a reason worth naming: that job's throwaway `PostProcessor` is
default-constructed and meters nothing. That is accidental protection, not a design — it stops
being true the moment the warm-up renderer is configured like the real one.

## Decision

1. `Engine::seekSeconds` calls `resetCameraState()`. A seek is `TimelineStep::Jump` (ADR-397) and
   this is what a jump costs: state that was a function of the frames you came from is not a
   function of the frame you arrived at.

   It sits in `seekSeconds` and **not** in the per-frame update, deliberately. Re-rendering the
   same second is a `Repeat`, not a `Jump`; re-seeding the meter for it would make a frame depend
   on whether somebody clicked the playhead rather than on the second it names.

2. `RenderJob::prepare` calls `engine_->resetCameraState()` after the pipeline warm-up and before
   the real renderer is constructed, so the one-shot the first shipped frame needs has not been
   spent on a frame that was discarded.

## The probe

`tests/integration/test_exposure_reset.cpp`. No device: the re-seed is a flag on a settings struct,
and a rule that needs a GPU to check is a rule nobody checks.

It is written so that it fails for the defect and cannot pass for the wrong reason:

- Frame 1 carries the reset the engine is constructed with.
- **Frame 2 must not.** That is the control, and it is what stops the test passing against an
  implementation that leaves the flag permanently raised — which would re-seed the meter every
  frame and remove auto-exposure from the engine entirely.
- The playhead is asserted to have actually moved before the seek's frame is checked, so a
  transport that clamped the seek back to where it already was would fail rather than pass vacuously.
- The flag is asserted to be a one-shot again on the far side.

Measured without the fix, with only that one call removed and everything else identical:
`test_exposure_reset.cpp:76 FAILED: CHECK( m.reset() ) with expansion: false`. With it: 5 cases,
81 assertions, green.

## Consequences

- A scrub now costs one meter re-seed. That is the point: the first frame after a jump is metered
  for the shot it is in.
- Anything else that should be re-seeded on a seek and is not will still be missed. The audit that
  found this one was of `resetCameraState`'s call graph; a general sweep of "what else survives a
  seek that should not" is not done here.
