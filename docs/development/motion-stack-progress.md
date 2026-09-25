# Motion Stack Build Board: progress

Last updated: 2026-09-25
Branch: `agent/motion` (worktree `../av-gen-motion`, from main 232a50d7)
Owner of: motion engine capabilities (`src/entity/`, `src/scene/` motion, clips, rigs, and the
engine side of the actor handoff). Not `src/directing/` or the AI layer.

## Handoff: paused 2026-09-25 at the owner's request

Work is paused so the owner can do a project build-out. Everything below is the state at the pause.
The branch is `agent/motion`, merged with main b0f141b5. The head and suite results are under
"Test status". Nothing has been pushed; the coordinator merges.

### Counts (per spec section; the table below has the notes)
| Phase | Done | Partial | Not started | Spec / N/A |
|---|---:|---:|---:|---:|
| 0 Research | 5 | 0 | 0 | – |
| A Foundation | 17 | 0 | 2 | – |
| B Procedural (67) | 55 | 1 | 0 | 11 |
| C Matching (95) | 81 | 0 | 1 | 13 |
| D Behaviour (78 + demo) | 42 + demo | 25 | 1 | 10 |
| ★ Review build (10) | 7 | 3 | 0 | – |
| E Learned (13) | 0 | 0 | 13, gated | – |
| F Advanced (9 tracks) | – | – | 9 tracks, gated | – |

### What is left, in the order I would do it
1. **The review build's partials.**
   - ★.6: the multicam director observes character events, not only scenario beats. Plan: an
     event camera keyed on a character event name, and the rig aims at the entity that raised it.
     `observeCameraEvents` reads `EntityWorld::worldEvents()` and the named `ActionEvent`s, then
     opens a span with a hold.
   - ★.7: an in-app character inspector (D §40/§48) and the live matcher view (C §68). Put the
     decisions in a tested `*_logic` module. **Don't run the windowed app:** it rewrites the
     owner's prefs.
   - ★.10: re-take the cost figures on a quiet machine.
2. **D's partials, by value:**
   - §26: spawn/remove and world-effect start/end producers;
   - §69: scene-level effects carry no semantics or events;
   - §40/§48: inspector (same work as ★.7);
   - §66: wire `glowmere-valley-2` and `-song`;
   - §44 and §8: the 500/1000-character benchmark rows, per-subsystem timing, allocation counts;
   - §31/§32: behaviour LOD tier 1, and keep tier 2's D3 violation out of live play;
   - §62: a fixed-step interactive play mode;
   - §24: follow and relationship labels;
   - §68: weight shift and repositioning in idle;
   - §23: patrol and area-biased wander;
   - §14: ORCA;
   - then the documentation-shaped rows: §42, §43, §46, §49, §51, §52, §59, §60, §72 and §74 (see
     `board-status.json` notes);
   - §45, the threading investigation, is not started.
3. **B §66:** the final demonstration, joined in Glowmere and driven by MotionRequests: notice,
   turn, walk, avoid, adapt, look, stop, shift weight, reach, inspect, turn away, resume. It needs a
   weight-shift layer, which does not exist yet. Render it to `12-motion-stack/`.
4. **C §94:** the final artistic demonstration. It waits on the owner, and the matcher is off in
   the film.

**E and F stay gated.** E's own stop gates (§61) are unmet, and its baseline (the matcher in the
product) is off by ruling. F's tracks F7, F8 and F9 have pieces built (Director slices, ADR-826,
checkpoints), but no track has been measured or classified.

### The owner's rulings to date
- **Matcher:** off in every shipped scene. It stays a review arm and a loading path (ADR-825,
  ADR-827).
- **Abduction beam:** reaches 150 m (ADR-827).
- **Sage:** calmed with the existing levers: interest refractory 15 s, observation dwell 3–6 s,
  gait dwell 0.6 s (624dbeba).
- **react:** holds `Crazy`'s crouch; autonomous clips play as they measure (ADR-827).
- **Rook's jump:** the 1.1 m default stands; no larger apex is invented (ADR-822).
- **Beat hops:** Ember's and Vane's 2.6 m beat hops are kept as authored (ADR-822).
- **proceduralMotion:** on for the five aliens in the multicam film.
- **Foot fixes:** Rook's turn (ADR-829) and Ember's bank (ADR-830) were approved and done. Review
  files 09 and 12–14 are in `12-motion-stack/`.

