# W3 notes: repository and project cleanup (Agent 3, branch `qa/clean`)

Worktree `/Users/natefaulkenberry/Documents/GitHub/av-gen-qa-clean`. A successor resumes from "State".

## State (2026-09-28)
- [x] T1 GV3 r7b integrated (`ad1a9fa4`)
- [x] T2 Glowmere family / examples audit; stale arms removed, fingerprints refreshed (`bc2c1939`)
- [x] T3 `docs/qa-pass/gv3-state-audit.md` written for W1
- [x] T4 three engine defects fixed with tests (`9cdb68be`)
- [x] T5 hygiene findings and worktree list (below)
- Open: the "list only" items below need an owner decision or another workstream.

## T1: how GV3 r7b is reproduced
- The r7b the owner reviewed (`~/Desktop/av-gen-review/18-glowmere-valley-3/revision/integrate/project-for-review/`)
  is gv3-int's `build/gv3/int/r7b/examples/world/` snapshot, made by gv3-int's `round.sh r7b`:
  the plain generator (preview mode, **not** `--final`), `ufo.py --no-trace`, then `snap.py`, which
  only rewrites the song path to absolute.
- Main's generator reproduces it: `python3 tools/make_glowmere_valley_3.py && python3 tools/gv3/ufo.py --no-trace`
  (needs `avgen_world_preview` and `avgen_cast_trace` built). Scene byte-identical; project identical except
  `assets.audio.path` (`../../../../../Desktop/Rebuild.mp3`, the family convention, vs `/Users/<owner>/Desktop/...`).
  The regenerated `03-directives.md` and `04-shot-plan.md` equal r7b's snapshot. Generator log equals gen-r7b.log.
- Semantic diff repo vs Desktop copy: project 1 difference (`/assets/audio/path`), scene 0.
- Verification, `avgen --project examples/world/glowmere-valley-3.json --audit-routes`: rc 0, 0 errors,
  routes 105/105, tracks 130/130, effect-default routes 23/23, effects 18/18 live; 73 shots, 225.5 s. The audit
  JSON equals the Desktop bundle's (minus the path field).
- Added to `examples/index.json` (Showcase, "Glowmere Valley 3"); the index test loads it (passes).
- Not included, and why (also in `docs/glowmere-valley-3/README.md`): the song (copyrighted; relative Desktop
  path, tests use `$AVGEN_REBUILD_AUDIO` or `~/Desktop/Rebuild.mp3` and SKIP); the purchased aliens
  (`assets/aliens/*.glb`) and farm animals (`assets/farm/*.glb`); the CC0 quaternius pack (gitignored bulk
  download); the Desktop `bundle/` (its `assets/` holds copies of the licensed files); the r7b cast trace
  (6.4 MB build artifact, only for `--final`, regenerable by `avgen_cast_trace` in ~17 min CPU).
- Desktop: no other Glowmere project data (the behave/navfix/characters `*.quality.json` are measurements).

## T2: examples audit
Removed (`bc2c1939`, recover with `git show 77ea4247:<path>`):
- `examples/world/_diag-water-*.json` (9) and `_pre-defects{,.scene}.json`: no test, tool, index or example
  names them (git grep); only ADRs/progress notes; no generator; the nine carried stale fingerprints; ten
  ADRs (896..945) had to migrate them. `docs/abduction-authoring.md`'s mention updated.
- Fingerprints refreshed: `examples/effects/ufo-stack.json`, `glowmere-valley-2-song.json`.
  `check_project_integrity.py`: 20 problems -> 9.

Kept, with reason:
- `_tier1`, `_tier1big` (ADR-056 A/B arms; "promoting them is a one-line change"), `_skyonly` (the calibration
  scene `docs/performance.md` measures against).
