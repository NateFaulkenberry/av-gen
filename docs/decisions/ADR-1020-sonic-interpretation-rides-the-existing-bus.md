# ADR-1020: Sonic interpretation rides the existing bus: a timbre post-pass, a character runtime, a note track, and an interpret source

**Status:** Accepted (Sonic Garden POC, Phase 0).
**Date:** 2026-09-30
**Context document:** `docs/prototypes/sonic-garden/RESEARCH.md`

## Context

The owner's brief (`docs/prototypes/sonic-garden/00-brief.md`) asks for Audio → Sonic Character and
MIDI → Musical Context, combined by a Visual Interpreter into ordinary signals. It asks for this without a new
modulation architecture, and with the subsystem doing nothing when unused.

## Decision

1. **Raw timbre** (`timbre.*`) comes from `sonic::analyzeTimbre`, a post-pass over the `AnalysisTrack`'s stored
   magnitude spectra.
   - It runs at audio load, and only when the project has a `sonic` block.
   - The streaming `Analyzer` is unchanged.
2. **The Sonic Character** (`sonic.<dim>`, `sonic.<dim>.slow`, `sonic.transient`) is a `SonicRuntime` in
   `SignalClock`, beside `MusicRuntime`.
   - It consumes each analysis frame on the hop clock and publishes once per render frame.
   - Seek checkpoints carry it with no extra code.
   - Every dimension is a table of fixed-range terms that the project can override. There is no auto-gain.
3. **Musical Context** (`notes.*`) is a pure function of time over a `NoteTrack`, read from a Standard MIDI File
   named by `sonic.notes`. The family is `notes.*` because `music.*` already means audio-derived structure events
   (ADR-073).
4. **The Visual Interpreter** is an `interpret` source kind in the `SourceRack`.
   - Named mappings blend any bus signals into `visual.<name>`.
   - Mappings can compete in a group, which gives continuous family weights.
   - The source is pure given the bus, so seeks replay routes from it.
   - Routes carry `visual.*` into parameters as they carry everything else.
5. All three signal families are declared in `declareFrameSignals`, so the engine bus and the replay bus agree on
   ids. They sit at zero in a project with no `sonic` block.

## Consequences

- Offline renders are deterministic: the timbre pass is a function of the file, the character a function of the
  frame sequence, and the context a function of time.
- An unrelated project changes in exactly one way: about 60 more idle names in the route-source picker.
- Live input and live MIDI (Phase 7) call the same per-frame timbre function on the analysis thread, and fill the
  same `NoteTrack` incrementally.
