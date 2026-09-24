# Motion Stack Build Board: progress

Last updated: 2026-09-24
Branch: `agent/motion` (worktree `../av-gen-motion`, from main 232a50d7)
Owner of: motion engine capabilities (`src/entity/`, `src/scene/` motion, clips, rigs, and the
engine side of the actor handoff). Not `src/directing/` or the AI layer.

## Status in one line

The recount is done: the 21 Sep board was stale in both directions. **Nothing from the build list
has been built yet.** The next work is the four capabilities the Director needs for its Slices 2–3.

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

The Director compiles scripted performances to `seq::Actor` in the baked tier (feasibility report
§5.4), so every item below is engine support for that path. Ordered by what unblocks Slice 2, then
Slice 3.

| # | Capability | Director slice | Status | Size |
|---|---|---|---|---|
| M1 | Entity ↔ actor handoff: an entity whose node an actor drives is held for the actor's span, with no double-add, the actor's clip cues winning over the gait, an entry blend from the simulated pose, and release in place. Scrub-exact. | 2 | not started | M |
| M2 | Clip semantics derived from the existing analysis (`analyseClip`, `measureLoopClosure`, contacts and phase): activity, duration, loop/one-shot, grounded/airborne, root motion, horizontal speed, take-off/peak/touchdown, start/end pose, interruptibility | 3 | not started | M |
| M3 | One-shot and transition playback on `ClipCue` (`playback: auto/loop/once`, `then`), plus clip events mapped to timeline seconds through a cue | 3 | not started | S–M |
| M4 | Jump as a pure arc shared with `Airborne` (`planJump`, minimum apex to clear, landing check) and a per-character jump capability in data; map `Jump_running` | 3 | not started | M |
| M5 | Local retime of an actor (stretch keys, path and cue times, scale cue speed) | 3 | not started | S |

### Handoff design (proposed, for the Director to confirm)
- **The input.** An actor whose node is bound to an entity holds that entity for the actor's span
  (an explicit `[start, end]`, defaulting to the actor's own extent). The span is an **input** to
  `EntityWorld`, read by play and replay alike. That is what makes it scrub-exact: ADR-700
  checkpoints copy the whole `Entity`, and changing the spans bumps the input epoch.
- **Inside the span:**
  - The body's travel and yaw come from the actor's pure `positionAt` and `headingAt`, and its
    speed from their derivative.
  - The node's timeline delta is **not** added on top, which is today's double-add
    (`applyNodeOffsets`, `fieldPosition`).
  - The gait state machine yields the clip to the sequence's cues. Look and foot layers still run.
  - The behaviours see `driven`, so wander and explore keep their state.
- **Entry:** blend from wherever the simulation had the body over `entryBlend` seconds. This
  answers the Director's open question: the performance starts from the simulated pose, and no
  pause is needed.
- **Release:** the body stays where the actor left it, the behaviours resume, and the decider
  replans.

## Decisions needed (owner)
1. **Rook's jump capability.** The Director's benchmark needs a 5.75 m apex; the default is 1.1 m.
   Should the per-character jump capability be data (a max apex per character), and what is
   Rook's? Or is the Umbra leap refused? I will build the data seam either way.
2. **One-shot semantics for autonomous characters.** Sequence cues will default to the measured
   loop semantics (`Crazy`, `Landing` and the deaths do not close). Applying the same to the
   entities' own clip maps changes what Glowmere's aliens look like (`react` = `Crazy` would hold
   its last frame rather than repeat). My proposal: sequences now, entities after a review render.
3. **Phase C gate (§86) and Phase B §66:** switching the matcher, and `proceduralMotion`, on for
   Glowmere is a visual call. Nothing downstream (the Director or E) has a product path until one
   of them is on.
4. **The Glowmere review build exists only on the board.** Please confirm its scope, including
   whether it targets the multicam film, the only scene whose aliens are wired.

## Test status
- Release build of this worktree at 232a50d7 succeeds (both test binaries link). No suite has been run on `agent/motion` yet: nothing but this record has changed from main.

## Log
- 2026-09-24: worktree created from main 232a50d7; recount of B §39/§66, C, D, the review build, E
  and F against the specs and main's code.
