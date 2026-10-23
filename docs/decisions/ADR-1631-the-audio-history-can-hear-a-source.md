# ADR-1631: The audio history can hear a source

- Status: Accepted (eng/stem-roles; the stem-analysis milestone's recommendation 3, audit P1-4,
  `docs/prototypes/living-prism/17-stem-analysis-milestone.md` §4; merge after the owner's approval)
- Extends: ADR-1116 (the audio history), ADR-1361 (heard through the Response controls), ADR-1580 (feeds), ADR-1630
  (roles).
- Code:
  - `src/audio/feeds.{hpp,cpp}`: `history`, `historySpectrum` and `historyOnsets` (`spectrumSource()`,
    `onsetsSource()`, `anyHistory()`);
  - `src/analysis/stem_analysis.{hpp,cpp}`: `StemFrame::spectrumRow`, each hop's `foldSpectrum` row, raw;
  - `src/analysis/audio_history_builder.{hpp,cpp}`: `HistoryFrom`, `sourceOnsetRows`, and `buildAudioHistory(track,
    spectrum, onsets)`. `LiveAudioHistoryFeed::pushRaw` takes a row and onsets from any source;
  - `src/app/engine.cpp`: `updateAudioHistory`:
    - offline, it builds from the named stems, keyed on the feeds revision too;
    - live, it takes the named input source's newest row and onsets;
    - it runs after the input sources publish;
  - `rebuildFeeds` warns about a history feed that names nothing;
  - `src/ui/feeds_row.cpp`: the **History** combo.
- Tests: `[adr1631]` in `tests/integration/test_stem_roles.cpp`.

## Problem

`AudioHistory` holds the GPU spectrogram ring and the onset rows that spectrum and onset fields read: Resonator, the
generative fields, Cymatic World and THE ASTRAL FORGE's conductors. It was built from the mix's analysis frames only
(audit P1-4, the milestone's gap 4).

So a field could not be excited by the drums alone or the bass alone without scene-specific routes. Its `low` onset
row was also the mix's low-band onset, the detector ADR-1620 retired as a kick on the bus: on Feline it fires on bass
notes and hat clicks.

## Decision

**The history takes a source from the project's feeds.**

- `"history": "role.drums"` (or `"stem.<key>"`) names one source for both halves of the history.
- `historySpectrum` and `historyOnsets` override one half each, so the bass's spectrogram and the drums' onsets can
  be heard together.
- An empty value, or a key that names nothing (a project warning), is the mix's history.
- No field and no scene changes.

**What a source contributes:**

- **Spectrogram:** each hop's spectrum folded onto the history's 64-bin log axis (`StemFrame::spectrumRow`, computed
  in the analysis the stem already runs). It is stretched per bin over the source's own 20th-98th percentiles, as the
  mix's is.
  - It sits at the stem's offset, at the stem's own hop rate.
  - Before the stem it reads 0. After it, zero rows to the mix's end: a source past its span is silent, not held.
- **Onset rows** (`low`, `mid`, `high`):
  - a drums source (ADR-1630) gives its measured classes: kicks, snares, hats;
  - any other source gives its onsets on all three rows. A bass note is activity, not a kick, and a field reading
    any row hears it.
- **The beat row** stays the mix's (recommendation 6: the mix's grid stays the default).

**It stays seek-exact.** Offline (a loaded file, played or rendered), the history is still the whole-file build, a
pure function of the track, the stems and the feeds. It is rebuilt when any of them changes.

**Live,** with an input open, it is the named input source's newest row and the onsets it fired since the last
history row, on the mix's clock. An inactive source gives silent rows and no onsets. The feed restarts when the
source changes. A source missing from the input reads the mix.

**One history is uploaded, as before.** The source replaces the mix in the one ring and the one onset block, so the
GPU cost is unchanged: no second ring, no shader change. A per-field `audioSource` would need a ring per source,
and nothing yet asks for two at once.

## Evidence

**Feline Footwear, the whole song** (the engine's log line):

| history | low / mid / high / beat onsets | build | size |
|---|---|---|---|
| the mix (unchanged) | 232 / 439 / 794 / 221 (ADR-898's band onsets) | 45 ms | 5.1 MB |
| `"history": "role.drums"` | **406 / 235 / 547 / 221**: the drum stem's kicks, snares and hats, and the mix's beats | 8 ms | 5.1 MB |

- At 60 fps the trace sees 545 hats because two hats can share a frame. The history keeps every hop.
- `[adr1631]` checks the rows against the stem's own classifier, frame for frame: kicks, snares, hats, a non-drums
  source's onsets on all three rows, the offset, and zero past the stem's span.
- **Live** (the synthetic device, `"history": "role.bass"` on a click channel): 16 / 16 / 16 onsets in 8 s, one per
  click, on all three rows.

**The demo.** Not committed: `~/Desktop/av-gen-review/86-stem-roles/86-osc-membrane-history-AB.mp4`, Feline 44-58 s.
- **The scene:** the Resonator's oscillator membrane (`experiments/osc-membrane`), whose `kickFront` field reads the
  low onset row and `snareFront` the mid row. Its committed look is unchanged.
- **A:** the mix's history. **B:** the same project with the three stems and `"history": "role.drums"`.
- **What changes:** the fronts launch on the stem's kicks and snares.
- **Cost:** both render 420 frames at the same rate (19 s and 17 s at 960x540). One ring is uploaded either way.

## Consequences

- Each stem keeps 64 floats per hop for its spectrum rows: about 5 MB for a 210 s stem.
- A history fed from a drums source has no low-band onset that is not a kick. A scene tuned to the mix's busier `low`
  row fires less often from the stem.
- The Director's span profile, `TriggerClock`'s band triggers and Astral's `SongAnalysis` still read the mix's frames.
  They are outside this ADR's scope (17 §1).
