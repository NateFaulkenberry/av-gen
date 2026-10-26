# ADR-1667: A project's notes file can be re-timed onto the audio, and can be optional

- **Status:** Accepted (scene/aurora-borealis).
- **Extends:** ADR-1020 (`sonic.notes`: a MIDI file's notes as `notes.*` signals).
- **Asked for by** the Aurora Borealis owner, pass 10: "I've bounced a BASS.mid midi file ... use that to perfectly line
  up the visuals with the bass notes ... the track itself is varispeed 3%".

## Code

- **`src/sonic/sonic_runtime.{hpp,cpp}`:**
  - `sonic.notes` may be a path, as before, or `{path, scale, offset, optional}`.
  - Every note's start becomes `start x scale + offset` and its duration `duration x scale`.
  - With `optional`, a missing file gives no notes instead of an error (`SonicSetup::notesMissing`).
- **`src/app/engine.cpp`:**
  - A save keeps the object form and rewrites only its path.
  - A missing optional file is logged as a warning.

## Tests

`tests/unit/test_sonic.cpp` (`[adr1667]`):

- the string form, unchanged;
- the object form, re-timed;
- a missing file: an error unless it is optional;
- a bad scale is refused.

## Context

Pass 9 detected the bass's notes from the stem's audio, with recall 0.94. The owner bounced the session's MIDI so the
lights would land exactly on the notes, but the file's own clock does not match the audio, for two reasons:

- **Varispeed.** The track plays with 3% varispeed: 83 BPM in the session, about 85.49 BPM heard.
- **Session timeline.** The session starts mid-timeline: the first note is at bar 40.8. The file's one tempo event
  (83 BPM) sits at tick 74880, so a reader times everything before it at the default 120 BPM.

ADR-1020 read the file's tempo map and had no way to move or stretch it. It also refused a missing file, so a project
that names a gitignored file (stems and their MIDI are never committed) would not open on a clean checkout.

## Decision

Two numbers and a flag on the notes:

- **scale and offset**, which map the file's own times onto the audio;
- **optional**, which makes an absent file mean no notes.

**This is the existing note path.** The notes still arrive as `notes.*` (`notes.noteOn`, `notes.active`,
`notes.lastVelocity`, ...), and the conditioned signals read them as they read any bus signal.

**Aurora Borealis** fits its scale and offset to BASS.wav's onsets by least squares:

- scale 0.970905, which is 1/1.03 to within 0.003%;
- offset -75.7446 s;
- residual median 1.5 ms;
- no drift.

All 216 notes fire. The audio onsets remain the fallback while no MIDI note has sounded for 4 s, which covers a missing
file.

## Rejected

- **Writing a re-timed copy of the MIDI file.** It is a second file to keep in step, it is still gitignored, and the
  project would still not open without it.
- **Putting the notes' times in the project as timeline hits.** That copies the owner's MIDI into a committed file;
  the MIDI sits beside the stems, and those are never committed.
- **A fixed-tempo override.** All the notes come after the file's one tempo event, so its times are already linear,
  and scale and offset are enough.