### Known defects, not fixed
- **Main's `_diag-water-*.json` projects** record a stale sha256 for the multicam scene. They came
  from another branch.
- **Vane down a steep bank** (multicam, ~16.5 s) bobs a foot ~0.10 m per posed frame. It is smooth
  compensation, not a step, but it is the largest remaining foot motion.
- **A sideways drift remains while a body pivots inside another's crowd radius** (ADR-831). The
  real fix is separation as steering the motion tier turns into steps. ADR-835 feeds the seam; the
  motion tier does not yet step on it.
- **`positionAt` smoothsteps `Smooth` keys**, where the baked track uses Catmull-Rom (M1 note
  below).
- **The `[!shouldfail]` slope-lean case** (`test_character_lab_slopes.cpp:187`) is the known
  ADR-260 limit. It is expected to fail on every clean run.
- **The multicam scene's camera events are scenario-only** (★.6).

### How to resume
1. `cd ../av-gen-motion` (branch `agent/motion`). Merge main, then run `cmake --preset release`
   (reconfigure; an incremental build misses test files a merge added) and
   `cmake --build build/release`.
2. Select tests by tag, not by name, because a comma splits a name. The seek-parity gates are
   `[adr800],[adr870],[motion][direction],[seek][engine][glowmere]`. The film metrics are
   `[stride]` and `[adr830]`.
3. Run the two suites one after the other, never during a build. Use the exit code: CPU
   `build/release/tests/avgen_tests` should exit 0 with exactly one expected failure; GPU
   `build/release/tests/avgen_render_tests` should exit 0.
4. Review renders: `tools/make_review_arms.py` and
   `tools/make_review_follow.py <project> <body> --offset … --tag …`. Render
   `src/avgen --project P --render DIR --range 0:T --fps 60` from 0 s at the film's own 60 fps.
   Delete the generated `examples/world/_review-*` and `_arm-*` files afterwards. They are not to
   be committed.
5. My ADR range is 820–869. The next free number is 836.

## How this was counted

Every row is counted against the owner's specs in `docs/design/specs/phase-*.md` and against
main's code. Commit titles, the board and the phase logs' own tables are not evidence. For each
done or partial claim the check is three things:
1. what the section literally requires;
2. the code, test or doc that meets it;
3. whether the product reaches it, meaning a caller in `src/`, `apps/` or `tools/`, or scene data.

Three read-only audits did the sweep. I spot-checked the load-bearing claims myself:
- the augmenter builds all seven kinds;
- the matcher appears only in `examples/labs/motionmatch/`;
- nothing in `src/` writes `intent.steering`;
- `Engine::applySectionActions` calls `direct()`, which a seek never replays;
- `proceduralMotion` is absent from every Glowmere scene.

"Principle" sections (for example "Do not…" and "Final principle") count as N/A unless they
impose something checkable.

## Per-phase table

| Phase | Done | Partial | Not started | N/A | Board (21 Sep) said | Note |
|---|---:|---:|---:|---:|---|---|
| 0 Research | 5 | 0 | 0 | 0 | done | |
| A Foundation | 17 | 0 | 2 | – | done | not re-audited this round; the board's own data had 2 rows planned under a "done" phase |
| B Procedural (§1–67) | 55 | 1 | 0 | 11 | 54/56 | §39 done (ADR-835: crowd separation feeds `intent.steering`); §66 is partial; see below |
| C Matching (§1–95) | 81 | 0 | 1 | 13 | 52 / 25 / 5 | §37/39/40/76/81 done by ADR-825 (the product loads a baked database through the slot); §68 is partly live (`MotionDebug::matching`) and counted done-in-CLI as before; §94 waits on the owner. **§86 (the Phase D gate)**: the matcher runs on Glowmere as a review arm and is not yet in the film |
| D Behaviour (§1–78 + demo) | 42 + demo | 25 | 1 | 10 | 44 / 16 / 8 | §35 done (ADR-824/828: runtime and scheduled `CharacterGoal`s, `release`, scheduled direction replayed); §26 still partial -- `ActionComplete` (ADR-828), `InteractionComplete` and `VolumeEnter/Exit` (ADR-832) have producers; spawn/remove and effect start/end do not. §25 done (ADR-833). §36 done (ADR-834) |
| ★ Glowmere review build (10) | 7 | 3 | 0 | – | 0 / 10 | done: 1–5, 8, 9; partial: 6 (the director reads scenario beats), 7 (no in-app inspector panel), 10 (the report exists; its cost figures were taken under load and are not reliable) |
| E Learned motion (13 board rows) | 0 | 0 | 13 | – | 13 planned | gated; see recommendation |
| F Advanced (9 tracks) | – | – | – | – | 9 gated | pieces exist in F7, F8 and F9; no track has been measured or classified |

