# GV3 revision: progress and state

This is the operational state file. Update it whenever the state changes. It was last updated
2026-09-26, after the gate passed, while wave-1 engine agents were running.

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

State when this file was saved (all were running as background agents of the session that wrote this):

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
