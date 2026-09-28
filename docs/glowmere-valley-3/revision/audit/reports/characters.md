## GV3 character audit (read-only): character animation, alien locomotion, behaviour and farm animals

**Main finding:** GV3's configuration makes the long idles worse, but it is not their only cause. Four engine rules produce the patterns the owner saw:
- Wander picks each new destination uniformly at random, with no preference for continuing forward.
- Every mover lets a body travel only as fast as `max(0, cos(heading error))` allows. So for any turn over 90° it stops and pivots in place.
- The decider has no time limit on standing.
- The only slope limit is a world-wide 63° cliff rule.

Scene data alone cannot remove the animals' pivots and reversals. Line numbers below are for the av-gen-gv3 worktree. Its uncommitted ADR-895 edit shifts `behaviors.cpp` by +12 lines after line 330 compared with HEAD. The cast-v2 trace was taken from a scene identical to today's in every entity field; only the water specular differs. Nothing in either repo was modified.

### How the system is put together (one simulation step)
The order is: Director tier → action queue → behaviours in authored order → speed limiter → gait choice → locomotion plan → animation sink → pose layers.
- **Action queue** (Move / Pose / Face / Wait / Interact). Any running action sets `driven` (action.cpp:955).
- **Speed limiter** (ADR-620, entity.cpp:2603) limits only the *published* speed, not the body's actual travel.
- **Gait** (gait.cpp) picks walk/run/turn/idle from speed and turn rate, and sets the clip's playback rate.
- **Locomotion plan and stride warp** (ADR-829).
- **Animation sink** maps an activity name to a clip (entity.cpp:156–174, composition.cpp:1964), runs the `proceduralMotion` chain, and the matcher if opted in (ADR-623).
- **Pose layers** (aim, foot IK, stride, secondary) exist only on the five aliens.

Aliens run `decide` (ADR-269/333). It scores its considerers at `hertz`, holds a choice with `dwellTicks`/`margin` (decision.cpp:211–320), and replaces the routine action queue with the winner. Its `mind` block (ADR-670) adds percept memory, attention, novelty/habituation and world-event hearing.

Animals run `wander` + `ground` + `liveliness`.

Navigation is one shared Navigator (`maxSlope` 0.55 ≈ 63°, navigation.hpp:44), a 4 m NavGrid with shore and vista points (nav_grid.hpp:320), a steering fan, and crowd separation (ADR-240/831/835). ADR-832 (event producers) and ADR-833 (semantic tags) change nothing in GV3, because nothing there authors interactions or tag filters.

### Capabilities
Classes: (a) implemented and used by GV3, (b) implemented but unused, (c) partial, (d) missing.

| Capability | Class | Mechanism | GV3 |
|---|---|---|---|
| Idle variation | c | `liveliness` sway/nod noise; attention glance drives the head (behaviors.cpp:2776); the `idle` considerer can carry an `activity` and `duration` (decision.cpp:470–487) | `idle` has no activity; `observe` = `idle` = the same `Idle` clip; animals idle on a frozen Walk frame (`idleRate` 0) |
| Short purposeful behaviours | a (thin) | Move + Pose errands; `Interact` verbs; `investigate` | only walk-and-observe |
| Wandering | a | `wander` (behaviors.cpp:668–871); `interest` considerer (decision.cpp:849–953) | used |
| Looking / attention | a for the head, c for the body | attention + aim layer; `lookAt` stops turning the body whenever any action runs (behaviors.cpp:924) | bodies do not turn during observe poses |
| Pauses | a | wander `pauseMin`/`pauseMax`; Pose dwell | used |
| Direction changes | c | only at stops; paths are straight | chord/length 0.96–1.00 |
| Curved turns | d above 90°, c below | travel scaled by `max(0, cos error)` (behaviors.cpp:162/835/1638, action.cpp:795); the action tier's turn rate is hard-coded at 2.45 rad/s (action.cpp:791) | see measurements |
| Avoidance | a | crowd separation, steering fan, social avoid | used |
| Interaction with objects | b | Interact + prop affordances + goal considerer (ADR-832) | none authored; the `goal` considerer has no subject |
| Movement to points of interest | a | taste weights over water, glow, vista, landmark, character | used |
| Reactions to world events | a (weak) | beat → world event (composition.cpp:3182–3222); `react` considerer (decision.cpp:1332–1465); heard within a 3D radius (behaviors.cpp:2724) | 1 of 5 aliens reacts |
| Group behaviour | c | `social` greet/avoid; no herding | no alien pair is within 10 m for more than 1% of the film |
| Animation variation | c | any activity name can map to a clip, but only one clip per activity; one-shot clips hold their last frame (composition.cpp:1975) | 9–10 of 26 clips used |
| Speed variation | c | `ActionDesc::speed` exists but no considerer sets it; moves default to walk speed (action.cpp:771) | every alien moves at 3.07 m/s |
| Non-repeating movement | c | visited-place novelty (decision.cpp:347), variety, habituation, stall breaker | sage runs a 15 s loop; wander has no memory |
| Motion matching (ADR-623) | b | opt-in `motionMatching` block | not configured |
| Slope limits | c | one world cliff limit; no scene key for it (composition.cpp:1878–1886) | animals walk 20–28° grades |
| Orientation to terrain | c | `slopeAlign` (grounding.cpp:120–133) | 0.55; animals have no foot IK |

