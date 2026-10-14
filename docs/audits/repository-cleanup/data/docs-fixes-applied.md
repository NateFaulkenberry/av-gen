# Documentation fixes applied (branch `cleanup/docs-fixes`)

Applied on 2026-10-14 on top of `464995fe`. The inputs were `docs-audit-findings.md` (§8 items 7-17 and 19, and
C1-C10), `maintenance-conventions-draft.md` and `code-audit-findings.md`. Each claim was checked against the code before
it was written; where the code and the audit disagreed, the code won. Nothing was built and no test binary was run. The
only things executed were read-only checks, a link checker, and a throwaway CMake/Ninja project in the scratchpad (fix 11).

## Fixes

| # | Commit | File(s) | Before → after | Evidence checked |
|---|---|---|---|---|
| 1 | `ec9b6ab9` | `docs/build.md` | 4 presets and 5 options → adds the `tsan` and `ubsan` presets, notes that `relwithdebinfo` has no test preset, and adds `AVGEN_ENABLE_TSAN`, `AVGEN_ENABLE_UBSAN` (ignored under ASan), `AVGEN_ALLOC_COUNTERS`, `AVGEN_PATHTRACE_DENOISE`, `AVGEN_GPUWORLD_PROTOTYPE` and `AVGEN_ASTRAL_FORGE_PROTOTYPE`, each with its default and meaning | `CMakePresets.json` (configure/build/test preset names, cacheVariables), `CMakeLists.txt:24-36,57-66`, `cmake/Warnings.cmake:52-63` (UBSan is skipped when ASan is ON), `cmake/Dependencies.cmake:150-180` (OIDN 2.3.3 arm64 prebuilt, 51 MB, reports itself unavailable), `src/CMakeLists.txt:63` |
| 2 | `2496f641` | `README.md` | "Python 3 (only for the test-audio generator)" → `python3` with numpy via `tools/ci/requirements.txt` (some tools also need Pillow or Blender). "Ten example worlds" → points to `examples/index.json` and File > Examples / File > Live Projects / `--example`. "`--help` lists every flag" → "lists the flags" (C7: `--live-profile` and `--trace-jumps` are missing from `usageText()`) | `tools/ci/requirements.txt`; `tools/ci/run-suite.sh` (runs `generate-audio.sh`); `tests/integration/test_rift.cpp:56`; `src/app/examples.cpp:89` (reads `index.json`); `src/ui/control_panel.cpp:245,299-300` (File menu, `Live Projects`, `Examples`); Pillow imports in `tools/contact_sheet.py` and others |
| 3 | `24105e39` | `docs/dependencies.md` | meshoptimizer and OIDN listed as "Planned" → table rows: meshoptimizer v1.2 (MIT, ADR-078, `src/assets/mesh_lod.cpp`), and OIDN v2.3.3 (Apache-2.0, bundled oneTBB Apache-2.0, optional, ADR-353). Both are removed from "Planned", and a line on Python tooling is added. The 100STYLE credit line that `test_repo_hygiene` requires is unchanged (1 occurrence) | `cmake/Dependencies.cmake:74-83,159-180`; `ADR-353` lines 84-85 (licence verified from the fetched `LICENSE.txt`); `grep meshoptimizer.h src` → only `mesh_lod.cpp` (the audit also named `procedural.cpp`; it has no include, so it is not cited); `tests/unit/test_repo_hygiene.cpp:194-217` |
| 4 | `0f888140` | `docs/engineering-labs.md` | `src/world/effects.hpp` → `src/world/effects/`; the `avgen --lab-case <lab>:<n> --print` line is removed | `ls src/world/effects/`; `--print` appears nowhere in `src/app/application.cpp`, while `--lab-case` is present |
| 5 | `e1540c45` | `docs/engineering-labs.md` | item 8, "`indtune.cpp` … is a 0-byte file" → struck through, "Removed, 2026-10-14"; the `avgen_bench_flatten` half stays open | **Depends on the code-cleanup branch's `git rm indtune.cpp`.** If that branch does not merge, drop this commit. `tools/CMakeLists.txt:96` still builds `avgen_bench_flatten` |
| 6 | `44d225ee` | `docs/pathtrace/overview.md` | `shaders/skinning.wgsl` → `shaders/pbr_skinned.wgsl` | `skinning.wgsl` does not exist. `pbr_skinned.wgsl` has `skinMatrix` (four-influence linear blend) |
| 6 | `44d225ee` | `docs/director-poc-plan.md` | "`src/app/placement.*`, `src/scene/placement`, `ObstacleField`" → the nonexistent `src/scene/placement` is removed (`src/app/placement.*` was already listed) | `src/app/placement.{hpp,cpp}` exist and `src/scene/placement*` does not. `ObstacleField` is in `src/entity/` |
| 6 | `44d225ee` | `docs/live-quality/PROGRESS.md` | adds a dated note under the header. `docs/live-quality/REPORT.md` was never committed. The in-repo report is `docs/investigations/live-projection-2026-10-02.md`, a byte-identical copy of `evidence-live-projection-2026-10-02.md`. The decisions are ADR-1080..1089, and the two temporary commits were reverted. The original log text is untouched | `diff -q` of the two reports (identical); `ls docs/decisions/ADR-108*`; `AVGEN_LIVE_FRAME_CSV` and `AVGEN_X_PROJECT_ANY` are absent from `src/`; PROGRESS.md:62-63 |
| 7 | `7fa73d66` | `docs/prototypes/sonic-garden/SCENE-CATALOG.md`, `PROGRESS-vfx-art.md` | 17 `` `scenes/*.py `` → `` `tools/sonic_vfx/scenes/*.py `` | the script asserted that each rewritten path exists. 0 `` `scenes/ `` references remain |
| 7 | `7fa73d66` | `docs/prototypes/liminal-space/00-brief.md` | Notes: `PROGRESS.md` → `PROGRESS-eng.md` and `PROGRESS-art.md` | `ls docs/prototypes/liminal-space/` |
| 8 | `be0bb8d9` | `assets/farm.manifest.json` | licence "Not recorded…" → "Purchased, commercially licensed. NOT for distribution. Confirmed by the project owner on 2026-09-28 (assets/farm/ATTRIBUTION.md). Do not commit, cache or upload the meshes: this repository is public." | `assets/farm/ATTRIBUTION.md`. Nothing generates this file: `make_farm_animals.sh` does not write it (`git grep`). `asset_library.cpp:236` only stores the string, and `test_farm_animals.cpp` checks only that it is non-empty. `python3 -m json.tool` passes |
| 9 | `7b2f2765` | `docs/decisions/README.md` | the range table ended "701+ unassigned" → rows for the blocks actually used: 701-979 (used 701-719, 750-770, 800, 820-835, 870, 880, 890-952), 1110-1113, 1114-1115, 1116-1124, 1160-1168 (1167 was reverted), 1180-1182, 1200-1219 (1200-1202 on main), 1220-1239 (1221-1222 on main). Unassigned now starts at **1300+** | ADR numbers taken from the filenames (691 files, max 1292). The audit's "1200-1239 used" was wrong: only 1200-1202 and 1221-1222 exist. Holders come from each range's first commit (`git log --diff-filter=A`). The 1200-1219 block is confirmed by `docs/prototypes/bioluminescent/PROGRESS.md:13`. The 1167 revert is commit `dd0095b9`. Index parity was re-checked: 691 rows = 691 files |
| 10 | `e2d3e269` | `docs/help/reference-command-line.md` | summary "Every flag, every AVGEN environment variable…" → "The common flags, the common AVGEN environment variables, and the exit codes. avgen --help lists the rest." Adds an intro line saying the page is not complete | audit C4 (73 of 114 flags missing). `test_help` has no summary-length rule and no CLI check |
| 11 | `3701e65e` | `docs/testing.md` entry 1 | "The test glob is *configure-time*… Always `cmake -S . -B build/release` after a merge" → the globs use `CONFIGURE_DEPENDS`, and with Ninja every build re-checks them, even a one-target build. The 2026-09-18 incident happened after that landed, so its mechanism is unknown. The guard is the **case count**: when the counts disagree, reconfigure, rebuild and rerun | `tests/CMakeLists.txt:3,146,149` (`CONFIGURE_DEPENDS`, added `8483030b` on 2026-09-09; the incident was 2026-09-18). All presets use `"generator": "Ninja"`. **Tested** with a throwaway project in the scratchpad (CMake 4.0.1, Ninja 1.13.2): adding a `.cpp` and then running `cmake --build b --target app`, or `ninja app`, printed "GLOB mismatch! … Re-running CMake" and compiled the new file |
| 12 | `8615d5c3` | `docs/development/ci.md` | new **"Generated inputs"** section: the `test-inputs` action (numpy venv first on PATH; `avgen_world_preview` copied to `build/release/tools/` only when `bin:` is passed, so the Rift cases SKIP in the sanitizer jobs); a table of the generated inputs (2 stdlib scores, 7 sonic-* scores from one run of `make_sonic_material.py`, one run per distinct command, sha256-checked; `rift.scene.json` from `test_rift` under a file lock, SKIP with the remedy, FAIL if `build.py` fails); and "Rebuild" at `assets/audio/rebuild.mp3` (gitignored, `$AVGEN_REBUILD_AUDIO`, on no runner, cases SKIP, including the three seek-parity cases since `ed9926db`). Also fixed: "two repository scores" → nine; the Build job also packages `avgen_world_preview`; the coverage list `~/Desktop/*.mp3` → `assets/audio/rebuild.mp3` and `trench.wav`; `assets/audio/*.wav` is no longer listed as missing; the Caching note no longer blames a configure-time glob; the song path in follow-up 2; the Files list adds the action and `requirements.txt` | `.github/actions/test-inputs/action.yml`; `ci.yml` (build packaging, `test-inputs` with `bin:` in `cpu-tests`/`cpu-assets`); `sanitizers.yml` (`test-inputs` without `bin:` in asan-tests, ubsan, tsan); `tools/ci/generate-audio.sh`; `assets/audio/manifest.json` (9 tracks); `tests/integration/test_rift.cpp:41-78`; `examples/bioluminescent/land.py:22`; `tests/support/rebuild_track.hpp`; `.gitignore:57-60`; commits `ed9926db` and `464995fe` |
| 13 | `b0cecaef` | `docs/development/maintenance-conventions.md` (new), `README.md` | about one page, from the draft: current reference vs records, canonical sources, where things go (ADRs with ranges and index, dated investigations, `docs/prototypes/<name>/` with README/PAUSED, session state not in git, review media in `~/Desktop/av-gen-review/`), `examples/index.json` as the catalogue, generators and regenerate commands, and how an experiment ends. The draft's proposed `tools/check_generated.sh` and the `docs/archive/` move are **not** stated as conventions, because neither exists. It is linked from the README docs list, together with `ci.md` | every tool path cited was `ls`-checked (`refresh_scene_fingerprint.py`, `check_project_integrity.py`, three `build.py`, `link-worktree-assets.sh`) |
| 14 | `bebc6449` | `README.md` | "development-log.md: what was built, tested, and measured" → adds "up to 2026-09-11 (ADR-097); later work is recorded in the ADRs" | the last `## ` entry in `docs/development-log.md` is 2026-09-11 / ADR-097 |

## Checked and left alone

- `docs/tree-of-life-cosmic-plan.md:40`: the line already reads "There is no `docs/adr` directory; ADRs live in
  `docs/decisions/`". The audit's hit was a false positive, so no edit was made.
- `docs/help/reference-command-line.md` body: no rows were added for the 73 missing flags. That needs a decision
  (complete the page, or add a test against `usageText()`).

## Link and path check

A checker in the scratchpad looked at every `.md` this branch changed (15 files). It checked relative Markdown
links and backticked paths under `src/ tools/ examples/ shaders/ docs/ tests/ cmake/ assets/ prototypes/ .github/
scenes/`, with `:line` suffixes stripped, and counted a path as present if it is tracked, a tracked directory, or on
disk. **1,103 references, 6 unresolved, all of them expected:**

- `docs/development/ci.md`: `examples/bioluminescent/rift.scene.json`, gitignored and generated by `test_rift`
  (that is the point of the sentence).
- `docs/engineering-labs.md:321,324,334`: `file.cpp:Symbol` suffixes the checker does not strip. All three files exist.
- `docs/live-quality/PROGRESS.md:4,6`: `docs/live-quality/REPORT.md`, once in the original log text (historical) and
  once in the new note that says it was never committed.

The `#generated-inputs` anchors resolve to `## Generated inputs`. Also re-checked: the ADR index has 691 rows for 691
files and the sets are equal; the 100STYLE credit line is still present in `docs/dependencies.md`;
`assets/farm.manifest.json` parses; no conflict markers.

## Not done, and why

- **`docs/architecture.md`** (C3: 16 of 32 modules) is NEEDS OWNER: either a rewrite or a historical banner. No
  banner was added because nothing supersedes it.
- **Historical banners** (task item 5): none added. No obsolete current-reference doc has a supersessor I could name
  with certainty. `upgrade-status.md`, `TODO-glowmere-handoff.md` and `STATUS-2026-09-25.md` are already dated or
  self-declared parked, and the audit routes them to an owner-decided archive move.
- **README "Status" paragraph** (milestones 0.1 to 1.2…) is stale but not in the SAFE list. It was left for the owner.
- **Inconsistency for the coordinator:** the header comment in `tools/ci/test-assets.list` says the song is "Not
  included, deliberately … (the owner's decision)". `docs/development/gpu-ci-private-assets.md:202` and `ci.md`
  follow-up 2 say the owner DECIDED it may go into the private repository and that adding it is future work. The
  two agree that it is not there today. They disagree on whether that is deliberate. `tools/ci/` is outside this branch.
- Fix 5 (`e1540c45`) assumes the code branch removes `indtune.cpp`.
- The owner items were not touched: the treeisland PNGs, `05-static-reference-vs-f027-full.png`, top-level scratch
  docs, and `examples/index.json`.
