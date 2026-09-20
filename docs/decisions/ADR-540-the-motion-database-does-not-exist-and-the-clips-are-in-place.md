# ADR-540: The motion database does not exist, and every locomotion clip is in place

**Status:** Proposed (Phase 0 research; not to be implemented before review)
**Date:** 2026-09-20
**Related:** ADR-086 (skeletal animation), ADR-096 (actions and intent), ADR-161 (root motion is not
in this content), ADR-192 (the alien pack), ADR-260 (three positions), ADR-300 (the layer stack),
ADR-337 (root motion is a transfer of authority), ADR-359 (a leg is three names), ADR-385 (a stated
reason is not evidence)
**Full analysis:** `docs/design/autonomous-character-animation.md`

---

## Context

A brief asks AV Gen to evolve from playing authored clips to motion matching, learned motion
matching, neural state machines and scene-aware motion synthesis. The obvious first move is
classical motion matching, because it is deterministic, understandable and well documented.

Before choosing an algorithm, the content it would run on was measured.

## The measurement

Probes: `clipstats.py` and `rootpath.py`, reading `assets/aliens/alien-scout.glb` directly, and
`mmprobe.cpp`, a standalone nearest-neighbour benchmark (Apple M2 Max, `clang++ -O2`, minima over
200 repeats per ADR-170).

**The whole animated-character corpus of this repository is 26 alien clips and nine farm walk
cycles.** The alien pack is:

```
26 clips · 57.07 s · 6,942 channels · 156,921 keys · 1,712 frames at 30 fps
```

Every clip carries 267 channels — 89 joints × (translation, rotation, scale) — whatever actually
moves. The rig is shallow (depth histogram `{1:18, 2:33, 3:17, 4:13, 5:8}`), and every joint
carries a baked translation channel in every clip.

**Every locomotion clip is authored in place.** Total root excursion over the clip, `root.x`, in
the file's own metres:

| clip | X range | Y range | Z range | root path length |
|---|---|---|---|---|
| `Idle` | 0.0014 | 0.0052 | 0.0084 | 0.021 |
| `Walking` | 0.0322 | 0.0714 | 0.0212 | 0.303 |
| `Running` | 0.0341 | 0.0964 | 0.0534 | 0.460 |
| `Idle_turn` | 0.0213 | 0.0301 | 0.0185 | 0.128 |

Three centimetres of sway over a 1.03 s walk cycle is not travel. ADR-161 measured three Mixamo
clips and found the same thing; this is the same finding across the pack that replaced them.

Motion matching's central feature is the **future root trajectory** — root position and facing at
+0.33 s, +0.66 s and +1.0 s, in character space. It is computed from a root that travels. **There is
no root trajectory in this data to extract**, and there is no transition content either: one walk
cycle, one run cycle, no starts, no stops, no planted turns, no strafes.

For completeness, the search itself is cheap and is not the obstacle. Brute-force weighted-L2 over
a `[frames × D]` float array:

| frames | D=27 | D=51 |
|---:|---:|---:|
| 1,712 (this corpus) | 11.5 µs | 27.8 µs |
| 20,000 | 132.1 µs | 324.7 µs |
| 60,000 | 405.5 µs | 977.2 µs |

against ~2.97 µs to sample a clip and build this rig's joint palette.

A second measurement from the same probe **produced a wrong conclusion and is recorded here because
the shape of the error is the valuable part.** The probe found the textbook early-out — abandon a
row once its running cost exceeds the best so far — to be 2.0× to 2.7× *slower* at every size
tested. A second measurement on Holden's real `features.bin` found it a 12% *win*. Both are
correct; the follow-up probe (`mmprobe2.cpp`, 53,500 × 27) isolates why:

| database | query | plain | early-out | ratio |
|---|---|---:|---:|---:|
| white noise | far / random | 361.5 µs | 804.5 µs | 2.23× slower |
| white noise | near | 356.5 µs | 486.5 µs | 1.36× slower |
| random walk (motion-like) | far / random | 360.8 µs | 391.2 µs | 1.08× slower |
| **random walk (motion-like)** | **near** | **359.7 µs** | **288.0 µs** | **0.80× — a win** |

A motion database is a smooth trajectory through feature space and a motion-matching query is
always near the frame currently playing, so the real case is the last row. The first probe used a
Gaussian database and a random query — the first row — which does not occur. **The early-out's
value is a property of the fixture, and it must be measured on real extracted features.**

