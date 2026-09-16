# ADR-011: Parameter and modulation model

- Status: Accepted (2026-09-08); extended in milestone 0.3 with sources (LFO, envelope, noise, random, timeline, macro), route polarity, presets and the v2 project format
- Research: `docs/research/audiovisual-systems.md` §20-21, `docs/research/audio-analysis.md` §5

## Problem

The brief's central architectural principle: every interesting scene property must be a
parameter, and audio (later LFOs, envelopes, timeline, MIDI/OSC) must drive parameters through
a generalised modulation system rather than hard-coded `if (bass > x)` logic. The first
milestone needs a real, tested version of this that will grow into the full matrix without a
rewrite.

## Alternatives considered

1. Hard-coded audio-to-property mapping in the scene (the "toy visualizer" path).
2. Full node graph / modulation matrix with a reflection system in 0.1.
3. A minimal typed `Parameter<T>` with metadata plus data-driven `ModRoute`s from a
   `SignalBus`, evaluated once per frame.

## Decision

Option 3, with these definitions (namespace `avgen::params` and `avgen::signals`):

```
Parameter<T>   { path, default, hardRange, softRange, base, modulated, final, flags }
               T in { float, glm::vec2/3/4, Color(vec4), bool, int }
ParameterSet   { registry of Parameter by path; iteration for UI/serialization }

Signal         { name (e.g. "audio.bass"), value(float), event flag, frame index }
SignalBus      { registry of Signals updated per frame by producers (Analyzer, clock, later LFOs) }

Processor      { one stage of the chain: Gain, Offset, Curve, Normalize, Clamp, Threshold,
                 Smooth(attack, decay), Envelope(mode, hold), Remap }
ProcessorChain { fixed-order list; stateful stages own their state; reset() on seek }

ModRoute       { source signal name, chain, amount, op (Add | Multiply | Replace | Min | Max),
                 target parameter path (+ component), enabled }
Modulator      { evaluate(bus, dt): for each target, final = clamp(op-fold over routes(base)) }
```

- Evaluation order per frame: producers update the `SignalBus` -> `Modulator` evaluates all routes
  and writes `final` on each parameter -> scene reads `final` -> renderer reads scene. The
  renderer never sees a signal; the analyzer never sees a parameter.
- Routes are data (serialisable to JSON) held by the scene document, not code.
- Smoothing is asymmetric attack/decay in the chain, at frame rate, with coefficients derived
  from time constants so results are independent of frame rate (`1 - exp(-dt / tau)`).
- Events (onset) enter the chain as impulses and are turned into control envelopes by the
  `Envelope` stage.
- The milestone 0.1 scene declares five routes: `audio.bass -> orb/scale`,
  `audio.mid -> orb/rotationSpeed`, `audio.treble -> orb/emissive`, `audio.rms ->
  scene/brightness`, `audio.onset -> orb/impulse`. The UI's "bass/mid/high response" sliders are
  the `amount` of those routes; "master gain" is a `Gain` on every route.

## Rationale

- This is the minimal shape that has the same topology as Notch modifiers, TouchDesigner CHOP
  exports, SynthLab's modulation matrix and Blender drivers (lessons 1, 3, 4, 5 of the research).
  Growing it means adding processors, sources and target types, never changing the topology.
- Data-driven routes make presets, serialisation, OSC addressing and UI generation the same
  mechanism: a path.
- Keeping smoothing out of the analyzer lets one raw signal drive several parameters differently.

## Consequences

- Parameters are the only way the UI or modulation touches the scene, so the UI is generated from
  parameter metadata from day one.
- Paths are strings; lookups are hashed once at route binding, not per frame.
- No reflection or ECS in 0.1; `ParameterSet` is populated by explicit registration.
- Serialisation covers parameters (base values), routes and processor settings; tests cover
  round-trip, clamping, defaults, mapping, modulation summation, and smoothing determinism.

## Rejected alternatives

- Hard-coded mapping: explicitly forbidden by the brief; would be thrown away.
- Full graph in 0.1: over-engineering before a single signal flows.

## Revisit triggers

- Milestone 0.3 (general modulation): add LFO, envelope, noise and timeline sources; per-route
  polarity; modulators of modulators; macros.
- Milestone 0.9 (project system): reflection or code generation for parameter registration if
  manual registration becomes error-prone.
