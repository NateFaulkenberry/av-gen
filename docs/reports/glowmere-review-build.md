# The Glowmere review build (multicam film)

2026-09-25. Branch `agent/motion`. The board's ★ items, on `glowmere-valley-2-multicam`, with the
owner's rulings of 2026-09-25 applied (ADR-827). Review sheets are in
`~/Desktop/av-gen-review/12-motion-stack/`.

## The ten items

| # | Item | Status | Evidence |
|---|---|---|---|
| 1 | Audit what A–D built | done | `docs/development/motion-stack-progress.md` (the recount) |
| 2 | Wire the alien population to the autonomous stack | done | behaviour: the awareness layer (Phase D §66); motion: `proceduralMotion` on the five aliens (ADR-827), confirmed posed through the provider chain |
| 3 | Per-character personality, data-driven | done (earlier) | five `personality` blocks; the beam reactions below differ by it |
| 4 | Perception + the smallest event set | done | one event, `abduction/beam`, now reaching 150 m |
| 5 | UFO reactions, staggered | done, measured | below |
| 6 | Multicam: the director observes | partial | the camera reads the staging scenario's beats, not what characters decide; semantic character events (F7) are not built |
| 7 | Debug visualisation + state inspector | partial | `MotionDebug::matching` and `databaseSamples`; `avgen_motion explain`; the analyzer. No in-app character inspector panel |
| 8 | AI ON / AI OFF comparison | done | below; sheets 05 and 06 |
| 9 | Offline animation-quality validation | done (root) | `avgen_character_quality` (ADR-826). The pose is judged by eye; skinning and IK defects are outside the analyzer's reach |
| 10 | Review report + measured runtime cost | this document; cost below is **not reliable** | |

## 5 — reactions to the saucer

The abduction beam fires 12 times in 226 s, 55–140 m from the aliens. Its world event reached
60 m, so over the whole film one alien reacted once (Vane, 0.25 s after the 26.0 s beam). At 150 m
(the saucer is lit and beaming at night; it is visible across the valley):

| beam (s) | reactions (onset after the beam) |
|---|---|
| 7.85 | Ember approaches +0.57 s; Rook approaches +5.1 s; Sage flees +5.6 s |
| 26.03 | Vane approaches +0.25 s; Rook approaches +1.45 s |
| 44.22 | Ember +0.03 s, Vane +0.06 s, Rook +0.76 s approach; Sage flees +0.86 s |
| 62.40 – 171.50 | Rook approaches at six of the seven beams (+0.08 to +3.3 s); Sage flees at two (98.8 s, 117.0 s); Tide approaches once (153.3 s); nobody at 171.5 s |

No two aliens are synchronised to a frame, and the direction of the reaction follows personality:
bold Rook approaches, cautious Sage flees.

## 8 — AI ON / AI OFF (60 s, the analyzer)

The AI-OFF arm runs the aliens' pre-awareness behaviours (`explore`, `liveliness`, `lookAt`) with
everything else identical.

| alien | velocity discontinuities (OFF → ON) | stuck s (OFF → ON) | oscillations (OFF → ON) | activity changes/min (OFF → ON) |
|---|---|---|---|---|
| rook | 72 → 6 | 6.43 → 1.22 | 47 → 1 | 60 → 16 |
| tide | 3 → 0 | 0.20 → 0.10 | 0 → 0 | 9 → 28 |
| sage | 4 → 4 | 0.33 → 1.38 | 4 → 16 | 33 → 49 |
| ember | 82 → 3 | 2.03 → 0.27 | 10 → 1 | 15 → 15 |
| vane | 28 → 4 | 8.72 → 0.57 | 43 → 2 | 57 → 18 |

- The awareness layer removes the pre-awareness explorer's hitching. In the AI-OFF arm, Rook,
  Ember and Vane stall and re-route. None of the five pops in either arm, and foot slip stays
  under 1% of moving frames.
- **Sage is worse with the AI on.** It is the one to tune:
  - 49 activity changes a minute and 16 A→B→A oscillations;
  - its decider chooses between the grove and nearby glows, and its `interest` behaviour interrupts
    its walks.
- Idle time rises with the AI on (Rook 3% → 25%). The characters stop to look at things, which is
  the design.

## The proceduralMotion and matcher arms

These change the pose, not the root, so the analyzer reads them identically to AI ON. They are
confirmed to pose through the path they claim, 150 of 300 frames at the rigs' 30 Hz:
- the procedural arm through the provider chain;
- the matcher arm matching on the baked 1,738-sample scout database.

They are judged by eye in `06-review-arms-rook-4up.mp4`. In a 0.5 s strip of consecutive
frames, all three produce coherent walk cycles; the procedural arm's phase differs slightly. Foot
contact in motion is not judged here and is for the owner to watch.

## Cost

Wall clock per simulated frame, 19 entities, from the analyzer on a machine carrying other agents'
builds and suites:

| arm | ms per simulated frame |
|---|---|
| AI ON | 7.65 |
| AI OFF | 9.11 |
| procedural | 8.47 |
| matcher | 6.94 |

The spread is the machine's load, not the arms. AI OFF is not really slower than AI ON, and the
matcher is not really the cheapest. **Not verified.** A quiet-machine re-take is needed before
these numbers mean anything.

## For the owner to look at

- `05-review-arms-rook-stills.png` and `06-review-arms-rook-4up.mp4`: the four arms, camera welded to
  Rook, 0–16 s.
- `04-beat-hops-rendered-three-films.png`: the kept beat hops, rendered.
- `01-react-crazy-loop-vs-once.png`: the react HOLD. It does not appear in the multicam film, where
  no alien plays a one-shot clip.
- **Decisions this build puts to the owner:**
  - whether the matcher goes into the film, after watching 06;
  - whether the 150 m beam reach reads right on screen;
  - Sage's churn.