ADR-182 says a probe that cannot fail proves nothing. This probe could fail and did not, and the
conclusion was still wrong. The corollary worth adding: **a probe on a synthetic fixture proves
something about the synthetic fixture.**

Two measurements that are *not* in doubt, from the same follow-up on real data: the **two-level AABB
hierarchy** (boxes over groups of 16 and 64 consecutive frames, as in the LMM paper's Appendix B)
is **4.9× faster** than scalar-with-early-out for **+15.6% memory**, because it keeps the linear
scan's cache behaviour and merely declines to look inside boxes it can prove are too far; and
**int16 quantisation was 1.4× *slower* than float32 on NEON**, because of the widening in the
squared difference. Quantisation is a memory tool on Apple Silicon, not a speed tool.

## The second blocker

`Importer::importClips` pushes each glTF animation onto the rigs built in the same `loadGltf` call
(`src/assets/gltf_loader.cpp:342-364`). **There is no way to bind one file's clips to another
file's skeleton**, and no retargeting of any kind exists in `src/`: no joint-name map, no bind-pose
reconciliation, no bone-length scaling. `assets/aliens/ATTRIBUTION.md` already records the cost —
each of six variants carries its own copy of all 26 animations, 3.9 MB of 4.5 MB per file.

So AV Gen cannot load a motion dataset even if one were licensed and available.

## Decision

**Classical motion matching is not built now.** It is placed behind two gates:

1. a retargeting/import pipeline that can get a second file's motion onto a rig, and
2. a database of travelling locomotion with transition coverage.

**What is built first is the motion-data foundation and the two transition fixes that pay for
themselves on the content that already exists**: offline contact and phase extraction,
phase-matched transitions, inertialization, and per-foot ground planes.

The feature extraction, database build and offline pipeline are identical for classical and
learned motion matching. Building the pipeline once makes both a later, additive choice.

## Rejected alternatives

* **Build motion matching anyway on 57 seconds.** The nearest neighbour of a degenerate trajectory
  query is arbitrary. It would be measurably worse than `Gait`'s hysteresis-and-dwell state
  machine, which is tuned and works.
* **Synthesise root trajectories from `GaitSettings::walkSpeed`.** Tempting, and it is arm 4 of the
  §16 probe rather than a plan — a constant-velocity synthetic trajectory makes every frame of a
  walk cycle look identical to the matcher, which is exactly the degenerate case.
* **Add a nearest-neighbour acceleration structure.** The two-level AABB is the right structure
  when one is needed and is measured at 4.9× — but at 1,712 frames a plain search is 11.5 µs, so
  it would buy nine microseconds for ~60 lines of traversal-order-dependent tie-breaking. Not a
  KD-tree in any case: at 27-55 dimensions a KD-tree degenerates toward a linear scan and pays
  random access for it, which is why both the LMM paper and Unreal only offer one paired with PCA.
* **Skip the data problem by licensing a dataset.** Partly viable and better than expected, but it
  does not remove the gate — it moves it. **100STYLE is CC BY 4.0 and is four million frames of
  stylized locomotion**, roughly 2,300× this repository's entire corpus, with the starts, stops and
  turns that are missing. ACCAD is CC BY 3.0 direct from Ohio State. CMU permits inclusion in
  commercially-sold products. **But none of them can reach the alien rig, because retargeting does
  not exist** (see "The second blocker"). So the licensed-dataset route is gated on exactly the same
  unit as everything else. ADR-542 has the licensing analysis, including what cannot be used.

## Consequences

* The roadmap's early phases deliver visible quality on 57 seconds of clips rather than waiting on
  content that does not exist.
* Any future motion-matching work is gated on a measurement (database size, trajectory coverage)
  rather than on enthusiasm.
* `docs/character-ai-research.md:677`'s rejection of blend spaces — "a blend space is three steps
  past the layer stack that does not exist yet" — is due a re-read, because the layer stack now
  exists (ADR-300). A 1D blend space delivers most of motion matching's smoothness on three clips.

## Revisit triggers

* A retargeting pass exists and 100STYLE (or equivalent CC-BY locomotion) has been brought onto
  the alien rig with measured foot contacts and extracted trajectories.
* The motion database exceeds ~20,000 frames (11 minutes at 30 Hz), at which point the search
  becomes a real cost and the feature layout matters.
* Anyone proposes an early-out or an acceleration structure: re-run the probes **on real extracted
  features**, not on `mmprobe.cpp`'s synthetic fixture.
