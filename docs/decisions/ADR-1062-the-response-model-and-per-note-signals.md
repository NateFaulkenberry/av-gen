# ADR-1062: The response model: signals by kind on one conditioning chain, the performer's five controls, and per-note MIDI

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion, brief §9-11)
- **Code:**
  - `src/sonic/response.*` (`ResponseModel`, `ResponseControls`, `ResponseSettings`);
  - the per-note facts and `assignVoice` in `src/sonic/notes.*`;
  - `SonicRuntime` (declare, step, publish) and `Engine::syncResponseControls`;
  - the Live panel's Response group (`src/ui/live_panel.cpp`);
  - the interpret source's widened hard ranges.
- **Tests:** `tests/unit/test_sonic_response.cpp` and the engine cases in `tests/unit/test_sonic.cpp` (`[adr1062]`).
- **Design:** `docs/prototypes/sonic-garden/VFX-ARCHITECTURE.md` §1-2; research 2.

## Context

The owner, §9-11:
- Sonic Garden must tell sustained, melodic, bass, kick, snare and hat apart, and MIDI must give distinct signals,
  "not one generic MIDI intensity".
- Sensitivity must not be `raw x sensitivity`.
- "slow chord -> slow world evolution; single bass note -> sharp visual impact".

The character (ADR-1020) has medium and slow tiers and one level-rise transient, and every `notes.*` was an aggregate:
a chord could not light three places.

## Decision

### Signals by kind (all 0..1; hits are events, each with an `...Env` envelope)

**Hits:**
- `response.kick`, `response.low`, `response.snare`, `response.hat` and `response.onset` come from ADR-1060's causal
  ratios, picked at a threshold the sensitivity moves (0.66x to 1.5x the default).
- `response.note` is the MIDI note-on, with its velocity through the same curve.

**Levels:**
- `response.bass` (30-150 Hz) and `response.level`, in dB on a fixed range;
- `response.transient` (a fast follower minus a slow one, in dB, 18 dB for full);
- `response.sustain` (the level less its percussive share);
- `response.flux` (broadband change over its own background).

**Presence:**
- `response.hatRate` (hats a second, 1.5 s leaky window, 12/s = 1);
- `response.melodic`: the audio's confident pitch moves, or MIDI's single notes in a moving line, whichever is larger;
- `response.pitch`: the latest recent MIDI note, else the latest confident audio pitch;
- `response.intensity`: the level over 12 s, the macro dynamics.

### One chain

`raw -> floor gate -> normalise -> sensitivity -> curve -> attack/release`.
- **Levels:** `(dB - floor)/48` with floor -60 dBFS. Sensitivity moves the floor by +-18 dB and sets the curve
  `x^(2^((0.5 - s) 1.6))`, so it lifts quiet values and never pushes the top past 1. Kind sensitivity 0 is silence.
- **Hits** are level-free by construction (ratios over their own median). A take 12 dB quieter gives the same kicks:
  15 and 15 on the test kit, after ADR-1060's compression was made relative to a causal spectral peak.

### The five controls

The parameters are `sonic/response/{sensitivity, transient, sustain}` (0..1, 0.5 neutral) and `{attack, release}`
(0.25-4x):
- **Sensitivity** moves both kinds. **Transients** moves the hits (`clamp(sens + transient - 0.5)`), and **Sustain**
  moves the levels. At Transients 0 nothing fires, and the levels are unchanged (tested).
- They are project parameters: registered while the project has a `sonic` block or live input runs, saved and loaded
  with the project, keyable and routable. Defaults come from the `sonic.response` block.
- The Live panel has a **Response** group: the five sliders, four hit meters (kick, snare, hat, note), and Reset. The
  old Sensitivity slider is now labelled **Input gain**, which is what it always was (`audio/inputGain`).
- Band edges, release times per hit and the floors live in the `sonic.response` JSON, not on the panel.

### Per-note MIDI

Every value is a pure function of time over the note track, the same for a file and live:
- **Voice slots.** A note takes the lowest slot free at its start and owns it for its life; with all 8 busy it takes
  the slot whose note began first. Slots are assigned at note-on from what is known then, so a file and the same notes
  played live give the same slots (tested). Signals: `notes.voice.<0..7>.{held, velocity, pitch, age}` and the event
  `notes.voice.<i>.on`.
- **Pitch-class lanes.** `notes.class.<0..11>` is the loudest sounding velocity of that class (C = 0); the event is
  `notes.classOn.<k>`.
- **Scalars:**
  - `notes.lastPitch` (the latest note-on; the coordinator's `onPitch` is this name) and `notes.lastVelocity`;
  - `notes.interval`, signed, an octave = 1;
  - `notes.lowest` and `notes.highest`, the sounding range;
  - `notes.velocitySpread` and `notes.channel`;
  - `notes.held`, the longest sounding note's held time, log 0.05-4 s, which tells a pad from a stab.
- **Events:** `notes.release` (strength = the note's duration, log) and `notes.low`/`notes.high` (either side of
  `sonic.response.splitKey`, default 60).

### Interpreter hard ranges

The bias range is now +-64 and the gain range +-256 (they were +-4 and +-16), so a mapping can be a narrow register
bump: bias -20, gain 33 lights only near 0.6. The soft (UI) ranges are unchanged, and existing values sit inside the
old ranges.

## Determinism

- The response steps on the hop clock inside `SonicRuntime::step` and is replayed from zero on a backward seek.
- The note envelope and every per-note fact are functions of time.
- Controls are read each frame before the clock steps. With constant controls, seek equals play (tested, 11 signals to
  1e-5). With keyed controls, a seek replays at the target's value, and envelopes may differ for up to one release
  time.
- All names are appended after `notes.phrase` (tested), so every older signal id is unchanged.

## Measured (test kit, 8 bars, neutral controls)

- Kicks are 95% or more on the placed hits with at most one extra; snares 95% or more; hats 85% or more.
- `response.low` fires on 80% or more of the bass notes; `response.kick` on 10% or fewer.
- A pad alone has a mean sustain above 0.2 and more than 2x the drums'. The drums' mean transient is more than 3x the
  pad's.

## Consequences

- About 130 more names in the route picker, under `response.` and `notes.`.
- `--sonic-trace` writes `response.*` columns. Its event columns stay 0 (rows are written after `clearEvents`); use
  the `...Env` columns.
- **Live response.** The live runtime gets the same controls each frame. The Smoothing slider still scales only the
  character's tiers.
