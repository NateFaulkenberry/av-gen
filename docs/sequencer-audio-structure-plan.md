# Sequencer-centred audio, song structure and Director integration — plan

**Status:** **A, B, C and D done** (ADR-215, ADR-216, 2026-09-15). Generation -- the second half of
D -- is defined as a seam (`seq/section_direction.hpp`) and waits on the Director decision layer
(`docs/director-poc-plan.md`) to supply a `SectionDirectionTable`. See "What was built" at the foot
of this file.
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
  judge. A test that asserts the analyzer finds a Chorus at 1:02.4 is a test of the analyzer's
  current opinion, not of correctness.
- The transport still works after the Control-section controls are deleted — including with no audio
  loaded, which is the ADR-102 case.

---

# Addendum — research-grounded structure detection (MSA)

The detector is upgraded from the existing fold to a real Music Structure Analysis pipeline:
beat-synchronous features → self-similarity → Foote novelty → boundaries → recurrence grouping →
functional labelling with confidence. References: Foote (2000) *Automatic Audio Segmentation Using
a Measure of Audio Novelty*; FMP C4S4 novelty-based segmentation; librosa's `recurrence_matrix`;
MIREX Structural Segmentation.

**Boundary detection and functional labelling are two problems and must not be conflated.** A
boundary can be reliable while its label is a guess, and the model has to be able to say
`47.312 → 71.842, likely Chorus, confidence 0.87` rather than asserting the label.

## What the analyzer already gives us

This is better than expected, and it decides the design.

| Needed for MSA | Already in `src/analysis/` |
|---|---|
| Per-frame spectrum | `AnalysisFrame::magnitude` — linear bin magnitudes, sine-normalised. Chroma and a timbre vector fold out of this; **no new FFT infrastructure is required** |
| Onset / spectral flux | `flux`, `onsetStrength`, `onset` (peak-picked) |
| Energy | `rms`, `peak`, `bands`, `bandsRaw` |
| Timbre proxy | `centroidHz`, `centroidNorm`, the band array |
| **Beat times for the whole track** | `OfflineBeats::beatTimes` — **Ellis (2007) dynamic-programming beat tracking is already implemented** (`trackBeatsOffline`), with a tempogram helper and a confidence |
| Whole-track offline pass | `AnalysisTrack::analyze(file, config, beatConfig)` |

Beat-synchronous aggregation is the prerequisite for everything else in a Foote pipeline, and the
beat grid is already there. That is the expensive half.

## What has to be built

A new `src/analysis/structure.{hpp,cpp}` — new files, in a directory no in-flight branch touches,
so this is the one part of both briefs that can start immediately.

1. **Chroma**, folding `magnitude` bins onto twelve pitch classes; and a compact **timbre** vector
   (log-band energies, roughly MFCC-shaped). Both normalised per frame.
2. **Beat-synchronous aggregation** — median of each feature over each beat interval, so the matrix
   is beats square rather than hops square. A four-minute track is ~500 beats, so a dense 500×500
   SSM is trivial; at hop resolution it would be ~20,000² and is not.
3. **Self-similarity matrices**, one per feature family (harmonic, timbral, energetic), combined
   with weights. Cosine similarity, then a percentile-based normalisation so a quiet track and a
   loud one produce comparable matrices.
4. **Foote novelty** — a checkerboard kernel convolved down the SSM diagonal, at two or three kernel
   widths so both a 4-bar and a 32-bar change are visible. Peak-picking gives candidate boundaries.
5. **Recurrence grouping** — thresholded nearest-neighbour recurrence between segments to find
   repetition families, so Verse 1 / Verse 2 and the repeated choruses group together.
6. **Functional labelling** from structural evidence, not from one heuristic: position in the piece,
   repetition-family membership and how often the family recurs, energy and density relative to the
   track's own distribution, and what precedes and follows. Genre robustness is a requirement, not a
   nicety: on an ambient track the honest answer is `Section A / Section B` with good boundaries,
   **not** a confidently hallucinated Verse/Chorus.
7. **Confidence**, separately for each boundary and for each label.

### Metrics must not be fabricated

The brief is explicit and it is the right rule: do not report `tension = 0.83` because a formula
produced a number. `energy` and `density` have defensible definitions from the existing features.
`tension` does not, yet — so it is either derived from something real (rising energy plus rising
density plus harmonic instability across a window) or it is not reported at all. An absent metric is
better than an invented one, and this project has already paid for the opposite choice.

## Testing this honestly

The hard part is that **there is no ground truth in the repository**, and a test asserting the
analyzer finds a chorus at 1:02.4 tests the analyzer's current opinion, not correctness.

So: **structural invariants, and synthetic fixtures with known construction.** A generated signal of
the form `A B A B C A` — distinct chroma and timbre per letter, at a fixed tempo — has boundaries
that are known exactly because they were placed, and a repetition grouping that is known because it
was built. That tests the pipeline without pretending to test musical judgement. Invariants worth
asserting on any input: sections are ordered, non-overlapping, gapless, cover the track, carry
sub-second precision, and are deterministic for identical input.

Detection quality on real music is *reported*, not asserted.

## Performance and caching

Analysis runs once, asynchronously, on import — never per frame. The result is cached in the project
so reopening does not recompute. Determinism is required: the same audio must produce the same
boundaries, which rules out anything seeded by wall-clock or thread scheduling.

---

# What was built, 2026-09-15

Against the four gaps above.

**A -- the editable model.** ADR-215. `seq::Sequence` carries an `analysis::SongStructure` and saves
it in its own JSON, so it is part of the project. `seq::reanalyze` replaces `Detected` sections,
keeps `Refined` and `Authored` ones exactly, cuts the fresh detection around them, and reports what
it kept. Editing marks what it touched. The §17 test asserts both halves directly: a moved boundary
survives a re-run that changed its mind about everything, and a boundary with detected material on
both sides is replaced.

One thing the tests found and the ADR records: provenance is per *section*, so dragging one boundary
protects the two sections it separates and therefore three boundaries. Over-protective on purpose.

**B -- the vocabulary.** `MusicalSection` gains PreChorus, Chorus, Break, Bridge, Instrumental and
FinalChorus. All four downstream switches answer every one of them explicitly; two are now exhaustive
switches with no `default` so `-Wswitch` catches the next addition, and the other two are pinned by a
test because their fall-through values (`Establish`, emphasis `0`) are also legitimate answers. The
relationship to `analysis::SectionFunction` is stated, one-way and total, with `Other -> Phrase`.

**C -- the consolidation.** ADR-216. Import moved to the Sequencer toolbar; the File menu item and
`O` stay and share the callback; the Control section's compact transport and Open Audio button are
gone. Nothing that remains is gated on `hasAudio` except the volume.

**D -- sections as regions.** A lane under the ruler, boundaries draggable, names and types editable,
split and delete, with its own optional free/beat/bar snap that assigns a beat's own value rather
than a rounded one. Detection runs on the `JobSystem`, once on import, cached in the project.

**Not built:** generation. `seq/section_direction.hpp` defines the event shape and leaves the
behaviour table as the hole, declines everything today, and names each section kind it had no
direction for.
