# Beat clock pass 02: report

The plan is `01-plan.md`. ADRs 1780-1785. Media and raw results: `~/Desktop/av-gen-review/99-beat-clock/`.

## 1. The beat clock, before and after

Measured with the Audio Response Lab (`tools/audio_lab/lab.py run`): the file grid through the engine's own
`--sonic-trace`, the live tracker through the causal replay (`avgen_audio_lab_replay`). Lock is the lab's: |phase error|
under 0.1 beat and the tempo within 3% for four beats running, timed from the section start.

| Fixture | File grid before | File grid after | Live tracker before | Live tracker after |
|---|---|---|---|---|
| syncopated, 110 BPM | 144.6 BPM, F 0.28, never locks | 110.0 BPM, F 0.93, locked from the start | 144.6 BPM, F 0.19, never locks | 110.0 BPM, F 0.80, locks after 2.7 s |
| dynamic, 120 BPM (0-44 s) | 95.9 BPM (one tempo), F 0.41 | 120.0 BPM, F 1.00 over the file | 60.0 BPM first, F 0.80, locks after 22 s | 120.0 BPM, F 0.86, locks after 2.0 s |
| dynamic, 96 BPM (44-60 s) | 95.9 BPM | 96.0 BPM, locked at the step | 96.0 BPM, relocks in 3.75 s | 96.0 BPM, relocks in 4.4 s |
| dense, 128 BPM | 127.9 BPM, F 0.0 | 128.0 BPM, F 0.99 | 127.8 BPM, F 0.72, locks after 15 s | 128.0 BPM, F 0.95, locks after 2.3 s |
| isolation, 120 BPM | 120.0 BPM, F 0.99 | 120.0 BPM, F 0.99 | 119.9 BPM, F 0.67, locks after 12 s | 119.9 BPM, F 0.91, locks after 1.5 s |

**The dense fixture's F 0.0 was the fixture.** Its sections started at whole seconds (4, 8, 12 s), which are not on a
128 BPM grid, so from 12 s its kicks sat 0.6 of a beat off the manifest's intended beats; the tracker followed the
kicks. The fixture now starts each section on its own grid (generator version 2). The old binary on the corrected
fixture: file grid F 0.99, live F 0.76 locking after 10.3 s. The "before" row above is the old fixture.

**Silence (live tracker, causal replay).**

| Silence | Before: pulses in it, confidence | After: pulses in it, confidence under 0.1 after, lock back after |
|---|---|---|
| dynamic 27.8-34 s (6 s) | 11 pulses; under 0.1 after 4.3 s | 0 pulses; 0.37 s; lock back at once (0.0 s) |
| syncopated 13.4-15.8 s (rest bar) | 5 pulses; held 0.59-0.77 | 0; 0.34 s; 0.003 s |
| syncopated 24.4-26.7 s | 4 pulses; held 0.77-0.82 | 0; 0.74 s; 0.002 s |
| syncopated 30.8-32 s (end) | 1 pulse; held 0.74 | 0; 0.36 s |
| isolation 21.8-24 s (end) | 3 pulses; held 0.80-0.82 | 0; 0.36 s |

The gate closes when a beat falls due in silence (input under -60 dBFS for at least 0.15 s, no sound within 8% of a
period), or after a beat and a bit of silence whatever the beat. So the confidence is down 0.34-0.74 s into a silence:
about half a second when a beat falls due early in it, up to about a beat and a half when one has just passed. A click
track's gaps (0.49 s of digital silence at 120 BPM) never gate (`[adr1781]` test).

**BlackHole loopback (the live device).** See §5.

## 2. What made the difference

- **The input** (ADR-1780): a level-free, low-weighted log-band rise instead of the onset detector's ratio over its
  threshold. A stacked snare no longer doubles a beat's weight, and the -30 dB intro reads like the loud section.
- **The metrical score** (ADR-1780): the autocorrelation at L, 2L, 3L and 4L. The son clave's dotted eighths (4:3) and
  the backbeat's two-beat lag (half time) now lose to the beat on the whole bar's evidence.
