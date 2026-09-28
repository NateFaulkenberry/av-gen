# Engineering rules for the GV3 revision's engine agents

You are one of several engineering agents working in parallel on AV Gen: C++23, Dawn/WebGPU, CMake + Ninja, Catch2. The work supports the revision of the music video "Glowmere Valley 3" (GV3).

**Read first:**
- The owner's revision brief: `/Users/natefaulkenberry/Documents/GitHub/av-gen-gv3/docs/glowmere-valley-3/revision/00-brief.md`. Read the parts your brief names.
- The read-only audit report(s) your brief names, in `/Users/natefaulkenberry/Documents/GitHub/av-gen-gv3/docs/glowmere-valley-3/revision/audit/reports/`. They carry file:line evidence. Those line numbers are against main 83a12334 plus ADR-893–895, which your worktree already has, so they may be slightly off.

## Your worktree
- Work only in the worktree your brief names. It is a git worktree on branch `agent/<topic>`, created from the main commit your brief names (wave 1 used `0b623b88`).
- Its assets (gitignored `.glb`, `.wav`, `.hdr`, textures) are already symlinked. Never commit them; check `git -C <wt> status` before every commit.
- **Build:**
  ```
  cmake -S <wt> -B <wt>/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm
  cmake --build <wt>/build/release -j 6
  ```
  Other agents build at the same time on a 12-core machine, so keep `-j` at 6 or less.
- **Binaries:**
  - `build/release/src/avgen` (the app, with `--project`, `--render`, `--range`, `--size`);
  - `build/release/tests/avgen_tests` (the CPU suite);
  - `build/release/tests/avgen_render_tests` (the GPU suite; it may need `--target avgen_render_tests`);
  - tools in `build/release/tools/`.
- Use absolute paths. The shell's working directory resets between commands.

## Hard rules
1. **Never touch the main checkout** `/Users/natefaulkenberry/Documents/GitHub/av-gen`, the GV3 worktree `av-gen-gv3`, or any other agent's worktree.
   - Run state-changing git only as `git -C <your worktree> ...`.
   - Commit on your branch only, in small logical commits. End every message with the line `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
   - Do not merge, rebase onto another branch, or push. The coordinator merges.
2. **One GPU, shared.**
   - Run anything that uses the GPU (`avgen_render_tests`, renders, GPU tools) through `/Users/natefaulkenberry/Documents/GitHub/<your worktree>/tools/gpu-lock.sh <command...>`.
   - Never put a CPU-only run under the lock. The lock serialises GPU work; with several agents, locking
     CPU suites too would stall everyone for hours. `docs/testing.md` #29 is still right that a CPU suite
     is contention for a GPU bit-identity check. So a GPU suite you offer as evidence runs when no CPU
     suite is running (check `ps` first), and your report says whether it did. (Settled 2026-09-27.)
   - Never kill processes you did not start. `pkill -f avgen_tests` kills other agents' suites.
   - Treat GPU timing failures as contention until you prove otherwise by re-running.
   - Shaders load from the source tree at runtime, so a `.wgsl` edit affects only your own worktree's binaries.
3. **No compatibility shims (ADR-441/442).**
   - The owner accepts intentional changes to existing scenes' looks and behaviour. Make the change cleanly, delete dead code, and re-baseline affected tests with evidence (show why the new value is right).
   - Record every existing scene or behaviour that changes in your ADR's Consequences.
4. **The silent no-op family.** This codebase keeps producing parameters, routes and settings that bind with no warning and never reach the output (e.g. `nodes/<procedural>/emissiveBoost`, event routes whose attack swallows the event).
   - Every new parameter, behaviour or setting you add needs a test proving it reaches the output: a GPU difference image for anything visual, a measured trajectory or signal value for simulation.
   - Add a control arm that fails without your change.
   - Registered-but-inert parameters are defects, not features.
5. **Determinism.** A seek must land on the same frame as play (ADR-089/091; checkpoints ADR-700; HIST ADR-703). Anything stateful must be checkpointed, replayed, or a pure function of time. Prefer pure functions of time.
6. **Catch2 and zsh traps.**
   - A test filter is an exact match: use a trailing `*`. A comma splits a filter in two. "No tests ran" exits 0. Always check that the assertion count moved.
   - zsh does not word-split unquoted variables, and `echo ====` fails (`=` expansion).
   - macOS has no `timeout`.
   - Exactly one FAILED line is expected on a clean full CPU run: `test_character_lab_slopes.cpp:187`, a shouldfail case.
   - An incremental build does not see newly added test files until CMake reconfigures. The tests use GLOB with CONFIGURE_DEPENDS; if in doubt, re-run the cmake configure step.
7. **ADRs.**
   - Write one per significant decision in `docs/decisions/` and add a row to `docs/decisions/README.md`.
   - Follow the house style of ADR-893, 894 and 895: title; Status/Date/Follows/Implemented by/Tests; Context; Decision; Consequences.
   - Use only the ADR numbers your brief gives you; the coordinator may renumber them at merge. Use today's date, 2026-09-26.
8. **Scope discipline.**
   - Build what your brief lists, well, with tests. Do not start unrelated refactors.
   - If you find a defect outside your scope, write it down in your report; don't fix it.
   - Do not edit GV3's own files (`tools/gv3/`, `tools/make_glowmere_valley_3.py`, `examples/world/glowmere-valley-3.*`, `docs/glowmere-valley-3/`). The coordinator applies features to GV3 in the revision phase.

## UI reach (the owner's standing rule)
Anything your work makes visible in the picture must be findable and adjustable in the app's UI,
under a name that describes what the viewer sees. A control that exists but that nobody can connect
to what they are looking at counts as a defect.
- **Parameters show up automatically.** Every exposed parameter appears in the Parameters panel and the World panel's Inspector, grouped by its path (ADR-387, `ui::parameterSubGroup`). So:
  - choose paths whose middle segments name the visual thing (for example `.../water/tears/...` reads as "tears");
  - set an explicit `label` where the leaf is cryptic;
  - keep `flags.exposed` on.
- **Anything that is not a parameter** (a template, a plan item, an effect) needs a named home in an existing panel. Say where it is edited.
- **Never run the windowed app or `--capture-ui` to check this.** The windowed app rewrites the owner's preferences. Verify from the grouping code, or with a CPU test on the registry.
- **In your report,** list each new control and where an artist finds it (panel → group → sub-group).

## Shared folders
Agents share the coordinator's scratchpad folder. Give every log and output file there a name that
starts with your stream (`render-gpu-full.log`, not `gpu-full.log`); another agent's run overwrote a
generic name once. Keep scratch copies of GV3 under your worktree's `build/`, never in `examples/`.

## Before you finish
- Run your targeted tests.
- Run the full CPU suite (`<wt>/build/release/tests/avgen_tests`, or `ctest --test-dir <wt>/build/release -j 4`) and record exact counts.
- If you touched rendering or shaders, run the full GPU suite under the lock.
- Report honestly. If something fails, say so, with the output and your diagnosis.

## Final report (your last message)
- What you built, with the main files.
- Where each new visible control lives in the UI (see "UI reach").
- Each ADR (number and title).
- Tests added, with their controls.
- Suite results with exact counts, and the exit codes.
- Every change to existing scenes' looks or behaviour, and how you verified it.
- Defects you found but did not fix.
- **How GV3 should use your work:** parameter names, JSON keys, recommended values, and anything that differs between the 960×540 previews and the 1080p/4K finals.
- Your branch name and final commit hash.