- `tractor-beam-lab-legacy.scene.json`: `test_beam_lab` loads it (ADR-262's "before" arm).
- `examples/world/renders/`: holds only its own `.gitignore` (`*`).
- `examples/world/assemblies/elder.scene.json`: live.

Listed, not changed (decision or other owner):
- `examples/treeisland/_vx2-*.json` (9): the 9 remaining integrity problems (stale fingerprint). Their
  generator `tools/make_vortex2_arms.py` no longer runs against the deliverable ("no vortex object to set
  'innerVoid'"). Candidates for removal plus a `.gitignore` rule like `_fog-*`.
- `examples/treeisland/` holds ~40 other `_ck-*`, `_ca-vg-*`, `_ctl-*`, `_glow-*`, `_view-*`, `_compare-*`
  arms; most have a generator in tools/ (`make_cosmickey_arms.py`, `make_treeisland_arms.py`); `_ctl-no-*`
  is read by `test_treeisland_example`. Not audited further.
- `examples/treeisland/renders/`: 28 tracked PNGs (51 MB) under a path `.gitignore` excludes (committed before
  the rule, ADR-450 `dab170b1`); ADR-372/393 cite them. Removing them from the tip does not shrink history.
- `renders/output-preview/*`, `renders/treeisland/*`: 7 small tracked text/json files under the ignored
  `/renders/` (force-added queue files, per the .gitignore's stated policy: fine).
- Glowmere family loads (`--audit-routes`, all rc 0, 0 errors): GV2 58/58 routes, multicam 64/64, ufo-stack
  71/71, lyrics 39/39. Intended-dead: GV2-song 4 effects (hero pulses with no spotlighting span),
  stylized-pbr 19 routes and terrain 2 (no audio by design), atmospherics 17 of 18 effects parked at
  `windowStart 3000 s`. Dead data: "unknown parameter" lines, 9 distinct in multicam/ufo-stack
  (`material/glowmereTissue/op/9/*`, `material/glowmereFireflies*/emissionIntensity`), 7 in GV2 and GV2-song,
  1 in stylized/lyrics/atmospherics (`material/paintedGround*/emissionIntensity`); `lod.lodCount` on three
  procedural nodes in every Glowmere scene. Removing them needs a same-load + frame-hash proof (GPU).

## T3: see `gv3-state-audit.md`

## T4: engine defects (`9cdb68be`)
1. Scene failure: `Engine::projectSceneError()`; `Application::rememberProject` logs an error and puts
   "The project's scene did not load (what is showing is not it): <cause>" in the status bar ahead of the
   warning count. Reproduced CPU-side: the Desktop unbundled copies from a temp folder load with rc 0 and
   routes 19/81 live, the scene failing on `../lightrigs/...`/`../entities/...`. Test: "A project whose scene
   fails to load says so apart from its warnings". The UI text itself is unverified (no screenshot).
2. `--export-bundle` + top-level `lightRig`: `bundleScene` now rewrites both locations. Test "Bundles carry a
   scene's top-level light rig" (3 assertions fail without the fix).
3. `--export-bundle` implied no `--headless`: `src/app/cli_batch.hpp` lists the batch flags; `parseArgs`
   sets headless for any of them. Test `[app][cli]`. Not reproduced by launching the app.

## T5: worktrees (from `git worktree list`; none removed)
All 17 have a clean tree (0 tracked changes, 0 untracked).
- HEAD already in main, safe to remove: behave, cihealth, ecolight, engine-3, engine-4, engine-5, engine-6,
  gv3-cast, gv3-cut, gv3-int, gv3-look, herofocus, navfix, signals (integrate/revision = main), uireach.
- NOT in main: `av-gen-gv3` (gv3/production, 2 progress-note commits, 1 file: PROGRESS.md) and
  `av-gen-gv3-world` (gv3/world, 1 commit, 1 line in its log). Merge or accept losing them first.
- Caution: removing a worktree deletes its gitignored `build/` (1-3 GB each). gv3-int's `build/gv3/int/` holds
  every review snapshot (r1..r7b), the r7b cast trace and the audit/quality outputs; gv3 2.1 GB, gv3-world
  2.7 GB. Copy anything wanted before removing.
