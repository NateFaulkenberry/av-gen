# The revision's iterations

Each iteration names what changed, what the evaluator measured and what was decided. The
evaluator is the Creative Critic. Session `gv3-revision` holds one track per category.

## Iteration 0: the baseline (the first pass's delivered final)
- **Job:** `job_1a0def77d9084ad74` (session `gv3-revision`, track `film`), run 2026-09-26.
- **Input:** `build/gv3/final/glowmere-valley-3-1080p.mov` (1920×1080, 60 fps, 225.5 s, with the song), evaluated in preview mode against the first pass's scene data and intent.
- **Completeness:** complete; every core check ran. Total time 198 s.

**Headline:** 89 issues (0 critical, 9 high, 50 medium, 30 low) over 40 shots. The weakest
dimensions are composition 0.64, motion 0.70, visual hierarchy 0.71, cinematography 0.75 and
technical quality 0.79.

The baseline, by the categories the brief asks to evidence:

| Category | Measured on the first pass |
|---|---|
| **Audio reactivity** | 24 route–entity pairs configured. **Only 1 is observed in the pixels,** the elder's kick heartbeat (+2.0% averaged over the film, z 8.5; +9% in s16 alone). 7 are configured but show no measurable response, 9 have too few clean events to judge, and 3 are behavioural targets. |
| **Camera stability** | 10 "camera shake / wobble", 9 "the camera path itself jitters", 3 "abrupt camera acceleration" and 2 "wobble on a camera move" findings. The shakiest shots are follow shots: s28, s18, s03, s29, s09, s36, s38, s25. |
| **Water and fine detail** | 9 "shimmering / unstable fine detail" findings (s09, s10, s18, s20, s37, s38 …). |
| **Pacing** | 22 s of 225 s (10%) comes after a shot's last new information. s16 and s24 show nothing new after their first frame; s08 holds 2.5 s of its 7.4. There is one "cutting slows where the music gets more energetic" finding, and three pairs of near-identical framings. |
| **Aliens** | Longest standing still: Sage 67.3 s, Rook 26.1 s, Vane 22.3 s, Tide 12.8 s, Ember 9.6 s. Repeated paths: Sage 10, Ember 7. 38% of the film is led by an alien. |
| **Animals** | Stationary 23–44% of the film. Each animal turns in place for 5.8–12.4 s in total. Up to 5 repeated paths each. 7 of 12 are anchored on 17–25° slopes; this is measured separately, because the evaluator had no ground samples, so the next run must supply them with `--ground`. |
| **UFO events** | One "several 'saucer' events are framed alike" finding. |
| **Composition and hierarchy** | 13 composition and 12 visual-hierarchy findings. The worst: the visitor out of frame for 67% of s07; horse-11 out of frame for 50% of s32; a bright element outweighing the subject in s20 and s22. |

## Round 1 (gv3-int r1): the merged film, as the four Phase 3 streams left it
- **What:** `gv3/production` `b74c3429` (cast, cut, look and world merged), generated and rendered whole at
  960×540 on engine-3 (main `876a11e2`). Render: 13,530 frames, GPU errors 0, exit 0, sequence hash
  `72fa69b41547fdeb`; only CPU suites ran beside it. Log and method: `phase3/integrate.md`, round 1.
- **Jobs** (session `gv3-revision`, track `film`): `job_1a0e49f4c67b66a3b` (label `r1`, the adapter's inputs) and
  `job_1a0e4a50eaa6cabba` (label `r1b`: the same video with the film's routes mapped to their heroes by
  `reactivity.critic_modulation()`, as gv3-look's clip jobs did; the adapter alone maps 11 of the 75 routes).

| Dimension | iteration 0 (first pass) | r1 | r1b (routes mapped) |
|---|---|---|---|
| character staging | 0.866 | 0.917 | 0.917 |
| cinematography | 0.754 | 0.887 | 0.887 |
| composition | 0.636 | 0.653 | 0.653 |
| effects | 0.942 | 0.975 | 0.876 |
| lighting | 0.872 | **0.796** | 0.796 |
| motion | 0.703 | 0.834 | 0.834 |
| musical synchronization | 0.887 | 0.951 | 0.767 |
| pacing | 0.983 | 0.969 | 0.969 |
| technical quality | 0.787 | 0.805 | 0.805 |
| temporal coherence | 0.843 | 0.929 | 0.929 |
| visual coherence | 0.912 | 0.962 | 0.962 |
| visual hierarchy | 0.713 | **0.645** | 0.645 |
| issues (critical/high/medium/low) | 0/9/50/30 over 40 shots | 0/8/50/40 over 73 | 0/8/67/40 over 73 |

By category (r1b where routes matter):
- **Audio reactivity:** 58 route-entity pairs judged (iteration 0: 24). "Configured and observed": the elder's kick
  on its gills (z 7.3), underside (z 8.5) and practical light (z 9.2), and the lantern's underside on the clap
  (z 4.5, new); the lantern's gills weak (z 3.5). 21 show no measurable response: the cap and spores beside the
  answering parts, the bloom's downbeat (15 events), the umbra's breath (7), the spire's and veil's slow levels
  (kept as background life, gv3-look), and **the saucer beam's four copied audio links (z 0.1-1.2 over 1,015
  on-screen frames)**. Musical synchronization falls in r1b because three times as many routes are judged.
- **Camera stability** (`tools/camera_stability.py`, the trace): 19 of 22 subject shots pass (gv3-cut's last
  check, on the source world: 21 of 22). The Critic's pixel classes: 6 shaky, 3 move with wobble (iteration 0:
  10 and 2).
- **Pacing:** the Critic's novelty reads 92.6 s held after the shots' last new information, 18 shots static from
  the first frame (iteration 0: 22 s, 3). Being investigated (the round 2 notes).
- **Aliens (ADR-910):** every stand under 8 s except Vane's 8.9 s watching E5; reversals rook 3, ember 4, sage 1,
  vane 1 (ember's are navfix's river bank; rook's are new with the closed world). The Critic: only rook floats
  (19% of its track over 0.1 m, worst 0.26 m).
- **Animals:** 0 reversals, 4 turns over 90°, 5.2 s on ground over 12° (bull-1, bull-21), 0 s facing uphill.
- **UFO events:** all five play; every moment the cut means to show is on screen (the Critic: beams 4/4,
  lifts 3/3, taken 3/3, the cross and the sweep); E3 now takes bull-21 (the closed world), framed alike.
- **World edge:** `world.py --check` FAILS on the new cut's cameras: 17 views with an open end (77.1 in all 7,
  up to 17 of 96 columns), and the river's mouth in 91 views of 12 shots.
- **Found on the sheets** (`~/Desktop/av-gen-review/18-glowmere-valley-3/revision/integrate/`): **two moons** (the
  visible procedural sky is looked up through `env/rotation` -0.568, the skybox's crisp moon is not); E2's moon a
  grey ellipse in a 24 mm corner; 95.3's horse washed out inside the beam; 19.1 and 25.1 pushed into plants;
  29.1's trees hiding the elder; 31.1's spire lost on a lit hillside; 59.1's Sage walking out of the frame.
- **Decided:** round 2 fixes these in data (below, and `phase3/integrate.md`).