**Alien clips.** All five GLBs carry the same 26: Button_push, Crazy, Dying_1_backpack, Dying_1_no_backpack, Dying_forward, Fall_loop, Fight_head_hit, Fight_idle, Fight_Jab, Fight_leg_kick_1, Fight_leg_kick_2, Fight_punch, Floating, Flying_jet, Idle, Idle_turn, Jump_running, Jumping, Landing, Running, Take_from_floor, Take_from_table, Walking, Walking_crouch, Walking_injured, Walking_low_grav. The cow, bull and horse GLBs have one clip each: `Walk` (0.9 s).

### Measurements (cast-v2, 20 Hz, 226 s)
"Still" means speed below 0.1 m/s. horse-11 is cut at 170 s, when the abduction takes it over.

| | still | longest still | reversals / stops | turns >90° at stops | yaw turned below 0.3 m/s | standing yaw rate | median segment | stop revisits (A→B→A) |
|---|---|---|---|---|---|---|---|---|
| rook | 13% | 5.0 s | 0/10 | 7 | 9% | 141°/s | 41.5 m | 0 (0) |
| tide | 37% | 12.8 s | 2/22 | 7 | 16% | 141°/s | 10.1 m | 6 (5) |
| sage | 43% | **67.3 s** | **14/19** | 18 | 30% | 141°/s | 17.1 m | 12 (10) |
| ember | 29% | 9.6 s | 12/35 | 17 | 14% | 141°/s | 6.4 m | 17 (12) |
| vane | 36% | 22.2 s | 2/16 | 12 | 24% | 141°/s | 20.1 m | 3 (1) |
| 12 animals | 20–41% | 3.4–6.6 s | 27/258 | 128/258 | **27–44%** | equals each animal's `turnRate` (69–110°/s) | 6.6–12.8 m | 4–13 each |

- **Speed never varies.** Each body moves at a single speed.
- **Turns are tight.** The median turn radius while moving is 0.7–1.4 m, less than a body length.
- **Tide plays `Running` at 0.41× speed** for 43% of the film.
- **The beam (170.35 s) reached one alien.**
  - Rook (161 m) and tide (177 m) were beyond the 150 m radius, which is measured in 3D from the saucer 23 m up.
  - Sage (124 m) heard it faintly and stayed put.
  - Ember (58 m) paced in the river.
  - Only vane walked in, from 60 m to 6 m by 200 s.
- **Slopes (plane fits to the trace, so estimates):**
  - cow-23, cow-12, horse-22, horse-2 and horse-20 spend 74/62/52/49/29% of their path on grades over 15°.
  - 134 of 273 animal stops (52%) are on ground steeper than 12°; 30 of those (84 s in total) face within ±45° of uphill.
  - Animals placed on the valley floor stop on 2–4° slopes.

### Root causes
1. **Walk → stop → 180° → walk back.**
   - **Animals:** each destination is drawn at a uniform random angle and accepted if it is merely navigable (navigation.cpp:193–210). Past `homeRadius`, the draw is re-centred on the spawn point, which is a return trip (behaviors.cpp:745–750).
   - **Sage:** its `holdPost` (weight 0.42, tolerance 9, pull 0.18) scores higher the further sage is from its post (decision.cpp:546), and walks it back only to the 9 m ring (decision.cpp:555). Meanwhile `graze` offers targets out to 50 m. On arrival about 26 m out, holdPost wins before the observe dwell starts. 12 of 21 stops are exactly 9.0 m from the post, the later outbound stops last 0.1–0.2 s, and the cycle repeats every 15.0 s (129/144/159 s).
   - **Ember (inference):** from 168.8 s to the end it paces back and forth in the river shallows. This fits an errand whose target lies across water it cannot ford.
   - **All movers:** travel is scaled by the cosine of the heading error, so any new target more than 90° off starts with a dead stop and a pivot.
2. **Long idles.** Nothing limits how long a body stands.
   - `idle` wins with no actions attached.
   - `holdPost`'s pose has duration 0 (decision.cpp:574), so it never ends on its own.
   - The stall breaker only watches plans that are moving (behaviors.cpp:2532).

   Sage stands on its ring from 158.8 s to the end. Its holdPost score of 0.42 beats every graze option once novelty and variety have discounted them; the exact scores are inferred. Tide's `minRange` equals its `approach` (7 m), so a nearby candidate turns into "stand and look for 6 s" (decision.cpp:148–159, 872–875); 9 of its 23 hops are under 4 m. Vane chains 8 s dwells.