- **The phase from the low register** (ADR-1780/1782): on the syncopated fixture the hats on the "and"s are a stronger
  pulse than the beat; the kick and the bass are not. The file grid then snaps each beat to the whole-band attack within
  an eighth of a period (the bass band's rise peaks 25-30 ms late: on Rebuild the grid sat +27 ms without the snap,
  +5 ms median with it; ADR-896's 30 ms bar-line test passes).
- **The tempo path** (ADR-1782) and **local `beat.bpm`**: the dynamic fixture's 96 BPM section is tracked as 96.
- **Live acquisition and hysteresis** (ADR-1781): a first estimate after 2 s, a lock confirmed over 4 s, a held lock that
  a syncopated or irregular bar does not move. The cost: the 120 -> 96 step relocks in 4.4 s, not 3.75 s.

## 3. Gates (ADR-1785)

The lab's beat findings are gates now (bounds in `lab.py`: `BEAT_F_FILE` 0.85, `BEAT_F_LIVE` 0.70, `LOCK_GATE_S` 8 s,
`SILENCE_FALL_S` 0.8 s, at most one beat in a silence, the lock back within 4 s). After: every fixture passes every gate
(sparse 34, dense 32, noisy 25, syncopated 42, dynamic 43, isolation 60, adversarial 34). Lock within 4 s stays a
finding (dynamic's 96 BPM step: 4.4 s).

## 4. Cross-scene transfer (`tools/audio_lab/transfer.py`, before and after)

Event Horizon, Rainbow World (its generated scene read from the owner's checkout) and, added for this pass, Sacred
Flight (12 `beat.*` routes). The same fixtures before and after.

| Scene | Fixture | `beat.bpm` median before -> after | Parameter finals that changed |
|---|---|---|---|
| Event Horizon | sparse / dense / dynamic | 195.8 -> 80.9 / 127.9 -> 128.3 / 95.9 -> 96.0 | 0 of 64 |
| Rainbow World | sparse / dense / dynamic | 195.8 -> 80.9 / 127.9 -> 127.8 / 95.9 -> 120.0 | 0 of 9 |
| Sacred Flight | each | as above | 12 of 199 (its beat-routed components) |

- Event Horizon and Rainbow World's audio-driven finals do not change: neither routes `beat.*` into a component the
  fixtures move.
- Sacred Flight's beat+interpreted family: the attacks answered within 120 ms go from 1.0 to 0.92 and a release becomes
  measurable (433 ms): its beat routes now pulse on the beat (dynamic: 120 BPM where it read 95.9) instead of a wrong
  grid. Stale-after-silence is unchanged (24 components, as before: the file grid runs on through silence by design,
  ADR-1581).
- The sparse fixture has no intended grid: both readings (195.8, 80.9) are trackers finding a pulse in four isolated
  hits.

## 5. BlackHole loopback

The live device path (`tools/audio_lab/loopback.sh`: the fixture played into BlackHole, AV Gen capturing it at
1080p30), on AC power (logged in each run's `conditions.log`; the owner confirmed AC for this session).

| Fixture | Before (03-final-report, 2026-10-26) | After |
|---|---|---|
| syncopated, 110 BPM | F 0.15, never locks | F 0.83, 110.0 BPM, locks after 3.3 s |
| dynamic, 120 BPM | locks after 25.5 s | F 0.84, 120.0 BPM, locks after 3.5 s |
| dynamic, step to 96 BPM | relocks in 3.75 s | relocks in 5.0 s |
| silences | (not reported) | 0-1 pulses; confidence under 0.1 after 0.35-0.73 s; the lock back after 0-1.1 s |

Latency is unchanged by this pass: sound to bus 39-40 ms median, 49-57 ms p95.

## 6. Not verified by eye

- The transport bar's **Mark beat** button (ADR-1783): built, its engine side tested (`[adr1783]`), the ImGui control
  never seen.
- The changed tooltips (Inputs table and stem Role, the Stems heading).
- The Audio Response Lab scene without its kick/snare/hat lamps: regenerated, checked by
  `tools/check_project_integrity.py`, not looked at.
