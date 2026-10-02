# ADR-1067: Drums under a mix: percussive flux, a snare that must last, and a kick with a period

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion; the art agent's measurement)
- **Supersedes:** ADR-1060's class rules (its causal framework, live wiring and latch stand).
- **Code:** `src/analysis/causal_onsets.*`; `ResponseModel::step` takes the detector's decisions.
- **Tests:** `tests/unit/test_drum_recall.cpp` (`[adr1067]`, the art agent's matrix as a regression test, on a C++
  port of its material in `tests/support/sonic_mix.hpp`), plus the updated `[adr1060]` and `[adr1062]` cases.

## Context

The art agent measured ADR-1060's live hits on its test material (`tools/sonic_vfx/drum_recall.py`). They collapsed
once a pad or a bass played under the drums: in the full mix, 1/32 kicks, 0/16 snares and 13/105 hats. Hats were weak
even alone (23/105).

ADR-1060's class gates were shares of the frame (the low band's share of the summed flux, the snare band's share of
the power), and a sustained part fills the frame. The response model also re-picked hits with its own thresholds and a
strength floor, which hid most of the hats under the 0.3 envelope test.

## Decision

1. **Percussive flux.** `P(k)` is the median of the per-bin SuperFlux over ±8 bins: the frequency-median half of
   HPSS, causal by construction. A pad's or a bass line's change lives in a few partials and is removed; a drum's
   attack is broad and survives.
2. **Hats** are percussive-flux attacks in 7-16 kHz over their own causal median, with no share gate.
3. **Snares** are percussive-flux attacks in 1.5-5 kHz, confirmed **4 hops (43 ms) later**. The band's noise floor
   (the mean of the frequency-median magnitude) must then stand at least 4.5 dB over its level just before the attack,
   and within 6 dB of its peak at the attack. A snare's noise lasts; a kick's click and a closed hat's tick do not.
4. **Kicks** are decided one hop after a local maximum of the low band's rise:
   - **The rise:** the larger of 40-120 Hz and 30-70 Hz, in dB, over the band's minimum of the previous three hops.
     It is floored at -60 dBFS, counts only once the band reaches -45 dBFS, and only while the band is within 12 dB of
     the frame's level on the candidate and on the decision hop.
   - **The score:** `min(rise/6, 1) + min(click/0.01, 1.5) + 0.9 onGrid`; a kick at 1.6 or more, or at a rise of 8 dB
     with any click.
     - The **click** is the percussive flux in 1.5-5 kHz over the three hops before the peak, because a kick's sweep
       falls into the band two or three hops after the beater.
     - **onGrid** means within 30 ms of the period the recent kicks keep: a comb over the pairwise differences of the
       last 4 s of accepted kicks, taking the longest period explaining 90% as much as the best. It carries
       four-on-the-floor kicks whose click a pad has buried. It never suppresses a kick off the grid.
5. **The response model takes the detector's decisions.** The performer's Transient sensitivity shapes their strength;
   below neutral it drops the weakest (at 0.25, those under 0.3), and above it no hit is invented. A hit that fires
   shows: its strength is at least 0.35.

## Measured

**The art agent's own renders, through the engine** (`drum_recall.py --avgen build/release/src/avgen`). Each cell is
found/total, with false hits in parentheses:

| mix | kick | snare/clap | hat |
|---|---|---|---|
| drums | 31/32 (+0) | 16/16 (+0) | 103/105 (+13) |
| +lead | 31/32 (+0) | 16/16 (+1) | 103/105 (+13) |
| +bass | 31/32 (+3) | 16/16 (+8) | 100/105 (+16) |
| +pads | 28/32 (+0) | 16/16 (+0) | 98/105 (+16) |
| full | 29/32 (+4) | 16/16 (+1) | 98/105 (+17) |

The 13-17 "false" hats include the loop's 15 open hats, which the tool's truth leaves out.

**The C++ port (the regression test).** Same voices, lines and levels; its own noise and phases:

| mix | kick | snare/clap | hat | bass notes off the kicks read as kicks |
|---|---|---|---|---|
| drums | 31/32 (+0) | 16/16 (+0) | 103/105 (+17) | - |
| +lead | 31/32 (+0) | 16/16 (+1) | 102/105 (+16) | - |
| +bass | 30/32 (+8) | 16/16 (+5) | 101/105 (+17) | 6/24 |
| +pads | 29/32 (+0) | 16/16 (+0) | 100/105 (+16) | - |
| full | 28/32 (+8) | 16/16 (+0) | 98/105 (+16) | 3/24 |

- **Without drums:** the pad bed fires nothing. The pad, the bass and the lead together fire 3 kicks and 1 snare in
  16 s.
- **Against the coordinator's targets, on the full mix:**
  - kick: 91% on the art agent's render and 88% on the port (target 90%);
  - snare: 100% (target 85%);
  - hat: 93% (target 70%);
  - bass notes off the kicks reading as kicks: 3/24 (12.5%) in the full mix (target 10%). The bass-only mix has
    6/24 on the port and 3 false on the art agent's render.
- **Latency:** hats on their attack; kicks decided about 20 ms after the attack; snares about 30-45 ms after.
- **Cost:** the frequency medians add about 1,250 17-element medians a frame.

## Limits (measured, not hidden)

- **A bright, clicky bass reads as a kick.** A sine bass with a 4 ms attack alone in the frame (ADR-898's groove) does
  so on about half its notes, and so does an alias-free saw pluck whose filter opens to 1.2 kHz. In both, the attack
  is broadband in 1.5-5 kHz, which is what a beater's click looks like. The tests record the counts (`WARN`); the art
  agent's darker, driven bass is the regression case.
- **Snare false hits on sustained broadband noise in 1.5-5 kHz.** A white-noise open hat that reaches down to 1.5 kHz
  reads as a snare.
- **Melodic material.** The arpeggio and keys-stab cases are measured in ADR-1068 (next).
