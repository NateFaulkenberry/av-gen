# ADR-1530: The Live panel drops its kick / snare / hat meters; the detection stays

- Status: Accepted (2026-10-19), eng/remove-ksh-detect. The owner chose option C1 of the blast-radius report.
- Amends: ADR-1062. Its Live panel Response group no longer has the four hit meters.
- Code: `src/ui/live_panel.cpp`. The meter row is gone, and the Transients and Input gain tooltips are reworded.
- Tests: `tests/unit/test_live_panel_no_hit_meters.cpp` (`[adr1530]`).
- Report: `docs/development/remove-kick-snare-hat/01-blast-radius.md`.

## Context

The owner asked to remove the legacy kick / snare / hi-hat detection "from AV Gen's live panel". Live multichannel
source assignment will make it redundant: the stems work's Inputs table has a per-source **Hits** toggle.

The audit followed the data. The panel's only kick/snare/hat surface was a row of four progress bars under Response,
showing `response.kickEnv`, `snareEnv`, `hatEnv` and `noteEnv`. The detector behind that row is not exclusive to it.

`CausalOnsetDetector` (ADR-1060/1067/1068) runs on every hop of every live and offline analysis. It is:
- the only source of the dB levels behind ADR-1510's response levels;
- the source of `response.flux`;
- the source of the live input's `audio.onsetLow/Mid/High`, which also feed the GPU onset fields and Astral Forge Live;
- the classifier the stems hits tier reuses.

The hit signals it publishes are routed 394 times in 47 example files. Those include every Sonic Abstract and Sonic
VFX scene, the Resonator and Generative scenes, Digital Mosh Live and THE RIFT LIVE.

## Decision

Remove the panel feature, not the processing:
- **The meter row is deleted, all four meters.** The note meter goes too: a lone note bar would read as a leftover, and
  the MIDI section already shows the last note and the count.
- **The tooltips are reworded.** Transients now says it scales "the hit signals and MIDI notes", not "kicks, snares,
  hats". The Input gain tooltip no longer cites "the hit detector".
- **Nothing else changes:**
  - the detector, `ResponseModel` and every `response.*` and `audio.onset*` name and meaning;
  - the five `sonic/response/*` parameters, their `sonic.response.*` project keys and the live-switch carry;
  - MIDI, the BlackHole 2ch path, offline analysis and the stems work.

  No project key is removed, so there is nothing to strip under ADR-441.

## Why the processing stays

Removing the detector, or only its kick/snare/hat classes, would silence the core reactivity of most live scenes and
change ADR-1510's levels. Its classes are also what the stems hits tier reuses. That would be a design change, not
cleanup. The cost it keeps is 78-84 us per analysis hop, under 1% of one core, on the analysis thread rather than the
audio callback (measured in the report, §7).

## Consequences

- The performer no longer sees at a glance whether hits fire on their input. The route picker and Signals view still
  show the envelopes. The intended replacement is source assignment: the Inputs table's per-source Hits.
- `docs/help/audio-signals.md` no longer says live input has no `audio.onsetLow/Mid/High`; it has had them since
  ADR-1060.
