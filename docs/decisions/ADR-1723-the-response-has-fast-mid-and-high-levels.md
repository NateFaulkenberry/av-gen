# ADR-1723: The response has fast mid and high levels, each against its own reference

- Status: Accepted (eng/audio-response-lab; the owner approved it at Gate 1, 2026-10-26, for Prismatic Dream's
  high-band glints)
- Amends ADR-1510 (the levels read against an adaptive loudness reference) and ADR-1580 (the level family is routed by
  feeds). `audio.<band>Level` (ADR-897) is unchanged.
- Code:
  - `src/analysis/causal_onsets.{hpp,cpp}`: `CausalOnsets::midDb/highDb`, 250 Hz-4 kHz and 4-16 kHz, which are
    ADR-1604's register bands.
  - `src/analysis/response_feed.{hpp,cpp}`: carried, folded and merged with the level family.
  - `src/sonic/response.{hpp,cpp}`: `BandLevel`, `response.mid` and `response.high`; `midWindow`/`highWindow` and
    `mid/highAttack/Release` in `sonic.response`.
- Tests:
  - `tests/unit/test_response_bands.cpp` `[adr1723]`:
    - band selectivity;
    - a quiet band reads against itself, down to the -40 dB reference floor;
    - both fall to 0 within 0.3 s of silence;
    - the JSON settings.
  - `tests/integration/test_audio_lab_e2e.cpp` (dense): in a section with only one register, only that register's
    level reads.

## Problem

The response had one fast band level, `response.bass`. The mid and high bands were only available as
`audio.<band>Level`, a long-term level that falls 4.3 dB/s and takes up to 14 s to read 0 after the music stops (the
Gate 1 report, finding 6). A high-band glint built on it glows for seconds after the hats stop.

## Decision

**Two more levels, built like `response.bass`:**
- a dB follower: attack 10 ms / 5 ms, release 150 ms / 120 ms, times the Attack and Release controls;
- read through a window: top +6.5 dB, span 15 dB, moved by the Sensitivity control.

**Each reads against its OWN adaptive reference** (the band's power, with the broadband reference's warm-up, rise,
fall and -40 dB floor). The bass reads against the broadband reference. A mix's high band sits 15-30 dB under its
broadband level, and how far depends on the master, so against the broadband reference the hats of one track would
read near 0 and another's near 1. Against their own reference they read across the range in both.

They are declared after every older id. Routed by `feeds.levels`, they follow that source.

## Consequences

- **Measured on the dense fixture:**

  | Section | `response.mid` | `response.high` | the other bands |
  |---|---|---|---|
  | mid only | 0.76 | | 0 |
  | high only | | 0.48 | 0 |

  The slow `audio.midLevel` still read 0.52 in the high-only section.
- Both read 0 within 0.3 s of silence, inside the documented 0.13 s / 0.10 s at the reference floor, plus margin.
- A band that DROPS 20 dB reads quiet until its reference falls (a minute), which is ADR-1510's breakdown rule. A band
  under -40 dBFS reads lower because of the floor: 0.23 at -45 dBFS.
- Older traces and live logs have no `response.mid` or `response.high` columns.
