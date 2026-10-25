# Talking Heads: final report (2026-10-25)

The owner approved stop 5 ("Looks good go with 1 recommended"). This is what followed, ready for the coordinator's
merge of `scene/talking-heads`.

## The owner's last notes

**"investigate this cut out look issue on the right hand head".** The ghosts caused it.
- **How it was found.** Each effect was isolated on the synth head at the same frame (102.93 s, `review.py --drop`,
  `--only`). With the ghosts removed the face was clean.
- **The cause.** The two spectral copies of the head sat on its surface: scale 0.999, and without the Hinge's mouth.
  Their flash offsets laid closed-mouthed patches across the open face.
- **The fix.** They now sit 3 cm behind the head and part sideways and back, never forward. Depth-tested against the
  head, they show only as an echo round its silhouette.
- **The rays.** They met at the upper lip, because their origin was at the slit's height while the "oh" opens
  downward. They now leave the middle of the opening; the anchor check passes at full opening, tilt 0 and 32.
- **Media:** `~/Desktop/av-gen-review/talking-heads/stop6/cutout/`.

**"it looks like its the synth stem - i think it needs to be more sensitive".**
`tools/talking_heads/gate_recall.py` scores a head's openings against an independent spectral-flux onset reference
of its own stem.

| Head | Recall before → after | Precision before → after | Open in silence |
|---|---|---|---|
| Synth | 47% → 68% | 38% → 44% | 1% |
| Bass | 68% → 72% | 95% → 97% | 0% |
| Drums (unchanged) | 33% | 100% | 0% |

- Synth and bass also open on their stem's `onsetStrength` (synth 0.8, bass 1.0, a 90 ms refractory). The level
  keeps the gate open while a note rings, and an onset while open is its own chomp.
- Lowering the level thresholds alone made the synth worse: legato notes never fall below them, so the gate stays
  open.
- The drums head is unchanged, as the owner asked.

## Option 1: the unlit fast path (ADR-1673)

A slim fragment entry, `fs_proc_unlit`, for unlit procedural surfaces that provably need no lit shading:
- unlit, opaque, untextured;
- no effect lanes and no refraction;
- a program with no screen-space inputs, no tangent, texture or field ops, no layers, and no normal, occlusion or
  height outputs.

Same colour within one level per channel. Byte-identical when off, and for every ineligible draw. Tested on the CPU
(the whitelist) and the GPU (taken, same image, never taken by lit or ineligible surfaces).

| Full scene, 1080p headless, Feline stems | Before | After |
|---|---|---|
| GPU p50 | 20.5-24.5 ms | 17.8 ms |
| GPU p95 | about 36 ms | 24.7 ms |

The background now costs about 3 ms, so the membranes did not need shrinking further.

## Registration and live acceptance

**Registration.** "Sonic Abstract - Talking Heads" is in `examples/index.json` with `"live": true`; the project has
`sonic.live`. It is number 10 in `tools/sonic_vfx/scenes/__init__.py` `ABSTRACT`, and its own index entry overrides
the generic one.

**Live acceptance**, on AC power with the LG UltraFine as the only display, at 60 Hz:
- **The paced display, checked first.** Event Horizon live gave 0 misses (baseline 3) and p99 36.7 ms (baseline
  40.2).
- **Talking Heads**, three live Auto reps of 900 frames from 60 s, plus a headless Ultra run (`tools/live_perf_baseline.py`):

  | Measure | Result |
  |---|---|
  | Deadline misses per 900 frames | 1, 1, 2 (median 1) |
  | Frame interval p99 | 37.3 / 37.7 / 37.9 ms |
  | Frames at Ultra | 100% |
  | Headless Ultra GPU | p50 14.5 ms, p95 18.3 ms |

  T1-T4 all pass.
- **The baseline row** is written to `docs/perf/live-baseline.json` with `write --add`, which keeps the other three
  rows.

**Suites.** Both exit 0 on the final tree (see the coordinator report). The first cpu-assets run caught the procedural
shader's third call site of the field evaluator (`[adr1561]`). The two fragment entries now share one helper.

## The scene's ADRs

| ADR | Decision |
|---|---|
| 1670 | A Light Beam can follow its owner (`local`, `pitch`) |
| 1671 | Light Beam's `castLight` is read as a Bool |
| 1672 | The Hinge deformer (the mouth), its signed mask offset, the lens and the purse (the "oh") |
| 1673 | The unlit fast path |
