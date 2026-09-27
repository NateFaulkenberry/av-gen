# GV3 revision: progress and state

This is the operational state file. Update it whenever the state changes. It was last updated 2026-09-27 12:45.

## SINCE THE HANDOFF (2026-09-27, from 12:20). Read this first, then the handoff below
The coordinator restarted after the old account's session limit (the owner switched accounts). Same process, so
two of the four Phase 3 agents survived: **gv3-world** kept running, and **gv3-cut** was resumed from its transcript.
**gv3-look** and **gv3-cast** had no transcript in the new session and were **relaunched fresh**, each told exactly
where its predecessor stopped.

- **MERGED (12:55): main is `ec515c8b`,** now also with characters (ADR-907–910, the ADR-911 placement amendment).
  Its suites: CPU 3,781 of 3,781, 0 failed, 3,233 s; GPU 533 cases, 532 passed, 1 skipped. **9 of 11 engine streams
  are in main.** The owner approved the fast-forward after the classifier refused it. Main was then merged into
  `gv3/production` (`3a00b180`). The shared build `av-gen-engine-2` is main.
- **Render is integrating** on a new branch, **`integrate/render`**, in `~/Documents/GitHub/av-gen-signals`: `776b1a87` =
  `ec515c8b` + `agent/render`. The merge was textually clean (the handoff expected conflicts; none). `integrate/revision`
  stays at the tested `ec515c8b`.
  - **Built** (12:45, exit 0; a true no-op afterwards; ctest lists 3,788 tests, +7 from render).
  - Next: the full CPU suite, then the full GPU suite in a window with no CPU suite. Then fast-forward
    `integrate/revision` to it, and main after the owner allows.
  - **Render's own full GPU suite (`full-2`) was withdrawn** from the lock queue at 12:57, with the owner's approval.
    It had waited 99 minutes; a CPU suite beside it would have voided it as evidence; the integration's GPU suite
    covers render's code too. A full GPU suite takes about 20 minutes (`full-1`: 1,166 s).
  - **The full CPU suite is running** on `776b1a87` (from 12:58; log `coord-render-int-cpu-full.log` in the
    coordinator's scratchpad, `/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/08ff7bb5-10b9-4c50-a00a-c45ffb80e000/scratchpad/`).
- **The alien float's cause is found. It is data, not the engine.** gv3-cast found that the multicam gives ember and
  vane an entity reaction `audio.bass -> liveliness/bounce` (+0.45 on a 0.32 bounce, 60 ms attack). With the authored
  bounce at 0, the reaction alone still lifted them (p90 0.16-0.17 m, max 0.22-0.23 m). Rook and tide match ADR-895.
  GV3 drops the reaction (`ALIEN_REACTIONS_DROPPED` in `cast.py`); gv3-cast verifies it on a trace. **No stride-bob
  engine stream.**
- **The world closure's alien reroute is explained and fixed in data** (gv3-world W2c, `7259efad`): ending the river
  in a pool joined its banks for the navigator. The river now runs edge to edge, with its head bent 45° east.
- **Launched: navfix** (ADRs 932-933, plus an ADR-931 amendment) in `~/Documents/GitHub/av-gen-navfix`, branch
  `agent/navfix`, from `ec515c8b`. Brief: [briefs.md](briefs.md) § "Engine follow-ups found in Phase 3". It covers
  routes that respect connected regions, no pacing on urgent reactions, and the evaluator hook passing `--world-preview`.
- **gv3-cast iteration 2 played all five events,** E4 included (cow-23 and cow-12 at 111.917 s). Its ADR-910 metrics
  are on disk; the relaunched agent reads and records them.
- **The GPU queue is long:** gv3-world's batch holds it; render's `full-2`, gv3-look's pair, gv3-world's stills and
  gv3-cut's 25-minute it2 film all wait.

## SESSION HANDOFF (2026-09-27 12:00)
This section supersedes every dated note below it, which are history. The session that wrote it is
ending. **Its agents end with it: a new session cannot message them.** It relaunches fresh agents
from each worktree (prompts below).

### 1. Verify first
```
cd ~/Documents/GitHub
git -C av-gen log --oneline -1; git -C av-gen status --short | wc -l        # main; the owner's checkout (read-only except merges)
git -C av-gen-signals log --oneline -1                                      # integrate/revision
for w in av-gen-gv3 av-gen-render av-gen-gv3-look av-gen-gv3-cut av-gen-gv3-world av-gen-gv3-cast; do
  echo "== $w $(git -C $w branch --show-current) $(git -C $w log --format='%h %ar %s' -1 | cut -c1-90)"; git -C $w status --short | head -5; done
ps -eo pid,etime,command | grep -E "[a]vgen_tests|[a]vgen_render_tests|[c]test |[n]inja -|[g]pu-lock|avgen --project|[a]vgen_cast_trace" | cut -c1-120
~/Documents/GitHub/creative-critic/.venv/bin/critic health
```
Leftover processes from the old session may still be running (suites, renders, traces). Let them finish, or stop them if they are orphans. Check that the GPU lock is not held by a dead process.

### 2. Where everything stands
- **Phases:**
  - Phase 0 (the evaluator gate): passed.
  - Phase 1 (the audit): done.
  - Phase 2 (the engine): 8 of 10 streams in main, 9th integrating, 10th finishing.
  - Phase 3 (GV3 revision): 4 streams in progress.
  - Phase 4 (4K final, report, self-critique): not started.
- **Main is `040d6644`:** signals, routes, emission, song, camera, reactivity, water and setpieces, each integrated and fully tested.
- **The integration** branch `integrate/revision`, in the worktree `~/Documents/GitHub/av-gen-signals`, is **`ec515c8b` = main + characters,** with the ADR-911 follow-placement fix.
  - The GPU suite **passed** (533 cases, 532 passed, 1 skipped). It overlapped 4 other CPU suites, so its bit-identity passes are not clean evidence (the GPU-lock ruling). If a later run disagrees, re-run it in a quiet window.
  - **The CPU suite was running at handoff** (12:07: 27 of about 3,760 tests done, 0 failed; ctest runs the longest first). Log: `/private/tmp/claude-501/-Users-natefaulkenberry-Documents-GitHub-av-gen/004befa7-093f-41d0-b2da-3d5e53a50a3a/scratchpad/coord-integrate5-cpu-full.log`. It may be gone with the old session's scratchpad; if so, re-run `ctest --test-dir build/release -L unit -j 4` there.
  - **If it passes:** `git -C ~/Documents/GitHub/av-gen merge --ff-only integrate/revision` (check that the owner's checkout is clean and on `main`), then merge main into `gv3/production`.
  - **Expected, harmless:** "Glowmere Valley 2 from several viewpoints" is intermittent, and the event-driven agent test's 180 s wall-clock limit fails under heavy load (it passes alone).
- **Render** (`agent/render` in `av-gen-render`, the last engine stream) is **done: `39388364`, report in [stream-reports/render.md](stream-reports/render.md).** CPU 3,686 of 3,706 passed (the 1 failure is the load-sensitive 180 s agent test, which passes alone). **Its full GPU suite on the final code has not run.** Run `AVGEN_AGENT=render tools/gpu-lock.sh build/render-gpu-full.sh full-3` in `av-gen-render` when the lock is free. Then merge it into `integrate/revision` after characters, build, run the full suites, and fast-forward main. **For GV3** (preview and final alike): `post/referenceHeight` 1080, `post/motionBlur/maxRadius` 60, `post/motionBlur/samples` 32, `scene/fogSky` 1.0, `scene/fogSkyDistance` 0. Keep bloom levels 6, stretch 10.386 and tile 20. **The 4K ×2 final costs about 0.34 s a frame, about 77–80 minutes for the film.** **Defect:** `avgen --render` renders bare sky and exits 0 when the scene fails to load, so check every GV3 render log for scene-load warnings. When done, merge it into `integrate/revision`, build, run the full suites, and fast-forward main. It merged main `3f720bfa`, so expect conflicts with setpieces and characters.
- **Two shared engine builds for GV3 worktrees** (each worktree symlinks `build/release` to one):
  - `~/Documents/GitHub/av-gen-engine` = `040d6644`;
  - `~/Documents/GitHub/av-gen-engine-2` = `ec515c8b` (with characters). Both are `BUILD-READY`.
  - For render's post and 4K features, build `av-gen-engine-3` at the next main and tell the streams. Never rebuild one under a running render.
- **The Critic** `6ed180c`: its adapter reads set pieces and ground samples. The daemon on 8765 was restarted at about 11:40 on that code.

### 3. The Phase 3 streams (briefs: [briefs.md](briefs.md) § "Phase 3: the GV3 scene revision")
| Stream | Worktree / branch | Owns | State at handoff |
|---|---|---|---|
| gv3-look | `av-gen-gv3-look` / `gv3/look` | `look.py`, `directives.py`, `reactivity.py`, the water and wind blocks | **checkpoint `a559756f` (WIP); the configured tier is done.** `reactivity.py` proposes on a scratch copy, applies EDITS, DROPS and ADDS with reasons, installs as ADR-927 does, and audits: 87 of 87 routes and 72 of 72 tracks live. It includes: the elder's heartbeat on the scored kicks; heroes ranked by the cut; the elder's ring every bar; a valley-wide drop ring; the riser's roll on the fungi; the drop's grade held at the plateau's (ev +0.15, sat 1.10, temp +0.05); the water base per ADR-915, the tears, `shoreFade` → `edgeFade` 1.6; the wind retuned; the aurora's per-frame spectrum response off (it made the sky jump up to 44% between frames). It runs on engine-2. **Left:** the pixel tiers (before/after clip pairs, route-locked checks per group, hue histograms plateau against drop, wind and tear stills at 1080p). **Outside its files:** `make_glowmere_valley_3.py` passes the scene to `look.apply_base` and `look.apply_motifs`. See `phase3/look.md` |
| gv3-cut | `av-gen-gv3-cut` / `gv3/cut` | `shots.py`, `rig.py`, `cuts.py`, `framing.py`, `review.py`, `install_cut`, `autoDirector` and a new `songcut.py` | **checkpoint `c6e3a646`.** The cut's spans come from the Director (`songcut.py` runs `avgen_song_cut`, records `song_cut.json`, `--recut` adopts a new one): 73 spans, CV 0.53, the riser 4-4-2-2-2-1-1 beats, the drop its own 2-bar shot. Every span authored. Aliens lead 13% (the generator refuses more than 20%). 18 of 22 follow rigs pass the stability bar (first pass: 5 of 16). Next: the it1 render, the Critic jobs, novelty trims (`trim.py`), composition fixes, before/after sheets. See `phase3/cut.md` |
| gv3-world | `av-gen-gv3-world` / `gv3/world` | `world.py`, `offline.py` | **checkpoint `6d25ed53`.** Both valley ends closed with ridges, banks and river-path edits; `world.py --check [--trace]` finds 0 of 452 traced views with an open end (baseline 275, in 21 shots). The ground from z −190 to 170 is unchanged to the mm, and every rig key is byte-identical. `offline.py` (behind `--final`) applies render-post's configuration, with shadows step-keyed to 300 m on the wides. **Not yet run:** before/after stills, the fog-march calibration, the 4K cost ranges. See `phase3/world.md` |
| gv3-cast | `av-gen-gv3-cast` / `gv3/cast` | `cast.py`, `ufo.py` and `ufo.plan.json` | **checkpoint `0ff69eea` (WIP, iteration 2 being measured).** `cast.py` carries the characters recipe, the re-homing, the scout (hero radius 4.92 m), and removes the hand-written scenario and horse keys. `ufo.py` compiles E1–E5 with `avgen_cast_trace --plan --save-project` and splices in only the plan's products, because a headless save photographs 745 parameters, clamps rippleScale 5.2 to 4.0 and drops 9. Iteration 1: aliens' longest still stretch 3.2–10.3 s; animals 0 reversals, 5 turns over 90°, 5.2 s on steep ground, 0 s facing uphill. Beats: E1 beam 13.900 s; E2 cross 26.650 s; E3 lift 66.967 s; E5 beam 170.350 s, lift 172.783 s, horse gone 177.717 s. E4 moved to (−69, 6) radius 20 on cow-12 and cow-23, re-trace running. See `phase3/cast.md` |

- Each stream's own notes, `docs/glowmere-valley-3/revision/phase3/<topic>.md`, hold its iterations and next steps.
- The streams never commit generated project files. After merging, regenerate with `python3 tools/make_glowmere_valley_3.py`.

**Relaunch prompt for a Phase 3 stream** (a fresh agent):
> You are the gv3-<topic> stream of Phase 3 of the Glowmere Valley 3 revision. Read ENGINEERING-RULES.md, then briefs.md § "Phase 3" (the common rules and "gv3-<topic>"), then the stream reports it names. Your worktree `~/Documents/GitHub/av-gen-gv3-<topic>` (branch `gv3/<topic>`) holds your predecessor's work. Read `git log gv3/production..HEAD`, `git status`, and `docs/glowmere-valley-3/revision/phase3/<topic>.md`, which has its iterations and next steps, then continue. The shared engine is `build/release`, a symlink. The Critic daemon on 8765 belongs to the coordinator. Commit early; never commit generated project files; end with the final report your brief describes.

**Relaunch prompt for render:** the rules, briefs.md § "render (ADRs 917–919)", and "inspect `git log main..HEAD` and `git status` in `~/Documents/GitHub/av-gen-render`, read `docs/development/render-design-notes.md`, finish, run the suites, and report".

### 4. What remains, in order
1. **Finish the engine:** characters into main (above), then render (merge, suites, fast-forward). Launch the two engine fixes in §5, navigation regions with the loop veto and the stride-bob excess, as focused streams from main. Merge them through `integrate/revision`, then build the engine at the final main for the GV3 streams.
2. **Finish the Phase 3 streams**, each with its evaluator evidence.
3. **Merge Phase 3:** merge each `gv3/<topic>` into `gv3/production` and regenerate the project. **Then re-validate across streams** (§5): re-trace the whole film, then check the follow rigs' stability, the alien framing and screen time, E1–E5's measured beats, and the ADR-910 metrics. The world closure changes the aliens' paths.
   - Resolve cross-stream issues. For example, gv3-cut frames E1–E5 at the beat times gv3-cast measures, and the Song Mode events come from gv3-cast's trace.
4. **The whole film:**
   - Render a full 960×540 preview: `tools/gpu-lock.sh build/release/src/avgen --project examples/world/glowmere-valley-3.json --render $PWD/build/gv3/r1.mov --size 960x540`, about 25 minutes.
   - Regenerate the Critic's inputs (INTEGRATION_GUIDE.md §6) and evaluate the whole film in preview mode, against iteration 0 (`job_1a0def77d9084ad74`, session `gv3-revision`, track `film`).
   - Iterate on the findings. Record each round in [04-iterations.md](04-iterations.md).
5. **The 4K final:** the generator with `offline.py`'s `--final`, render-post.md's command, about 1.5–2.5 h. Evaluate it at `--delivery 3840x2160`.
6. **The deliverables** (brief DELIVERABLES and §18): the revision report, before/after evidence per category, and the Director's self-critique. Review material goes in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/`.

### 5. Known open issues
- **ENGINE DEFECT, which breaks the owner's hard requirement (walk → stop → 180° → back).** Found by gv3-cast; the next engine fix to launch.
  - `NavigatorPath::route` (`src/entity/action.cpp`) ignores the navigation grid's connected regions, so a goal across the river reads "Ready".
  - After hearing E5's beam, ember paced the river bank walk → stop → 180° → back 8 times in 40 s (176–216 s).
  - Urgent reactions are exempt from ADR-909's loop veto.
  - It is worked round in GV3's data for now (E5's reaction is "stop and watch where you stand"). The engine needs: routes that respect connected regions (a goal in another region gets the nearest reachable point, or is refused), and the loop veto applying to urgent reactions. Each needs a control arm.
- **ENGINE DEFECT (the alien float):** the stride bob. rook and tide match ADR-895's formula; ember and vane rise about 2.4× more (walk p90 0.35 m, max 0.45–0.71 m). The cause of the extra factor is not found; gv3-cast ran a bounce-0 control. Fix it in the engine, not in data.
- **CROSS-STREAM: closing the world reroutes the aliens** (gv3-world found it; the cause is not found).
  - With the valley's ends closed, ember and rook diverge from about 13–17 s by up to 100 m, and sage, vane and tide later. The alien follow shots' cameras move by up to 77 m, although the ground they walk is unchanged.
  - Suspects: the navigation grid's connectivity, the water attraction, and the index-keyed scatter glow.
  - **So the Phase 3 merge must re-validate on the merged project:**
    - a whole-film cast trace;
    - gv3-cut's follow-stability bar and alien framing;
    - E1–E5's beats (E4's cows especially);
    - the ADR-910 metrics.
  - If the sensitivity is a defect (a path should not move 100 m because ground 300 m away changed), it is engine work.
