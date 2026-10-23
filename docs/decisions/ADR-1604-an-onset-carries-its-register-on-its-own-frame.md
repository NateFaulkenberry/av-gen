# ADR-1604: An onset carries its register on its own frame

- Status: Accepted (eng/living-prism-p1, The Living Prism P1-1)
- Extends ADR-1520 (the stem vocabulary), ADR-1060/1067 (the causal onsets), ADR-1580 (feeds). Additive.
- Code:
  - `src/analysis/causal_onsets.{hpp,cpp}`: `registerShares(now, previous, binHz)`; `CausalOnsets::onsetRegister`
    per hop;
  - `src/analysis/stem_analysis.{hpp,cpp}`: `StemFrame::onsetRegister`, the latest onset's, held;
  - `src/analysis/response_feed.{hpp,cpp}`: the register travels with a fed onset (`fromCausal`, `fold`, `mergeFeeds`);
  - `src/signals/stem_signals.{hpp,cpp}`: `stem.<key>.onsetLow`, `.onsetMid`, `.onsetHigh`;
  - `src/sonic/response.{hpp,cpp}`: `response.onsetLow`, `.onsetMid`, `.onsetHigh` (declared after every older id).
- Tests: `tests/unit/test_stem_signals.cpp` (`[adr1604]`: a body reads low, a hat high, energy weighting, a band that
  lost power takes no share; the vocabulary is 19 names).

## Problem

The audit (02 §2.3, §9.1, G3) found class hits (kick, snare) 28-44 ms late by construction: a kick is decided a hop
after its rise peaks, a snare 43 ms after its attack when its noise floor confirms it. The fast path for impacts is the
broadband onset (`stem.<k>.onset`, `response.onset`), but it said nothing about what kind of attack it was, so a scene
could not size or colour an impact by register without waiting for the class.

## Decision

Every onset carries a register: how the power its hop gained divides between low (30-250 Hz), mid (250 Hz-4 kHz) and
high (4-16 kHz) -- the per-band increase in summed squared magnitude since the previous hop, floored at 0 and
normalised to sum to 1 (all 0 when nothing gained). It is published on the onset's own frame and held until the next
onset, so a route on the onset can read it as a depth or a remap on the frame it fires and through its envelope.

Energy-weighted, not flux-weighted: a first version used each band's mean log flux, which reads "where something new
appeared" rather than "where the attack's energy is". It read a snare with a 190 Hz body as 0.76 low (the body rises
from silence as far, in dB, as the noise does); energy weighting reads it 0.94 high.

## Evidence (probe B1, the synthetic kit with known hit times, 30 and 60 fps identical)

| source | kicks (median low / mid / high; low share > 0.6) | snares |
|---|---|---|
| `stem.drums.onset*` | **0.85 / 0.04 / 0.10; 100%** of 36 | 0.00 / 0.06 / **0.94** (high) |
| `response.onset*` (the mix: drums and a bass line) | 0.81 / 0.02 / 0.14; 61% | 0.69 / 0.01 / 0.28 |

The separated source meets the audit's criterion (a kick reads low share > 0.6) on every kick. The mix's register is
contaminated by what else is sounding: the bass line's notes and beating add low-band gain on the snare's hop. That is
the owner's choice (separate stems, §14 item 5) made measurable: the register is honest on a source and approximate on
a mix.

## Consequences

- Impacts can be authored on the fast onset and shaped by register at once; the class arrives later for tails and
  counts (P1-8's phase-out uses this on a mix).
- Three more signals per stem and three more `response.*`; the sonic trace and the live log carry them.
