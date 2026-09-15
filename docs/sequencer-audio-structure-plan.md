# Sequencer-centred audio, song structure and Director integration — plan

**Status:** planned, queued behind the Director POC (`docs/director-poc-plan.md`), which is itself
queued behind three in-flight branches.
**Written:** 2026-09-14

## Again, most of the architecture already exists

Read before planning, because the previous brief turned out to be 80% built already. So is this one.

| The brief asks for | What exists today |
|---|---|
| Waveform on the sequencer timeline | `SequencePanel::drawStrip` already draws one, with audio clips in a lane beneath it (ADR-103). The layout rules are written down there and were learned the hard way — clips drawn *on* the waveform stole the scrub click. |
| Sections as real sequencer regions | `seq::Marker` with `MarkerKind::Section`, populated by `Sequence::setSectionMarkers(structure)` |
| Director responds to section events | **`TriggerKind::Section` already exists** — "a musical section begins (by name; empty = any)" — alongside `Beat`, `Bar`, `Cue`, `ShotStart/End`, `ActionComplete`, `VolumeEnter/Exit` |
| Director responds to continuous musical values | the signal bus: `audio.beat`, `audio.bass`, `audio.rms`, `audio.onset`, `audio.lowMid`, already used throughout `glowmere-valley-2.scene.json` |
| An event/track abstraction to extend | `seq::SequenceEvent` with `Trigger`, `EventAction`, `EventSchedule`, `Firing` |
| Section detection | `signals::MusicalStructure::fromMoments`, `StructureSettings`, `app::structureOfTrack` |
| One canonical transport | `TransportBar::draw(engine)` — a shared widget, so de-duplicating is deleting a call, not rewriting a control |
| Precise timing | `StructureSection` is already `double` seconds, not bars |

The brief's §11 — "do NOT generate thousands of keyframes" — is also already the project's position:
the auto-director is a bake to timeline keys, and ADR-091's `Director → Cinematic Action → Behavior
→ Navigation` is a runtime hierarchy, not a keyframe dump.

## What is actually missing

Four things, and they are the whole job.

### A. Sections are derived, not authored (§6, §7, §8, §17)

`setSectionMarkers` **replaces** the marker list's Section entries from the fold. There is no
provenance, no confidence, and no persistence, so:

* a user cannot drag a boundary from 01:02.4 to 01:03.1 and have it survive anything;
* re-running analysis silently destroys refinements — exactly what §17 forbids;
* nothing distinguishes "the algorithm guessed this" from "the user decided this".

**The fix is a model change, not a UI change.** A section needs `origin` (Detected | Refined |
Authored) and `confidence`, and the structure needs to be part of the saved project rather than
recomputed at load. Re-analysis then has a defensible policy: replace `Detected` sections, keep
`Refined` and `Authored` ones, and report what it left alone. That is the one piece worth building
first, because every other part of this brief reads it.

### B. The section vocabulary is short (§5)

`MusicalSection` has nine kinds: Intro, Build, Phrase, Drop, Verse, Breakdown, FinalBuild, FinalDrop,
Outro. The brief names twelve, adding Pre-Chorus, Chorus, Break, Bridge, Instrumental, Final Chorus.

Extending the enum is cheap; **the risk is downstream**, because `shotKindForSection`,
`isDropSection`, `mayBeSplit` and `emphasisFor` all switch on it and a new kind that falls through a
`default` produces a silently bland shot. Each addition needs a deliberate answer in all four, and
a test that every enumerator is handled.

Note also §5's own caution, which the code should honour literally: the labels are an interpretation.
`Detected` sections carry confidence; a user's rename makes it `Refined` and confidence stops
meaning anything.

### C. Two transports and two import buttons (§1, §2)

`control_panel.cpp` has an "Open Audio" button (~line 1002) and `transport.draw(engine, true)`
(~line 1018), plus a playhead readout and another `transport.draw(engine)` above the timeline. The
Sequencer has its own. Because `TransportBar` is a shared widget, the consolidation is: delete the
Control-section duplicates, move the import entry point into the Sequencer toolbar, keep the menu
item (`Open Audio... (O)`) as the keyboard path.

**Care required:** the menu item and the keyboard shortcut are not duplicates of each other, and
ADR-102 established that a project without audio still has a transport. Removing the button must not
re-introduce "transport disabled when no audio", which was a bug once already.

### D. Generation (§10, §12) is gated on the Director

"Generate initial Director sequence" produces `SequenceEvent`s whose triggers are
`TriggerKind::Section` and whose actions start director behaviours. It cannot be built before the
Director decision layer from `docs/director-poc-plan.md`, and it should be a thin translation on top
of it — a table from section kind to behaviour, not a second director.

## Order

1. Merge `agent/farm-animals`, `agent/anim-cleanup`, `agent/world-effects`.
2. **A** — the editable section model with provenance, persistence and the re-analysis policy. New
   code plus `src/signals/musical_events.*` and `src/seq/sequence.*`. Independent of everything
   else in both briefs; this is the first thing to build.
3. **B** — the section vocabulary, with the four downstream switches answered deliberately.
4. **C** — the UI consolidation. Touches `src/ui/control_panel.cpp`, so it waits for the
   world-effects branch.
5. The Director decision layer and the UFO POC (`docs/director-poc-plan.md`).
6. **D** — generation, then the Glowmere end-to-end path in §15 and §19.

## What to measure, not assume

- Re-analysis preserves refinements: assert directly that a `Refined` boundary survives a re-run and
  that a `Detected` one is replaced. §17 is a data-loss requirement and deserves a test that would
  fail if it regressed.
- Detection quality is *not* a pass/fail: report the sections found on a known track and let a human
  judge. A test that asserts the analyser finds a Chorus at 1:02.4 is a test of the analyser's
  current opinion, not of correctness.
- The transport still works after the Control-section controls are deleted — including with no audio
  loaded, which is the ADR-102 case.