- **A headless save photographs the run** (see the memory note on project vs scene state). On GV3 it wrote 745 parameters, clamped rippleScale 5.2 to 4.0 and dropped 9, so `ufo.py` splices in only the plan's products. Never adopt a headless-saved GV3 project wholesale.
- **Four aliens float 0.2–0.5 m while walking** (measured on engine `040d6644`); gv3-cast is checking it on engine-2. If it is an engine defect, it needs an engine fix, not data.
- **E4 cannot play on the tuned cast** (gv3-cut measured it): the re-homed cows graze 35–40 m north of E4's region (−55, 40), radius 30. Move the region to about (−70, 8), or name cow-12 and cow-23 (gv3-cast was told).
- **E1–E4 were off screen** in the first-pass cut; gv3-cut frames them. Check `framing[].on_screen` in the Critic's report.
- **The Director's evaluator hook** (ADR-931) calls the adapter without `--world-preview`, so it has no ground data. A small engine follow-up.
- **`scout` needs a hero record** (radius about 4.9 m) for the Critic.
- **Engine defects recorded, not fixed:**
  - the Modulation panel's route and source edits have no undo;
  - other HIST readers (speed and velocity signals, Trail ribbons, ADR-545 velocity) still read across placements;
  - decider considerers have no slope limit;
  - multi-leg `move` errands brake at every waypoint;
  - `masterGain` pulls Multiply routes toward 0;
  - the ecology light takes the layer's colour, not the program's.