3. **Animals pivot on a frozen frame.** Travel is zero while the heading error exceeds 90°, and the body turns at wander's `turnRate`. The gait plays the `turn` clip at `idleRate` when speed is 0 (gait.cpp:88–115). GV3 sets `idleRate` 0 with `turn: Walk`, so a frozen mid-stride pose rotates in place. Wander also stops the body dead (behaviors.cpp:779–786, 836), while the speed limiter ramps only the reported speed. The legs therefore step in place for about 0.4 s after the body stops (cow-23 at 45.70 s).
4. **Slopes.**
   - **Placement:** spawn points are copied verbatim from GV2-multicam: cow-12 at y 40, cow-23 at y 37, and horses 2, 22 and 20 at y 16–20, all on the valley flank.
   - **Navigation:** there is no scene key for the slope limit and no preference for flat ground.
   - **Facing:** at rest an animal faces whichever way it arrived.
   - **Grounding:** it aligns the body to only 55% of the slope (ADR-354), and animals have no foot IK.

### Recommendations, ranked
**Engine changes.** Each is off by default so golden traces stay bit-identical; GV3 opts in.
1. **Forward-biased, slope-aware destinations.** `pickDestination` takes an optional heading cone and maximum slope (navigation.hpp:215, navigation.cpp:193). Wander gains `headingSpread` and `maxSlope`, and leans the cone toward home instead of re-centring on it (behaviors.cpp:738–752). Test: a seeded wanderer on flat ground for 600 s makes no re-target over 150°, and on a tilted plane no destination exceeds `maxSlope`.
2. **Walk-through turns.** Add `turnRadius` to wander and to GaitSettings, plus a `turnRate` to replace the constant at action.cpp:791. While moving, turn rate is capped at speed ÷ radius and travel keeps a minimum fraction of speed; a pivot happens only from rest. This touches behaviors.cpp:162/835/1638, action.cpp:789–816 and gait.hpp. Test: a 180° re-target at speed traces a radius at least R. Keep test_farm_locomotion.cpp:598 and the ADR-333 route golden unchanged at R = 0.
3. **Wander eases into and out of stops** using `Gait::approach` plus an arrive radius, as action.cpp:806–813 already does for the action tier.
4. **Decider anti-idle and anti-loop.** Add `maxStillSeconds` to the stall breaker (behaviors.cpp:2524–2571). Give `holdPost` a `duration`, and have it walk back to half its tolerance rather than to the edge of the ring. Test in test_entity_decision.cpp: no A→B→A within 20 s and no still stretch longer than `maxStillSeconds`.
5. **Speed variety.** A `speedRange` option on interest/react considerers sets `ActionDesc::speed` from a seeded draw per decision (decision.cpp:118); react speed scales with urgency.
6. **Put these metrics into the ADR-826 analyzer** (character_quality.hpp/.cpp): longest still stretch, reversals, pivot yaw, turn radius, slope under stationary bodies and facing uphill. Test with hand-built cases in test_character_quality.cpp. These metrics then act as the regression gate for items 1–5.
7. **Safety net:** a single-clip animal that has to pivot plays its clip while turning (gait.cpp:88–117) instead of rotating a frozen frame.

**Scene changes** (in tools/gv3/cast.py, then regenerate):
- a. Re-home cow-12, cow-23, horse-2, horse-20 and horse-22 onto the corridor floor, at least 20 m from the river. Shrink `homeRadius` and `maxRange`, and check the new spots with `slopes.py` or tools/gv3/ground.py.
- b. Sage: holdPost weight 0.42 → about 0.1, and graze `homeRadius` down to within the holdPost tolerance plus about 10 m.
- c. Tide: `runEnter`/`runExit` 3.02/1.98 → about 4.8/3.1; `minRange` 7 → at least 14; `dwell` 6 → 3–4 s.
- d. Vane: `dwell` 8 → 3–4 s; `maxRange` 130 → about 60.
- e. Abduction: event radius 150 → at least 250. Raise `mind.memory.eventSeconds` from 10 to 20, because at 10 the react considerer's 12 s `fadeSeconds` is never reached. Each extra abduction raises the same event, so the reactions scale with the brief's request for more abductions (§9).
- f. Use the unused clips as new activities, e.g. `inspect: Take_from_floor` and `tinker: Button_push`. Set dwell to about the clip's length, since one-shot clips hold their last frame. Give `idle` an activity and a short duration.

Scene data cannot remove the animals' pivots and reversals, the aliens' fixed 140°/s pivots, or the single speed. Those need engine items 1–3 and 5.

Files are in `/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/004befa7-093f-41d0-b2da-3d5e53a50a3a/scratchpad/audit/`:
- measure.py
- measure-output.txt
- measure.json
- slopes.py
- slopes-output.txt
