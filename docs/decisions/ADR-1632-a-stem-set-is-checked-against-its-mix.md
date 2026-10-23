# ADR-1632: A stem set is checked against its mix, and a misaligned stem is named

- Status: Accepted (eng/stem-roles; the stem-analysis milestone's recommendation 5,
  `docs/prototypes/living-prism/17-stem-analysis-milestone.md` §4; merge after the owner's approval)
- Extends: ADR-1520 (stems, `checkStem`, `mixDiagnostics`).
- Code:
  - `src/audio/stem_source.{hpp,cpp}`: `stemProbeFactor`, `StemProbeWriter`, `stemProbe`, `checkStemSet`,
    `StemSetCheck`, and `StemFileFacts::probe`;
  - `src/analysis/stem_analysis.cpp`: `analyzeFile` writes the probe while it decodes;
  - `src/app/engine.{hpp,cpp}`:
    - `StemState::probe`;
    - `refreshStemMixDiagnostics` runs the check whenever the mix or the stems change;
    - its per-stem findings join each stem's `mix` diagnostics, the Stems section and the project warnings;
    - `stemSetCheck()` returns the result.
- Tests: `[adr1632]` in `tests/integration/test_stem_roles.cpp`. The fixture's `bass_late.wav` (the bass 250 ms late
  in its file) is the deliberately offset stem.

## Problem

A wrong offset was silent (17 §3, recommendation 5). `mixDiagnostics` compares a stem's span and rate with the mix's,
and says that matching lengths are not proof of alignment. A stem bounced from another session start, or the wrong
song's stem, published confident signals at the wrong instants.

## Decision

**Every stem keeps a probe.** The probe is the mono signal box-averaged to about 4 kHz: a factor of 12 at 48 kHz, the
same factor for every file of one rate. It costs 3.4 MB for 210 s, written while the stem is decoded.

The probe is linear, so the probe of a sum is the sum of the probes. A set that sums to its mix sums to the mix's probe
with the same residual, and a stem d seconds off is d seconds off in its probe.

**The check:**

| check | what | severity |
|---|---|---|
| rate | another rate than the mix: analysed at its own rate (`mixDiagnostics`, unchanged), and left out of the sum | info |
| length | the span against the mix's (`mixDiagnostics`, unchanged) | warning |
| channels | another channel count than the mix: analysed as mono, and a mono stem's width reads 0 | info |
| sum | the mix fitted as a weighted sum of the stems at their offsets (least squares, one gain per stem: a mix has faders), as a residual in dB below the mix | info: an incomplete set (no vocal stem) is not misaligned |
| lag | per stem, where it best matches what the other stems leave of the mix, within +-0.5 s | warning when not 0, naming the offset that aligns it; a warning too when it matches nothing |

**How the lag is searched:**
- The match is a normalised cross-correlation over the stem's 16 loudest seconds.
- It is searched coarse first (an eighth of the probe rate, every eighth lag), then refined at the probe rate around
  the coarse peak, and compared with lag 0.
- A lag is reported when it is at least 2 probe samples (0.5 ms) and correlates at least 0.02 better than 0.
- "Matches nothing" is a best correlation under 0.2.

**Nothing is corrected.** AV Gen never guesses an offset (ADR-1520). It warns, names the offset that would align the
stem, and the artist sets it.

## Evidence

| set | residual | lag found | warning |
|---|---|---|---|
| Feline's 3 stems, offsets 0 | 42.5 dB below the mix (17 §2 measured 40.4 with one global gain) | none | none |
| the same, bass at offset 0.025 | 4.5 dB | -25.0 ms (correlation 1.00 there, 0.51 at its offset) | "...the offset that aligns it is 0.0000 s" |
| the fixture's 4 stems, offsets 0 | 105 dB below (the mix is their exact weighted sum) | none | none |
| the fixture with `bass_late.wav` (250 ms late) at 0 | 1.3 dB | -0.250 s | "...250.0 ms earlier...: the offset that aligns it is -0.2500 s" |
| `bass_late.wav` at its aligning offset -0.25 | 105 dB below | none | none (only `mixDiagnostics`' span note) |
| a silent stem, a 44.1 kHz stem, a mono stem | | none | the silent stem's own warning; the rate and channels as info |

- The fitted gains are the mix's faders. The fixture's centre-panned drums fit at 0.45 x cos(pi/4), as built.

## Consequences

- A project with stems spends the check's time at load and on every stem edit: 41 ms for Feline's three 210 s
  stems and 23-31 ms for the fixture's 20 s stems, plus 3.4 MB of probe per 210 s stem.
- A stem that legitimately differs from the mix (a stem from a remix, a stem re-amped later) can read as "does not
  match" or as misaligned. The warning says to check it; nothing stops it loading or playing.
- Periodic material can match a whole beat off when the offset is wrong by about a beat. The search stops at +-0.5 s,
  and the suggested offset is a suggestion.