### B: the open row
- **§39 Obstacle-responsive motion: done (ADR-835).** Crowd separation publishes its push as
  `intent.steering`, and `advanceMotion` passes it to the motion request for every body. Obstacle
  avoidance was already part of the desired heading.
- **§66 Final demonstration: partial.** Two halves exist and were never joined.
  - The two halves:
    - `test_vertical_slice.cpp` is a scripted MotionRequest run;
    - `test_phase_d_autonomy.cpp` is driven by the decider, but on the clip player, not by
      MotionRequests.
  - Missing: notice → turn, avoidance under MotionRequest control, weight shift (no layer
    exists), inspect / turn away / resume, and any Glowmere demonstration.

### C: what changed since the board
- **Landed since 21 Sep, now done:** §21, §35, §44–49, §50–55 (measured under load; re-take on a
  quiet machine), §59, §61, §64–67, §70, §71, §73, §77–79, §89 (`docs/design/motion-matching.md`),
  and §91–93.
- **Downgraded to partial:** §37, §39, §40, §76 and §81. (All five done since: ADR-825.)
  - The product never loads a baked `.motiondb`. `Composition::matchAssetFor` extracts features
    synchronously at composition build.
  - `MotionDatabaseSlot`, the async load and hot-swap, has no caller outside tests.
- **Not started:** §94, the final artistic demonstration.
- **Product reach:** the matcher runs only where an entity sets `motionMatching`, which is only the
  six labs in `examples/labs/motionmatch/`. Glowmere runs on `ClipMotionProvider`. So §3, §33, §59
  and §66 are done in labs only, and the Phase D gate (§86) is not met in the product.
- **Assets missing here:** the demo pack and the 100STYLE packs are gitignored and absent on this
  checkout, so the tests for §20, §44, §50–55, §64 and §65 SKIP here.
- **Other gaps:**
  - The in-app `MotionDebug` carries no matcher fields, so §68 is CLI-only.
  - The `SkeletonMismatch` and `Failed` statuses are never produced.

### D: what changed since the board
- **Landed, now done:**
  - §63 scrubbing: ADR-671 (6ad6db5f), plus ADR-700 checkpoints (f4ffe238, 57396b37).
  - §75 final report: `docs/reports/autonomous-character-phase-d-report.md`.
- **Landed, still partial:** §66. All five aliens run the awareness layer, but **only in
  `glowmere-valley-2-multicam.scene.json`**. `glowmere-valley-2` and `-song` are not wired. Props
  and effects carry semantic tags since ADR-833.
- **Downgraded from done:**
  - §24: no relationship labels and no follow.
  - §25: no semantic categories. (Done again since: ADR-833.)
  - §26: none of the spec's event kinds has a producer. (Since: ADR-828 and ADR-832.)
  - §32: live play culls and coarsens by camera distance.
  - §62: interactive play uses wall-clock dt.
- **Not started:**
  - §36 camera/cinematic signals. (Done since: ADR-834.)
  - §45 threading investigation.
- **Partial, and matters to the Director:** §35.
  - What exists: verbs, never clips, and a keyframeable `goal` considerer.
  - Missing: a runtime way to target a goal, behaviour or event.
- **Scrub hazard for any Director command issued over time:** a seek does not replay timeline
  `EntityAction` → `direct()` calls. ADR-093 re-delivers only the standing intent. Anything the
  Director compiles to `direct()` is not scrub-exact. What the Director compiles to a
  `seq::Actor` is scrub-exact only if the entity handoff is an input the replay reads. See the
  handoff design below.

