# ADR-018: Timeline — keyframe automation as the first modulation layer, cues for presets

- Status: Accepted (2026-09-08)
- Research: `docs/research/audiovisual-systems.md` §2.7 (TouchDesigner timelines and keyframe
  channels), §4.3 (Unreal Sequencer), §5.2 (Unity Timeline assets vs bindings), §21 (data model,
  lesson 9: keyframes as just another channel source, evaluation as a pure function of time)

## Problem

Milestone 0.8 needs authored change over time: parameters that follow keyframed curves, in
seconds or in beats, and scene-state changes at known moments (the drop, the breakdown), while
audio-driven modulation keeps working on top and offline renders stay deterministic and seekable.

## Alternatives considered

1. Keyframes only as signals (`TimelineSource` from 0.3 routed through `ModRoute`s).
2. Automation writes parameter *base* values every frame (DAW "read" automation).
3. Automation writes parameter *final* values between reset and routes (chosen).
4. A full sequencer with tracks bound to scene objects, clips and blend modes.

## Decision

- `params::Timeline` holds `Track`s (target path, optional component, time base seconds or
  beats, mode replace/add/multiply, loop length, sorted `Key`s with step, linear, smooth
  (clamped Catmull-Rom), ease-in/out/in-out and Bezier (Hermite tangents) interpolation) and
  `Cue`s (time, name, preset, morph length). `Track::evaluate(t)` and `Timeline::cueAt(clock)`
  are pure functions of a `TimelineClock` (audio seconds, beats from the beat clock).
- Evaluation order per frame: `ParameterSet::resetFinals()` → `Timeline::apply()` →
  `Modulator::applyRoutes()` → scene. Automation therefore sets the value routes add to, and the
  user's base values (sliders, presets, project files) are never overwritten by tracks. The UI
  marks automated parameters with `[A]`.
- Cues are the exception: at a cue the engine recalls the named preset into the base values,
  morphing from the values current at that moment over `morphSeconds`. The engine owns that
  state and re-syncs it after a seek (the latest cue at or before the position applies).
- Timeline time is the audio position (render time without audio), so pause holds and seeks are
  exact; beat-based tracks and cues read the per-frame beat clock (ADR-012).
- Projects carry the timeline (`"timeline"`, project format version 3); keys are recorded
  from the UI ("Key at current time", the Timeline tab) or written by hand.

## Rationale

Writing finals keeps one source of truth for authored state (base) and one for evaluated state
(final), which is what makes presets, projects and automation compose instead of fighting: a
preset changes what the sliders say, a track changes what the frame shows, a route adds the
audio. Pure evaluation on a clock is what offline rendering (1.0) needs and what makes seeking
trivial. Signals-only keyframes (alternative 1) remain available for modulating anything through
a chain, but keying a parameter directly is what people expect from a timeline. A sequencer with
scene bindings (alternative 4) is more than the current scene model needs; compositions already
carry per-node parameters, so a track on `nodes/<name>/position` is a node animation.

## Consequences

- Positive: deterministic, seekable automation in seconds or beats; audio modulation stacks on
  top; presets become time-addressable through cues; projects round-trip everything.
- Negative: no curve editor beyond the key table and preview; no recording of live slider moves
  as continuous curves (one key per click); cues morph from "whatever the values were", which
  after a backwards seek means the values at the seek, not at the original time.
- Follow-ups: a proper curve editor, live recording, per-track processors, scene switching on
  cues (needs asynchronous loading), export of the timeline range as the offline render range.
