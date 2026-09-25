# Motion Stack Build Board: progress

Last updated: 2026-09-24
Branch: `agent/motion` (worktree `../av-gen-motion`, from main 232a50d7)
Owner of: motion engine capabilities (`src/entity/`, `src/scene/` motion, clips, rigs, and the
engine side of the actor handoff). Not `src/directing/` or the AI layer.

## Status in one line

The recount is done. **M1–M3 are built**: the handoff, the clip semantics table, and one-shot and
transition cue playback. M4 (jump arc) and M5 (retime) are next.

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
| B Procedural (§1–67) | 54 | 2 | 0 | 11 | 54/56 | §39 and §66 are partial; see below |
| C Matching (§1–95) | 76 | 5 | 1 | 13 | 52 / 25 / 5 | the gates §85 and §86 are **not met in the product** |
| D Behaviour (§1–78 + demo) | 39 + demo | 27 | 2 | 10 | 44 / 16 / 8 | §63 scrub and §66 Glowmere landed; five "done" rows downgraded |
| ★ Glowmere review build (10) | 2 | 5 | 3 | – | 0 / 10 | specified only on the board; no spec text exists |
| E Learned motion (13 board rows) | 0 | 0 | 13 | – | 13 planned | gated; see recommendation |
| F Advanced (9 tracks) | – | – | – | – | 9 gated | pieces exist in F7, F8 and F9; no track has been measured or classified |

### B: the two open rows
- **§39 Obstacle-responsive motion: partial.** Built and tested, and fed nothing.
  - What exists: the seam (`MotionRequest::steering`, `CharacterIntent::steering`) and its sum in
    the controller and the matcher.
  - The gap: no code in `src/` writes `intent.steering`. Avoidance rewrites the desired yaw in the
    behaviour tier instead. `advanceMotion` returns early unless `proceduralMotion` is set, and
    only labs set it.
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
- **Downgraded to partial:** §37, §39, §40, §76 and §81.
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
  and effects carry no semantic tags.
- **Downgraded from done:**
  - §24: no relationship labels and no follow.
  - §25: no semantic categories.
  - §26: none of the spec's event kinds has a producer.
  - §32: live play culls and coarsens by camera distance.
  - §62: interactive play uses wall-clock dt.
- **Not started:**
  - §36 camera/cinematic signals (`isInShot`, `screenImportance` and the rest);
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
| 1 | Audit A–D | partial (this recount is most of it) |
| 2 | Wire the aliens to the autonomous stack | partial: behaviour yes, multicam scene only; motion no (`proceduralMotion` off) |
| 3 | Per-character personality, data-driven | done (multicam) |
| 4 | Perception + smallest event set | done (one event, `abduction/beam`) |
| 5 | UFO reactions staggered | partial: the five `react` blocks are identical and onset stagger was never measured |
| 6 | Multicam: the director observes | partial: the camera reads the scenario's beats, not character events; baked `heroAtCut` goes stale |
| 7 | Debug viz + state inspector | not started (text trace only) |
| 8 | AI ON / OFF comparison | not started |
| 9 | Offline animation-quality validation on the result | not started |
| 10 | Review report + measured cost | partial: timings were taken under load |

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
| M4 | Jump arc shared with `Airborne`, minimum apex, landing check, per-character jump capability in data, `Jump_running` mapped | 3 | next | — |
| M5 | Local retime of an actor | 3 | not started | — |

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

## Review renders
`~/Desktop/av-gen-review/12-motion-stack/` (fixture: `examples/labs/motion/`):
- **`01-react-crazy-loop-vs-once.png`: the owner's decision 2.** It compares `Crazy` looping
  (autonomous `react` today) with `Crazy` played once and held.
- **`02-jumping-measured-events.png`:** the measured takeoff, peak and touchdown on the pose.
  Checked by eye; they are right.

## Decisions needed (owner)
1. **Rook's jump capability.** The Director's benchmark needs a 5.75 m apex; the default is 1.1 m.
   Should the per-character jump capability be data (a max apex per character), and what is
   Rook's? Or is the Umbra leap refused? The lead's ruling for now: build the per-character seam in
   M4, keep Rook at the 1.1 m default, and invent no larger apex.
2. **One-shot semantics for autonomous characters.** Sequence cues now use the measured
   semantics. The aliens' own clips are unchanged, as the lead instructed. The review render is
   `12-motion-stack/01-…png`. Should `react` hold `Crazy`'s last frame rather than repeat it?
3. **Phase C gate (§86) and Phase B §66:** switching the matcher, and `proceduralMotion`, on for
   Glowmere is a visual call. Nothing downstream (the Director or E) has a product path until one
   of them is on.
4. **The Glowmere review build exists only on the board.** Please confirm its scope, including
   whether it targets the multicam film, the only scene whose aliens are wired.

## Test status
At a6228408 (M1–M3), release build, with a second `cmake --build` doing no compile or link:
- **CPU `avgen_tests`:** 3,253 cases, exit 0. One expected failure, the `[!shouldfail]` at
  `test_character_lab_slopes.cpp:187`. 16 skips: the hardware skips, plus the gitignored demo and
  100STYLE packs.
- **GPU `avgen_render_tests`:** 443 cases, exit 0. One skip (NDI).
- **New tests:** `[handoff]` 14 cases, `[semantics]` 11. Every new check was broken and seen red
  before being restored: 3 mechanisms for the handoff, 4 for the semantics.
- **Selecting `[seek]` by tag also runs main's hidden `[.known-defect]`** "UFO stack at 150 s is
  the same played and scrubbed", which fails as it does on main. It is not in the default suite.

## Log
- 2026-09-24: M1 adopted from `agent/director` (ADR-758) with the ADR-820 additions; M2/M3
  (ADR-821); clip-playback lab and two review sheets; both suites green.
- 2026-09-24: worktree created from main 232a50d7; recount of B §39/§66, C, D, the review build, E
  and F against the specs and main's code.