### ★ Glowmere review build
| # | item | status |
|---|---|---|
| 1 | Audit A–D | done (this recount) |
| 2 | Wire the aliens to the autonomous stack | done: behaviour, and `proceduralMotion` on in the multicam film |
| 3 | Per-character personality, data-driven | done (multicam) |
| 4 | Perception + smallest event set | done |
| 5 | UFO reactions staggered | done: the beam reaches 150 m, and the five react staggered by personality (ADR-827) |
| 6 | Multicam: the director observes | partial: the camera reads the scenario's beats, not character events |
| 7 | Debug viz + state inspector | partial: text trace, `motionDebug`, and the ADR-826 analyzer; no in-app panel |
| 8 | AI ON / OFF comparison | done: review arms (05/06) and the analyzer |
| 9 | Offline animation-quality validation | done: ADR-826 `avgen_character_quality` |
| 10 | Review report + measured cost | partial: `docs/reports/glowmere-review-build.md`; costs taken under load |

### E and F: recommendation (not started, reported first as instructed)
- **E: keep all of it gated.**
  - Its own stop gates (§61) are unmet.
  - Its baseline, Phase C in the product, does not exist yet (the matcher is lab-only).
  - The Director spec says not to build neural runtime decisions.
  - `IConsiderer` already *is* the `LearnedBehaviorProvider` seam; at most name it (S).
- **F, worth doing, in this order:**
  1. **F8-lite (M):** a per-character quality analyzer over a simulated run (foot slide, root pops,
     stuck/idle %, option churn), as JSON. It closes review items 8 and 9 and gives the Director a
     verifier.
  2. **F9 golden scenes (S–M):** Glowmere Autonomous and UFO Encounter as regression scenes, with a
     same-seed AI-off arm.
  3. **F7 step 1 (S–M):** a `CharacterGoal` timeline event compiled to `goal` considerer data. This
     is the Director's Slice 4 "goal mode".
  4. **F7 semantic character events (M):** candidates for the Director's live event producers.
- **F, keep unbuilt:**
  - F4 generative motion, including the backflip; the missing backflip is an asset problem;
  - F3 ragdoll;
  - F6 learned goals and needs;
  - F5 ORCA / crowd LOD.

## Current focus: what the Director needs first

The Director compiles scripted performances to `seq::Actor` in the baked tier. Its Slice 3 plan on
`agent/director` names the M3–M5 interfaces, and what is built below matches them.

| # | Capability | Director slice | Status | Evidence |
|---|---|---|---|---|
| M1 | Entity ↔ actor handoff | 2 | **done** | ADR-758 adopted from `agent/director` (f009638d) as the one mechanism; ADR-820 additions; `test_directing_handoff.cpp` (10, acceptance) + `test_motion_handoff.cpp` (4) |
| M2 | Clip semantics table | 3 | **done** | ADR-821; `scene::clipSemantics`, `Composition::clipSemanticsFor`, `avgen_motion semantics [--json]`; `test_clip_semantics.cpp` |
| M3 | One-shot/transition cue playback + clip events on the timeline | 3 | **done** | `ClipCue::{playback, then}`, `seq::resolveCue`, `seq::clipEventSeconds`; Rook plays `Jumping` once and is handed back to his gait on Glowmere |
| M4 | Jump arc shared with `Airborne`, minimum apex, landing check, per-character jump capability in data, `Jump_running` mapped | 3 | **done** | ADR-822; `planJump`/`minimumApex`/`checkArc`, `EntityDesc::jump`, actor `airborne` spans, `seq/jump.hpp`; `test_jump_arc.cpp` |
| M5 | Local retime of an actor | 3 | **done** | ADR-823; `seq::retimeActor`, `ClipCue::offsetSeconds`, `Actor::timeWarps`; `test_actor_retime.cpp` |

### Found while building M4: every autonomous hop was a skid

`explore` re-grounded the body on every step of a hop, and ADR-194's tests counted `Jump` frames
without measuring height.

| Body | Authored hop | Peak before the fix | Peak after |
|---|---|---|---|
| Ember | 2.6 m on the beat | 0.39 m | 2.73 m |
| Vane | 2.6 m on the beat | 0.09 m | 3.00 m |

This is a film change in `glowmere-valley-2`, `-song` and `-atmospherics`, but not in the multicam
film. It is flagged for the owner in `12-motion-stack/03-…png`.

### What M1 is (ADR-758 + ADR-820)
- **Where the mechanism came from.** An actor on a node that an entity drives is a performer. The
  bake skips its transform tracks. The composition applies the actor as a `DirectorMotion` on play
  and in both replay paths. That motion owns the body outright, and the gait reads the path's
  speed unramped. Height comes from the terrain, and every performance is grounded until M4. The
  performer's signature is in the replay key.