- **`glowmere-valley-2-multicam` stays untouched** (the owner's rule). Its stale-key warnings are expected.

### 5b. The bottleneck is the GPU
At handoff, four GV3 streams and render queue on `tools/gpu-lock.sh`: gv3-cut's 25-minute full-cut render, gv3-look's clip pairs (about 50 minutes behind the lock), gv3-world's stills and 4K ranges, and render's tests. Keep GPU jobs short: clips of the shots that matter, 2 s at 4K. Run the CPU-only checks (`--audit-routes`, `--propose-reactivity`, `avgen_song_cut`, `avgen_cast_trace`, `world.py --check`) while waiting.

### 6. Rulings in force
- **Engine changes:** they go to main through `integrate/revision`, with full suites before each fast-forward.
- **The GPU lock:** CPU suites never take it. A GPU suite offered as evidence runs when no CPU suite does.
- **Logs:** names start with the stream's name.
- **No compatibility shims** (ADR-441/442).
- **UI reach:** anything visible is controllable, under viewer-word labels.
- **Never:**
  - commit the song or the licensed assets;
  - run the windowed app for automation;
  - run `tools/make_abduction_scenario.py`;
  - modify `glowmere-valley-2-multicam`.
- **The owner's latest instruction:** "use as many agents as you want to parallelize remaining work, save status updates often".

## Start of a session: do this first
1. **Read this file**, then [00-brief.md](00-brief.md) (the owner's revision spec, verbatim).
2. **Check the state of every repository involved:**
   ```
   git -C /Users/natefaulkenberry/Documents/GitHub/av-gen log --oneline -3      # main (the OWNER's checkout: read-only for you)
   git -C /Users/natefaulkenberry/Documents/GitHub/av-gen-gv3 status --short; git -C ~/Documents/GitHub/av-gen-gv3 log --oneline -3
   for t in signals routes emission characters camera water; do echo "== $t"; git -C ~/Documents/GitHub/av-gen-$t log --oneline main..HEAD; git -C ~/Documents/GitHub/av-gen-$t status --short | wc -l; done
   git -C ~/Documents/GitHub/creative-critic log --oneline -3
   ps -eo pid,etime,command | grep -E "avgen_tests|avgen_render_tests|ninja|avgen --project|critic" | grep -v grep
   ```
3. **Verify, don't assume.** A worktree with uncommitted changes and no running agent means a stream was interrupted. See "Relaunching" below.

## State after the usage limit (2026-09-26 14:21): read this first
The session hit its usage limit at about 10:30 and resumed at 14:21. **The owner's instruction since then: at most one or two subagents in parallel**, to spare the session limit; raised to **three** at 15:35. No stray processes were left; two stuck wait loops were removed. Nothing is merged into main since `0b623b88`, and no GV3 scene edit has been made. The evaluator gate passed.

| Stream | ADRs | Branch head | State |
|---|---|---|---|
| routes | 900–902 | **merged into main** (`808f32e4`) | Built. Full CPU suite passed after the pause (3,571 cases, 3,551 passed, 19 skipped, 1 expected failure, exit 0). **Left:** 3 GPU failures unexplained (`test_render_job.cpp:248` readback ring vs sync path, `:318` EXR determinism, `test_glowmere_valley_2_views.cpp:201` camera 72 m off). The camera branch passes all three, so routes is the likely cause; the suspects are in its commit message. Then the report. UI done: route rows edit delay and depth, and show liveness badges. |
| camera | 911–913 | `3d9795f2` (WIP checkpoint), clean | All seven deliverables committed. Full GPU suite 510/509 passed/1 skipped. GV3's follow shots went from 10 of 11 failing the bar to all 11 passing. **Left:** UI reach (plan in the WIP commit: a control table in `scene/camera_rig.hpp`, drawn in `ControlPanel::drawCameras`); the full CPU suite (it stopped at 2,650 of 3,532 cases with one wall-clock failure under load at `test_directing_agent.cpp:379`). GV3 rig values are in ADR-913 and [audit/data/camera-gv3-recommended-rig-changes.json](audit/data/camera-gv3-recommended-rig-changes.json). |
| water | 914–916 | `ddf44a85` (tears WIP), clean | ADR-914 (bounded advection) and 915 (fades count 1080-row reference pixels) finished and tested. **Left:** re-run `[tears]`, `[water]` and `[water6_2]` GPU tests after the last shader change; UI reach (a `water/tears/` sub-group); ADR-916; full suites. **For GV3:** go back to ripple 0.1 at rippleScale 2.6, with the bass route at 0.03 (ADR-915 makes the final-only values wrong). Stills and scripts are in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/water-work/`. |
| setpieces | 928–931 | `905c91ac`, clean | `2ea9c7b5` built and tested: templates (abduction with 1–3 animals, survey, flyby), `PlanSetPiece` with validation, staging at timeline seconds, a bus-id bug fixed; 23 cases pass. `905c91ac` (`avgen --plan`, cast_trace `setPieces`) was never compiled. **Left:** build and fix; the UI home (a "UFO set pieces" section in the Director panel); the three-abduction end-to-end proof; the evaluator hook; ADRs; the full suite. The Critic's adapter should read the trace's `setPieces` instead of hard-coding GV3's beats. Notes: `docs/development/setpieces-design-notes.md` in the worktree. |
| render | 917–919 | `7f36b8e3` (WIP), clean | Built, and the CPU tests pass. `post/referenceHeight` (720) scales every pixel-sized post value; `scene/fogSky`, a sky-radiance fog map; offline floors. **Nothing has run on the GPU.** **Left:** GPU tests and their thresholds, suites, re-baselining small-frame bloom tests, GV3 evidence, the 4K cost, the ADRs' measurements. World edge: report only (a backdrop ring would take 1–2 days). Notes: `docs/development/render-design-notes.md`. |
| signals | 896–899 | **merged** (`e0657a26`) | Done. |
| emission | 903–906 | **merged into main** (`808f32e4`) | Report in [stream-reports/emission.md](stream-reports/emission.md). |
| characters | 907–910 | `5dc541db` + **22 uncommitted files** | Died mid-fix (gait state must start as authored for bodies seeked but never stepped). No report. |

**Round 1, launched 14:35:** the signals finisher (agent `a544c532ad5e8e2f3`) and the routes finisher (`a48d9ba7c7b4c3bf2`).
- **Main baseline for routes' GPU failures,** run by the coordinator on main's own build (the gv3 worktree's build is main's tree, `a58f77ac`): the readback-ring and EXR-determinism tests pass on main, so those two failures are routes'.
- **"Glowmere Valley 2 from several viewpoints" fails on main too,** identically (`test_glowmere_valley_2_views.cpp:201`, camera 72.478889 m off). It is a pre-existing defect, not caused by any stream. Nobody owns it yet.

**15:35: the owner allowed a third agent.** The emission finisher (`a272c875ba85fb192`) joined round 1, because it is on the critical path for the reactivity planner.

**Merged: signals** into main as `e0657a26` (ADR-896–899; the branch's suites were CPU 3,554/3,554 and GPU 510 with 509 passed and 1 skipped). Main was then merged into `gv3/production` (`cfd50932`).
- The coordinator restored `examples/world/glowmere-valley-2-multicam.json` on the branch (`fe711eab`), because the owner's brief forbids modifying it. Its stale `control.phraseBars`/`sectionPhrases` keys are ignored at load with a warning. The multicam tests passed with the file restored: 17 cases, 12,373 assertions.
- **For GV3:** delete `control.phraseBars` and `control.sectionPhrases` from the generator's project, and pin `"music/meter/bar1Beat": 0, "music/meter/phraseBars": 8`. `beat.count` is 0 at bar 1; bar n, beat m is `(n−1)×4 + (m−1)` beats. `audio.onsetLow` matches the scored kicks (precision 0.969, recall 0.979), but fires 0–18 ms late, so keep the scored kicks where GV3's 20 ms lead matters. For the arrival's sparkle, use the hat rate or `audio.energy`, not the treble level.

**Launched: song** (wave 2, agent `ab2d20dab3b1ac697`) in `~/Documents/GitHub/av-gen-song`, branch `agent/song`, from `e0657a26`.

**Integration in progress (19:05).** The branch `integrate/revision` lives in the finished signals worktree (`~/Documents/GitHub/av-gen-signals`), whose build already matched main, so only changed files recompile.
- **Routes is merged into it,** and passed its final suites on its own branch (CPU 3,575 cases, 3,555 passed; GPU 509 cases, 508 passed, 1 skipped). Its report is in [stream-reports/routes.md](stream-reports/routes.md).
- **Two semantic conflicts with signals** were fixed in `d202eb53`: the liveness inputs now carry the engine's `analysis::Meter`, and the seek replay fills `SourceContext::musicalBeats`.
- **Targeted tests on the combined build pass:** routes 57 cases, signals 36 cases, multicam 17 cases.
- **Next:** merge emission when it reports, then run the full CPU and GPU suites once on the combination. The GPU suite goes in a window with no other CPU suite running. Then `git -C ~/Documents/GitHub/av-gen merge --ff-only integrate/revision`.
- **Launched: the camera finisher** (`aaf13597ae6a51721`). It merges main into `agent/camera` first, to resolve its `camera_director.cpp` conflict with signals.

**Integration, continued (19:55).** Emission is merged into `integrate/revision` too (`7b030ac4`). Two more semantic fixes:
- The ADR-906 trigger-clock bind in `Engine::update` takes `meter()`.
- ADR-905 stopped registering `material/<p>/emissionIntensity` on programs that emit nothing, so `Registry::checkTarget` now asks the scene's facts for a reason before it reports `unknown-target`. ADR-902 is amended (`06be0d88`).

Targeted tests on the three-stream build all pass: routes 57, signals and emission 51, liveness 31, emission GPU plus render-job 27 cases under the lock, multicam 17. **The full GPU and CPU suites are running** on `06be0d88` (logs `scratchpad/integrate-gpu-full.log` and `integrate-cpu-full.log`). If they pass, fast-forward main to `integrate/revision`, then merge main into `gv3/production`.

**Launched: reactivity** (wave 2, agent `a2e7d46e9e7bd31bd`) in `~/Documents/GitHub/av-gen-reactivity`, branch `agent/reactivity`, from `integrate/revision` `06be0d88`. Agents running: camera finisher, song, reactivity.

**MERGED (21:05): main is `808f32e4`,** signals, routes and emission integrated and tested. Main was then merged into `gv3/production` (`a7687bc9`).
- **Full suites on the integration:** GPU 519 cases, 518 passed, 1 skipped, exit 0. CPU via `ctest -L unit -j 4`: 3,626 tests, all passed except four test-harness issues, fixed in `808f32e4` and re-run:
  - three route-audit test names began with "--", which ctest hands to Catch2 as an option;
  - the Rook/Umbra "loads cleanly" test now accepts the two ADR-896 stale-key notices on the owner-frozen multicam file.
- **The CPU suite through ctest** takes about 50 minutes, against 2 hours run serially. Use it.
- **Streams branched before this main** (camera, from `e0657a26`; song, from `e0657a26`; reactivity, from `06be0d88`) will need main merged in at integration. Reactivity has been told.

**2026-09-27 00:25, after the second usage limit (it reset at 00:20).**
- **Song is done** (report in [stream-reports/song.md](stream-reports/song.md); its GV3 cut JSONs are in `audit/data/song/`). It is merged into `integrate/revision` as `819d992a`, and builds.
- **Camera** was code-complete: UI reach committed (`a32e52ff`), main `e0657a26` merged. Only its full suites and report were left. It is merged into `integrate/revision` too (one index conflict); the build is running.
- **The coordinator runs the song and camera suites** on the integration build, to save agent tokens. Then main fast-forwards.
- **Reactivity:** 14 commits including main `808f32e4`, and 3 uncommitted files. Resumed (`a2e7d46e9e7bd31bd`) to settle them, run its suites and report.
- **Characters:** its finisher had just begun. Resumed (`a0e273bebdc7d7224`).
- **Left after these:** water, setpieces, render.

**MERGED (01:50): main is `bc89ce3d`,** now also with song (ADR-920–923) and camera (ADR-911–913). Main was merged into `gv3/production` (`38305a09`).
- **Suites on the integration:** GPU 520 cases, 519 passed, 1 skipped, exit 0. CPU `ctest -L unit -j 4`: 3,664 of 3,664 passed, exit 0.
- **Camera's finisher** died at the limit after committing its UI reach, so it wrote no final report. What it left:
  - the follow knobs are in the Cameras panel under plain names (`6f0832c9`);
  - a knob set there survives a project save (`a32e52ff`);
  - its GV3 guidance is in [stream-reports/checkpoints.md](stream-reports/checkpoints.md) § Camera and ADR-913.
- **5 of 10 streams are merged.** Left: reactivity and characters (running), then water, setpieces and render.

**Reactivity is done (about 02:10)** (report in [stream-reports/reactivity.md](stream-reports/reactivity.md); evidence in `~/Desktop/av-gen-review/reactivity-adr924-927/`, with `install_reactivity.py`). It is merged into `integrate/revision` as `a6598157`, and builds. **Its full suites are running**; when they pass, fast-forward main.
- **The GV3 proposal:** 60 routes and 2 sources, all live. The tuned variant is 63 routes with 0 warnings.
- **The install recipe is in the report.** Remove the shared-material kick and breath routes, use event mode for the pulses, the routes stream's energy arc, and a bar-triggered `bar-wave` field.

**Launching: setpieces finisher.** It merges `integrate/revision` first, where the append conflicts with reactivity are expected. Running: characters (finishing), water (finishing), setpieces.

**MERGED (05:25, after the third usage limit): main is `a6598157`,** now also with reactivity (ADR-924–927). Suites on the integration: GPU 521 cases, 520 passed, 1 skipped; CPU 3,691 of 3,691; exit 0 for both. Main was merged into `gv3/production` (`e166c454`). **6 of 10 streams are merged.**
- **The limit stopped all three running agents again** (it reset at 05:20). All three were resumed:
  - **characters** (`3eb53fdd`, 8 commits; its ADR-910 doc is untracked, plus a probe test): suites and report left;
  - **water** (`f66d65d4`, ADR-916 committed; 22 untracked scratch `zz-water-*` copies not to commit): suites, report and GV3 tear values left;
  - **setpieces** (`82e1dde7`, `integrate/revision` merged, 32 uncommitted files): the proof, UI, evaluator hook, ADRs and suites left.
- **Render is untouched** since its WIP checkpoint (`7f36b8e3`). It is the last stream to launch.

**MERGED (07:30): main is `3f720bfa`,** now also with water (ADR-914–916). The branch already contained main, so the merged tree is the one its suites ran on: GPU 532 cases, 531 passed, 1 skipped; CPU 3,699 tests, all passing except a wall-clock limit under load that passed alone. Report in [stream-reports/water.md](stream-reports/water.md). Main was merged into `gv3/production` (`784061f7`). **7 of 10 streams are merged.**
- **Launched: the render finisher** (`abb3faba9bb55470e`). It merges main first. Running: characters (on its last suites), setpieces, render.

**Characters is done, but held (08:05).** Report in [stream-reports/characters.md](stream-reports/characters.md); recipe in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/characters-work/`.
- **On GV3 with its settings:** animals have 0 reversals (from 27) and 5 s on steep ground (from 480 s); aliens' longest stand is 3–7 s (sage 14.8 s, from 67 s).
- **One CPU failure, #1712.** A performance teleports rook 21.8 m, and ADR-911's follow filter smooths across the jump. That is a latent camera defect the stream exposed.
- **A fix agent** (`a63c21b31dd7a0f6b`) works on `agent/characters` itself: it merges main `3f720bfa`, then makes the follow reference restart when a subject is teleported, with an ADR-911 amendment. **Merge characters only after that passes the full suites.**
- Running: setpieces, render, followfix.

**Setpieces is done (08:30).** Report in [stream-reports/setpieces.md](stream-reports/setpieces.md); the GV3 plan (E1–E5) is in `audit/data/setpieces/gv3-ufo.plan.json`. Its own suites: CPU 3,739 tests with 0 failed; GPU 522 cases, 521 passed, 1 skipped. It is merged into `integrate/revision` with no conflicts. **The full suites are running**; then fast-forward main.
- **Still to do for the Critic:** its adapter must read the cast trace's `setPieces` instead of GV3's hard-coded beats. The code is in the report. Do it in Phase 3, when GV3's inputs are regenerated.
- Running: render, followfix.

**The schedule under the two-agent limit.** Each round is two fresh agents. A fresh agent gets the rules, its brief section, the table row above, and "inspect `git log main..HEAD` and `git diff`, then finish".
1. signals finisher + routes finisher.
2. emission finisher + camera finisher.
3. water finisher + characters finisher.
4. setpieces finisher + render finisher.
5. song (wave 2) + reactivity (wave 2).

The coordinator merges each finished stream through an integration worktree (below), then Phase 3 starts.

**Merging:** merge each finished branch into an integration branch `integrate/revision` in a worktree `~/Documents/GitHub/av-gen-integrate`, created from main. Build there and run the full CPU and GPU suites. Then fast-forward main (`git -C ~/Documents/GitHub/av-gen merge --ff-only integrate/revision`, only after checking the owner's checkout is clean and on `main`). Finally, merge main into `gv3/production`.

## Goal
Revise the music video Glowmere Valley 3 (GV3) to the owner's brief. The phases are ordered and gated:

| Phase | What | State |
|---|---|---|
| 0 | Quality-evaluator gate: the Creative Critic must work in the loop before any scene edit | **passed** 2026-09-26 ([02-gate.md](02-gate.md)) |
| 1 | Capability audit (Director, characters, cameras, modulation, mushrooms/wind, render/post, water) | **done** ([01-audit-and-plan.md](01-audit-and-plan.md), [audit/reports/](audit/reports/)) |
| 2a | Engine wave 1: six streams in parallel worktrees | **in progress** |
| 2b | Engine wave 2: Director capabilities, render/post | **in progress**: setpieces and render launched 2026-09-26; song and reactivity wait for wave-1 merges |
| 3 | GV3 scene revision with the Director's planners and the evaluator in the loop | blocked on 2 |
| 4 | Final 4K render; revision report; the Director's self-critique | not started |

**Owner decision (asked and answered this session):** "Build it; GV3 uses it."
- Build the missing Director capabilities in the engine: music analysis, a reactivity planner, musical shot durations and arcs, set pieces, the evaluator hook.
- The revision takes its structure, pacing and reactivity from those planners.
- Shot composition stays authored and evaluator-checked.

## Completed
- **First pass** of GV3: v0, v1, v2 previews and a 1080p final.
  - Committed on branch `gv3/production`: `3d0e93ce` (production) on top of `0fd83284` (engine).
  - The first pass's record is `docs/glowmere-valley-3/` 00–08 (findings F1–F37).
  - The final is `build/gv3/final/glowmere-valley-3-1080p.mov` (gitignored), with a copy and review sheets in `~/Desktop/av-gen-review/18-glowmere-valley-3/`.
- **The first pass's engine fixes are merged into main:** `0b623b88`, "Merge engine/gv3-first-pass". That is ADR-893 (continuous path level), 894 (waters blend by weight), 895 (eased stride bob), plus the tools `avgen_world_preview --seams`, `avgen_cast_trace` and `tools/contact_sheet.py`. Two tests were re-baselined with evidence.
  - Suites before the merge: GPU 509/508 passed/1 skipped; CPU 3,518 cases, 3,496 passed, 19 skipped, 1 expected failure. The two re-baselined tests' groups passed afterwards.
- **The revision brief** is saved verbatim at [00-brief.md](00-brief.md). The owner's water reference image is outside the repo at `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/water-reference-from-owner.png`.
- **The Creative Critic** is built: a standalone repo at `~/Documents/GitHub/creative-critic` (HEAD `88a928e`), with local commits only and no remote.
- **The Phase 0 gate passed** on 2026-09-26 ([02-gate.md](02-gate.md)). A fresh render of s16, as a PNG sequence and as a video clip, each placed at film time 85.4 s:
  - completed with every core check evaluated (`--strict`, exit 0);
  - read the heartbeat as "configured and observed" (+8.9% and +9.9%);
  - compared correctly: against the v2 film's s16 it found the F37 water fix (shimmer 0.0146 → 0.0043).

  The gate's session is `gate-check`, track `s16-gate`.
- **The Phase 1 audit** is done. Seven reports are in [audit/reports/](audit/reports/), with their scripts and data in [audit/scripts/](audit/scripts/) and [audit/data/](audit/data/). The synthesis and plan are in [01-audit-and-plan.md](01-audit-and-plan.md).

## In progress: wave-1 engine streams
Each stream has a worktree `~/Documents/GitHub/av-gen-<topic>` on branch `agent/<topic>` from main `0b623b88`, with assets linked. Briefs are in [briefs.md](briefs.md); the shared rules are in [ENGINEERING-RULES.md](ENGINEERING-RULES.md).

State earlier in the day (superseded by the snapshot at the top of this file):

| Stream | ADRs | Committed | Uncommitted work at save |
|---|---|---|---|
| signals | 896–899 | — | 43 files modified, 7 untracked; baseline CPU suite was running |
| routes | 900–902 | — | 16 modified, 7 untracked; baseline suite was running |
| emission | 903–906 | — | 27 modified, 2 untracked; baseline suite was running |
| characters | 907–910 | `5dc541db` (ADR-910 quality metrics) | 11 modified, 3 untracked |
| camera | 911–913 | `ae3b75dd` (cast_trace `--camera` pose track and `--start`) | 13 modified, 8 untracked; not yet rebuilt |
| water | 914–916 | `6948f72d` (water parameters unregistered on removal) | 3 modified, 7 untracked |

**The agents' IDs**, valid only in the session that spawned them:

| Agent | ID |
|---|---|
| evaluator | a584cee06bb7eb012 |
| signals | a7d2818efbef31802 |
| routes | aaa7f9d0504bc7135 |
| emission | a9ba616cc9c6a5582 |
| characters | a5ac25e540728df3c |
| camera | affb91850489e817e |
| water | aa297869a066886dd |
| setpieces (wave 2) | a0862ba1386aa8345 |
| render (wave 2) | a98789a22624b1131 |

All seven had been stopped once by an account usage limit and resumed with SendMessage.

**Relaunching** a stream from a new session:
1. Start a fresh agent with [ENGINEERING-RULES.md](ENGINEERING-RULES.md) and its section of [briefs.md](briefs.md).
2. Tell it that its worktree already holds partial work to inspect and finish.
3. Before relaunching, check that no old process is still running in that worktree.

## Next steps, in order
1. **The Phase 0 gate: done.** For a single-shot check in the loop, use the commands in [02-gate.md](02-gate.md) (render with `--range a:b`, submit with `--video-start a`, `--strict`).
2. **Review and merge each wave-1 stream** as it finishes. Order: signals → routes → emission → characters → camera → water. For each:
   - read its report and diff, run its targeted tests, and check that the full CPU and GPU suite counts are clean;
   - resolve ADR number collisions (renumber the file, the README row and every `ADR-NNN` reference);
   - merge in the owner's main checkout, only after checking `git status` is clean and the branch is `main`: `git -C ~/Documents/GitHub/av-gen merge --no-ff agent/<t> -F <msgfile>` (`-F -` does not work with merge);
   - later streams: merge or rebase main into them before merging;
   - after each merge, merge main into `gv3/production`.
3. **Wave 2** ([briefs.md](briefs.md) § Wave 2):
   - **setpieces** (928–931) and **render** (917–919) were launched 2026-09-26 from main `0b623b88`, in `~/Documents/GitHub/av-gen-setpieces` and `av-gen-render` (branches `agent/setpieces`, `agent/render`).
   - **song** (920–923) launches after signals merges; **reactivity** (924–927) after signals, routes and emission merge. Create their worktrees from the main of that moment.
   - The launch prompt is the rules file plus the brief's section, plus: the other streams in flight, the ADR numbers, the Critic daemon on 8765 being the coordinator's (never restart it), and the final-report requirement.
4. **Phase 3, the GV3 revision** (after the Director streams merge). **The plan is [03-revision-plan.md](03-revision-plan.md)**: the drop without a style shift, five UFO events (E1–E5), the reactivity map, the animals' flat meadows, and a check per item. Work from `tools/make_glowmere_valley_3.py` + `tools/gv3/*`, iterating with the Critic on single-shot clips. It must cover everything in the brief:
   - audio reactivity at micro, meso and macro levels: hero effects on the heroes' `-under`/`-gills` nodes, the small mushrooms' per-layer lanes, travelling waves, wind, the macro arc;
   - water tears;
   - smooth cameras;
   - re-homed animals and retuned aliens;
   - several varied UFO events, with the riser still the centerpiece abduction;
   - pacing from the Director's durations;
   - a drop that changes the world's state, not its style;
   - the 4K offline configuration: `limits` "tier", `shadowCascades` 0 (4), `scene/shadowRange` 160 with 300 keyed on wides, `terrain.shadowDistance` 320, `terrainViewDistance` 1000, `terrainLod` false, `volumeMaxDistance` 220, `volumeSteps` 32, `horizonDensity` 1, rig `updateHz` 0, tone map AgX, bloom threshold 0.9 and knee 0.7, depth layers, grain 0.035. These are from reports/render-post.md; tune them with the evaluator.
   - Decide the world edge (the 640 m valley is visible in the wides) with the owner.
5. **Final 4K render**, the revision report (per the brief's DELIVERABLES) and the Director's self-critique (brief §18).

## Decisions, and why
- **Engine changes go to main first**, then GV3 continues from main. The owner authorized merging system changes into main (brief, System Development Gate). The owner's checkout is otherwise read-only for agents.
- **No compatibility shims** (ADR-441/442, and the owner's explicit `emissiveBoost` instruction). Scene looks may change; re-baseline with evidence.
- **Engine work runs in parallel with the evaluator build.** The gate protects GV3 scene edits only, and the owner was told this.
- **The first pass's cut was authored by a Python generator, not by the engine's Director.** Measured on GV3, the engine's own Director gave:
  - uniform 5.9 s shots;
  - cuts off the downbeat;
  - no detected pull-back, break, riser or drop.
- **Learned aesthetic models stay off.** The best reached 0.65 pairwise accuracy on GV3, and none beat chance on v1→v2.

## Known problems and limitations
- **The Critic's other limits:**
  - fast mode projects bounding boxes, so foliage and terrain can't occlude;
  - the generic per-shot reactivity grid is weak on short shots, so trust the route-locked checks;
  - some thresholds are tuned on GV3;
  - s16's `height_frac` reads 80, a projection artifact with the camera inside the bounds.
- **Engine defects found and assigned to the streams:**
  - the dead `emissiveBoost` (emission);
  - swallowed event routes (routes);
  - bar phase off by one beat, density always 0, sections not on the bus (signals);
  - cuts carry motion blur (camera);
  - `camera/focus/emphasis` and `aimFollowSmoothingMs` unreachable (camera and director);
  - `reactionProfile: organism` inert;
  - ColorCycling's default route is a phase-rate trap;
  - GV3's `"shoreFade"` key is parsed by nothing (its fix is `edgeFade`);
  - the dead arcs on `paintedGround2` and the firefly programs.
- **Seek ≠ play for one-frame renders:** no particle history, and the cast may stand differently. Judge particles and cast shots on continuous clips.
- **Staging values ignore project parameters.** Edit `tools/gv3/cast.py`.
- **Pixel-based fades** (water ripples, bloom levels, motion-blur tiles) differ between 540p previews and 1080p/4K finals. Check final-resolution stills before a final render.

## Owner requirements and creative direction (do not lose)
- **Audio reactivity is the highest priority.** It must be visible and meaningful, not just configured, and not "everything pulses to the beat". Hierarchical (micro/meso/macro), correlated but not identical.
- **Hero effects throughout,** with different timing and amplitude per hero. Use the small coloured mushrooms: waves, grouped or staggered responses, colour evolving by section.
- **Subtle continuous wind.**
- **Water tears** like the owner's reference: thin angular seams of compressed ripples. They must read, integrate with the style, not make the whole surface busy, and scale with distance.
- **Camera:** controlled cinematic movement, not procedural wobble.
- **Aliens are part of the world, not the whole video.** Never long idles, never walk → stop → 180° → back.
- **Animals** stay grounded on flat valley ground, oriented to it, and walk through curved turns.
- **More UFO/abduction events**, varied in location, framing, scale, timing, number of animals and relation to the music. The riser builds to the centerpiece.
- **No post-riser style shift.** The drop changes lighting, emission, atmosphere, activity and camera, while staying Glowmere.
- **Shots:** no global duration. Shorter where the music is dense; longer for contrast and scale.
- **4K, maximum offline quality,** with draw distance verified; cinematic Glowmere post, not a generic filter.
- **The Director should use the evaluator** in its loop and do the final self-critique.
- **Keep the first pass's good work.** The concept is that the valley's light is the protagonist. Keep:
  - the kick heartbeat on the elder's gold;
  - the horse glowing gold as it is lifted;
  - s14/s39 rhyming wides.
- **Never:**
  - commit the song or the licensed alien/farm assets;
  - modify `glowmere-valley-2-multicam`;
  - run the windowed app for automation (it rewrites the owner's prefs);
  - run `tools/make_abduction_scenario.py`.

  Review material goes in `~/Desktop/av-gen-review/`.

## Learned the hard way
- **The silent no-op family.** Parameters and routes bind with no warning and do nothing. Prove every modulation target on a difference image or an evaluator route-locked check.
- **Emission ownership.** A material program that writes emission owns it (ADR-179): drive `material/<program>/emissionIntensity`. Until the emission stream merges, `emissiveBoost` is dead on procedural nodes and multiplies zero on a glTF with no emissive factor. There, use an FXL Glow effect.
- **Critic usage.** Run `critic submit ... --wait --json` as a background command; the harness notifies on exit, so there's no polling. Use `critic shot <job> <sid>`, `critic findings --shot`, `critic frame --t --exact`, `critic compare`, and `critic session history <s> --track <t> --mode preview`.
- **Git and the shell.**
  - `git merge -F -` fails; use a file.
  - zsh doesn't word-split variables, and `echo ====` fails.
  - Catch2 filters are exact, and commas split them.
  - Exactly one expected FAILED line appears per clean CPU run (`test_character_lab_slopes.cpp:187`).
  - Run the GPU suite under `tools/gpu-lock.sh`, and never put a CPU-only run under the lock.
  - Link a new worktree's assets with main's `tools/link-worktree-assets.sh <wt>`. Configure with `-DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm`.
- **Cost of a full build:** about 51 CPU-minutes, roughly 5 minutes on an idle machine. The full CPU suite takes about 2 hours single-threaded under load.
- **Renders.** Use absolute `--render` paths; a relative one resolves against the project's folder. Previews are always 60 fps (entities step at 1/fps). A full 960×540 preview is about 20–23 minutes; the 1080p×2 final took 34 minutes.

## Where things are
- **Owner's brief and this record:** `~/Documents/GitHub/av-gen-gv3/docs/glowmere-valley-3/revision/`.
- **First pass record:** `docs/glowmere-valley-3/` 00–08. Its README has the iteration loop commands.
- **GV3 generator:** `tools/make_glowmere_valley_3.py` and `tools/gv3/` (music, directives, shots, look, cast, world, rig, ground, review, framing `--video`, cuts, shimmer).
- **Project:** `examples/world/glowmere-valley-3.{json,scene.json}`, generated; never hand-edited.
- **Evaluator:** `~/Documents/GitHub/creative-critic`. Its docs are README, INTEGRATION_GUIDE (§3 has the Phase 0 commands), DEVELOPMENT_PLAN and MODEL_BENCHMARKS; the AV Gen adapter is `adapters/avgen/avgen_adapter.py`. Daemons: 8765 is the coordinator's; 8791 and 8794 are the evaluator agent's test instances.
- **Review folder:** `~/Desktop/av-gen-review/18-glowmere-valley-3/`, containing:
  - the final 1080p film and the v2 preview;
  - sheets and the before/after sheet;
  - a record snapshot;
  - the water reference.
- **Memory:** `~/.claude/projects/-Users-natefaulkenberry-Documents-GitHub-av-gen/memory/av-gen-gv3-music-video.md`.
