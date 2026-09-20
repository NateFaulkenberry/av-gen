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

A second measurement from the same probe, worth recording because it contradicts the textbook: the
**early-out** inner loop — abandon a row once its running cost exceeds the best so far — is
**2.0× to 2.7× slower at every size tested** (20,000 frames, D=27: 132.1 µs plain vs 357.5 µs
early-out). The branch defeats clang's autovectorisation, and on this machine the straight-line
vectorised loop wins by more than the early-out saves.

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
* **Add a nearest-neighbour acceleration structure.** Brute force is 132 µs at 20,000 frames; a
  tree buys 5-10× and costs determinism risk in tie-breaking plus a build step.
* **Skip the data problem by licensing a dataset.** See ADR-542: almost every open mocap corpus is
  non-commercial, and AMASS-derived generative output inherits the restriction.

## Consequences

* The roadmap's early phases deliver visible quality on 57 seconds of clips rather than waiting on
  content that does not exist.
* Any future motion-matching work is gated on a measurement (database size, trajectory coverage)
  rather than on enthusiasm.
* `docs/character-ai-research.md:677`'s rejection of blend spaces — "a blend space is three steps
  past the layer stack that does not exist yet" — is due a re-read, because the layer stack now
  exists (ADR-300). A 1D blend space delivers most of motion matching's smoothness on three clips.

## Revisit triggers

* A travelling locomotion dataset with starts, stops and planted turns is licensed or captured.
* The motion database exceeds ~20,000 frames (11 minutes at 30 Hz), at which point the search
  becomes a real cost and the feature layout matters.
* Anyone proposes an early-out or an acceleration structure: re-run `mmprobe.cpp` first.