- **Added by ADR-820:**
  - Under a performance, the action tier no longer names the clip. Measured before the fix: the
    decider's `observe` held Rook's rig in `Idle` through a 6 m/s run.
  - A performer's cues stop when its span ends.
  - A cued clip owns the rig, and the gait yields.
  - `Actor::entrySeconds` defaults to 0. A value above 0 blends from the simulated pose. The entry
    point is entity state, so a scrub into the blend matches the play, whether it replays from
    zero or restores a checkpoint.
  - `headingAt`'s header now says degrees.
- **Known, not fixed:** `positionAt` smoothsteps `Smooth` keys, while the baked track uses
  Catmull-Rom.
  - Performances are unaffected, because the Director compiles `Linear` keys.
  - A camera aimed at a smoothly keyed non-performer is slightly off.

### What M2/M3 measure on the scout (ADR-821)

Every one of these clips passes the test's independent check: the feet are off the ground in the
flight, and the body is at its highest at `peak`.

| Clip | Loop | Ground | Events (clip s) |
|---|---|---|---|
| Jumping | yes (it closes) | leaves | takeoff 0.567, peak 0.767 (+0.42), touchdown 1.100 |
| Jump_running | yes | leaves | takeoff 0.067, peak 0.367 (+0.76), touchdown 0.667 |
| Landing | no | leaves | touchdown 0.267 (opens in the air) |
| Fall_loop, Floating, Flying_jet | yes | airborne | none |
| Crazy, the three deaths | no | grounded | plants/releases |

## Remaining work
See "What is left" in the handoff at the top. The earlier list here (Director-facing D, C loading,
§25/§26/§36, B §39) was done by ADR-824 to ADR-835.

## Review renders
Everything is in `~/Desktop/av-gen-review/12-motion-stack/`, which has its own README.
- **01–04:** react loop vs once, the measured jump events, and the beat hops.
- **05–06:** the four review arms on Rook.
- **07, 09:** Rook's turn before and after (ADR-829).
- **10–11:** Sage calmed.
- **12–14:** Ember's bank: the terrain and her feet, before and after (ADR-830/831).

## Decisions needed (owner)
None are open. All the earlier questions were ruled on; see "The owner's rulings to date" at the
top. The next genuine owner decisions would be putting the matcher in the film (C §86/§94), and
routing the cinematic signals (ADR-834) into anything shipped.

## Test status

At 27ddfc7b (main b0f141b5 merged), release build, run one after the other:
- **CPU `avgen_tests`:** 3,469 cases, exit 0. One expected failure: `[!shouldfail]` in
  `test_character_lab_slopes.cpp:187`. 16 skipped.
- **GPU `avgen_render_tests`:** 492 cases, exit 0, 1 skipped (NDI).
- **Seek parity:** `[adr800]`, `[adr870]`, `[motion][direction]` and `[seek][engine][glowmere]`
  pass. Scrub equals play exactly on the multicam film.
- **Film invariants:**
  - The multicam film's entity trace is identical across ADR-833, ADR-834 and ADR-835 (60 s hash
    `5f6071349066d4eb`).
  - Every alien's worst foot step per posed frame is under 0.15 m over 40 s (`[adr830]`).
  - Rook's turn is under 0.03 m (`[stride]`).

## Log
- 2026-09-25: paused at the owner's request; main b0f141b5 merged; handoff written at the top.
- 2026-09-25: ADR-829 (Rook's turn: stride warp), ADR-830 (Ember's bank: terrain cliff), ADR-831
  (crowd push while pivoting), ADR-832 (interaction and volume triggers), ADR-833 (semantic tags),
  ADR-834 (cinematic signals), ADR-835 (steering fed). Review files 09 and 12-14.
- 2026-09-25: ADR-824 (scheduled direction, release, runtime goals); ADR-825 (baked database loading);
  ADR-826/827 (analyzer, review build); ADR-828 (CharacterGoal, character events, clip readout).
- 2026-09-24: M4 (ADR-822, including the hop-skid fix) and M5 (ADR-823).
- 2026-09-24: M1 adopted from `agent/director` (ADR-758) with the ADR-820 additions; M2/M3
  (ADR-821); clip-playback lab and two review sheets; both suites green.
- 2026-09-24: worktree created from main 232a50d7; recount of B §39/§66, C, D, the review build, E
  and F against the specs and main's code.
