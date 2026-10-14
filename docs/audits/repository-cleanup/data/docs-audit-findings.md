# Documentation audit: findings

Audited 2026-10-14 on `cleanup/repo-consolidation`. HEAD moved during the audit, from `7b83fd58` to `ed9926db`,
because the coordinator committed three docs/CI changes. The path checker and the ADR check were re-run on
`ed9926db`. Nothing was built and no test binary was run. The only things run were read-only checks,
plus a set of generators executed in a throwaway `git archive` copy in the scratchpad (see
`artifacts-inventory.md`).

Scale: 1,152 tracked `.md` files, of which 692 are under `docs/decisions/` (691 ADRs and the index). There
are 4,324 tracked files, about 181 MB, 100.6 MB of it binary.

Classes used below:
- **current**: authoritative, current reference.
- **historical**: an ADR, a dated report or a prototype log. These are records and should not be "fixed".
- **stale**: contradicts the code.
- **redundant**
- **scratch**: agent working notes, briefs, handoffs, PROGRESS/RESUME/TODO files.
- **unclear**

---

## 1. Inventory

### Root and repo-level

| Doc / group | Class | Notes |
|---|---|---|
| `README.md` | current, partly **stale** | The run examples check out: every flag exists, `prores422` is valid, `--example "The Infinite Temple"` is in the index, and `shaders/examples/feedback.wgsl` exists. Stale: "Ten example worlds ship as data" (`examples/index.json` has **84** entries). "Python 3 (only for the test-audio generator)" is wrong: the CPU suite now needs `python3` plus **numpy** (`tools/ci/requirements.txt`, `generate-audio.sh`, `test_rift`). "Status: milestones 0.1 to 1.2…" predates everything since 2026-09-09. The doc index omits testing labs, water, volumetrics, the engineering labs, camera docs and the help system. |
| `CREDITS.md` | current | Consistent with `docs/dependencies.md` and enforced by `test_repo_hygiene`. |
| `LICENSE` | current | MIT. |
| `.agent/` (5 files, last touched 2026-09-19) | **scratch**, stale | `PROJECT_STATE.md` says "main @ 598082c1, 2,459 cases". `CURRENT_TASK.md` lists the agent tracks of 2026-09-19. Nothing references the directory. |
| `assets/*/ATTRIBUTION.md`, `assets/100STYLE-ATTRIBUTION.md` | current | Licence records. `assets/farm.manifest.json` contradicts the farm one (§5). |
| `examples/weather/README.md`, `docs/prototypes/*/README.md` | current for their scope | |
| `tools/` (132 entries) | no README | Nothing indexes the ~60 `make_*` generators or the probes. Docstrings are good; an index is missing. |

### `docs/` top level (94 files)

| Doc / group | Class | Notes |
|---|---|---|
| `build.md` | current, partly **stale** | See §3 (presets and options are missing). |
| `testing.md` | current | Its CI commands match `tools/ci/run-suite.sh`. One claim needs checking (§3, C6). |
| `architecture.md` | **stale** | The module table and the §8 directory layout list 16 modules. `src/` has **32** (§3, C3). |
| `audio.md`, `rendering.md`, `lighting.md`, `shaders.md`, `assets.md`, `asset-library.md`, `project-format.md`, `authoring.md`, `composition.md`, `output-preview.md`, `control.md`, `procedural-*.md`, `spatial-data.md`, `gpu-fields.md`, `splines.md`, `sdf.md`, `grammar-and-hierarchy.md`, `gpu-culling-lod.md`, `scene-states-and-macros.md`, `volumetrics-and-simulation.md`, `water.md`, `world.md`, `world-editor.md`, `sequencer.md`, `song-*.md`, `section-shot-language.md`, `camera-presets.md`, `camera-lab.md`, `auto-director.md`, `abduction-authoring.md`, `image-formation.md`, `performance.md`, `application-performance.md`, `engineering-labs.md`, `renderer-limits.md`, `dependencies.md`, `visual-quality.md` | current | User guides and subsystem references. Broken paths are listed in §2. `engineering-labs.md:447` documents a flag that does not exist (§3, C5). `dependencies.md` is out of date on two dependencies (§5). |
| `development-log.md` | historical | Last entry 2026-09-16. README still presents it as "what was built". |
| `TODO-glowmere-handoff.md` | **scratch** (parked 2026-09-10) | Handoff checklist, self-declared PARKED. |
| `tornado-handoff.md` | historical (handoff) | README links it as current. |
| `upgrade-status.md` (2026-09-10, "976 tests"), `integration-map.md`, `cinematic-world-gap-analysis.md`, `glowmere-world-builder-audit.md`, `glowmere-washout-findings.md`, `post-artifact-forensics.md`, `renderer-forensics-plan.md`/`-report.md`, `renderer-pre-upgrade-baseline.md`, `renderer-qa-2026-09-11.md`, `renderer-2-*.md` (4), `offline-backend-audit.md`, `image-look-audit.md`, `lights-and-viewport-authoring-audit.md`, `metal-rt-decision.md`, `world-performance.md` | historical | Dated audits and reports. Eight top-level docs have **zero** inbound references: `bevels-and-hero-objects`, `constellation`, `engine-brief`, `glowmere-washout-findings`, `integration-map`, `sequencer-audio-structure-plan`, `tree-of-life-cosmic-plan`, `upgrade-status`. |
| `*-spec.md` (`sequencer-spec`, `ai-control-plane-spec`, `help-system-spec`, `world-authoring-spec`, `image-look-spec`), `*-plan.md` (`character-ai-plan`, `director-poc-plan`, `sequencer-audio-structure-plan`, `tree-of-life-cosmic-plan`), `group-d-city-brief.md`, `engine-brief.md` | **scratch/historical** (owner briefs and agent plans) | They are inputs, not references. Several cite files that were never created or were later renamed (§2). |
| `shot-*.md` (5), `constellation.md`, `stylized-glowmere.md`, `glowmere-asset-families.md`, `glowmere-audio.md`, `tractor-beam-lab.md`, `bevels-and-hero-objects.md` | historical / per-world notes | Tied to example worlds. Keep them, but they are not engine reference. |
| `character-ai-research.md`, `character-animation-lab.md`, `character-intelligence-lab.md`, `ai-control-plane.md`, `ai-tool-backlog.md`, `art-direction.md` | current-ish / unclear | Mixed research and reference. `ai-tool-backlog.md` is a backlog (scratch). |

### `docs/` subdirectories

| Group | Files | Class | Notes |
|---|---|---|---|
| `decisions/` | 691 ADRs + README | historical (immutable) + current index | Index is complete (§4). 134 path references in ADRs point at moved or removed code. That is expected for immutable records, so do not edit them. |
| `help/` (47 md + `features.json`) | current, **shipped content** | Loaded at runtime (`src/help/database.cpp`) and test-enforced (§6). `reference-command-line.md` claims "every flag" but omits 73 of 114 (§3, C4). |
| `development/ci.md`, `development/gpu-ci-private-assets.md` | current | Match `ci.yml`, `sanitizers.yml` and `run-suite.sh`. The `gpu.yml` "broken path" is in the private repository: a false positive. |
| `development/STATUS-2026-09-25.md`, `director-system-progress.md`, `motion-stack-progress.md`, `gpu-sim-seek-investigation.md`, `render-design-notes.md`, `setpieces-design-notes.md` | scratch / historical | Handoff index for the 2026-09-25 pause. Its "local main ~100 commits ahead" is long obsolete. |
| `architecture/transport-audit.md` | historical | A single file in its own directory. |
| `quality-lab/` (9), `hdr-lab/`, `lighting-lab/`, `lod-lab/`, `particle-lab/`, `shadow-lab/`, `visibility-lab/`, `volumetric-lab/` (one README each) | current | Lab references. The `examples/quality/aliasing(-dolly)` references are stems of `.json`/`.scene.json` files: soft false positives. |
| `visual-cookbook/` (6) | current (art guidance) | |
| `performance/` (5), `measurements/` (1 md + 3 json) | historical | `measurements/` is the ADR-450 Cosmic Ocean data. ADR-441 removed that feature. |
| `investigations/` (8) | historical | Dated. Two came in today (`631d9c2f`). |
| `reports/` (4), `qa-pass/` (9), `renderer-upgrade/` (12), `renderer-level3/` (1), `pathtrace/` (2 md + 10 png + spike) | historical | `renderer-upgrade/` has four files numbered `08-*`. |
| `live-optimizer/` (6), `live-quality/` (4) | scratch + historical | Briefs, PROGRESS and MERGE-NOTES. `live-quality/PROGRESS.md` still lists "temporary commits (must be reverted before merge)" and a `REPORT.md` that does not exist. The report landed as `docs/investigations/live-projection-2026-10-02.md`. |
| `design/` (36 md) | historical / unclear | Character-animation and director design. `director-agent-feasibility-report.md` cites `src/direct/` and `src/world/effects.hpp`, which do not exist. |
| `research/` (38 md + ~70 data files under `gpu-world/`) | historical | Phase-0 research. Its paths to *external* repositories (`src/renderer_mtl.cpp`, `docs/compat.md`, …) are not ours: false positives. `gpu-world-productionization.md` cites `examples/phonotaxis/`, which the owner dropped. |
| `glowmere-valley-2/` (16), `glowmere-valley-3/` (75 incl. `revision/`, `art-pass/`) | historical + scratch (PROGRESS, briefs) | Project logs. |
| `prototypes/` (9 projects, 110 files: astral-forge, astronaut-musicians, bioluminescent, bioluminescent-valley, chorus-field, digital-mosh, liminal-space, procedural-space, sonic-garden) | scratch + historical | Briefs (`00-brief`, `0N-brief-*`), `PROGRESS*.md`, `PAUSED.md`. `sonic-garden/SCENE-CATALOG.md` cites `scenes/*.py`, which live at `tools/sonic_vfx/scenes/`. `bioluminescent-valley/PAUSED.md:63` cites a `hero_assets/README` that does not exist. |
| `experiments/` (3 json) | historical | Written by `tools/experiment.py`. |
| `audits/` | this audit | |

---

## 2. Broken paths and links

Checker: every tracked `.md` file. It checks relative Markdown links, and backticked paths that start with
`src/ tools/ examples/ shaders/ docs/ tests/ cmake/ assets/ prototypes/ .github/ scenes/`. A `:line` or
`::symbol` suffix is stripped and `x.hpp/.cpp` shorthand is accepted. A path counts as present if it is
tracked or on disk.

Raw result: **178 unresolved references in 82 docs**. Of these, 23 resolve only to gitignored local assets,
which is fine.

### In current docs (the ones worth fixing)

| Doc:line | Reference | Correct target / note |
|---|---|---|
| `docs/engineering-labs.md:180` | `src/world/effects.hpp` | The `src/world/effects/` directory (effect_stack, effect_instance, …). |
| `docs/pathtrace/overview.md:258` | `shaders/skinning.wgsl` | `shaders/pbr_skinned.wgsl` |
| `docs/tree-of-life-cosmic-plan.md:40` | `docs/adr` | `docs/decisions/` |
| `docs/quality-lab/repository-reconnaissance.md:452` | `docs/README.md` | No such file. It should be `README.md`, or the sentence is historical. |
| `docs/director-poc-plan.md:81` | `src/scene/placement` | `src/app/placement.{hpp,cpp}` |
| `docs/lights-and-viewport-authoring-audit.md:84` | `tests/unit/test_gizmo.cpp` | Never created (the audit proposed it). |
| `docs/character-ai-plan.md:50-52,131`, `docs/character-ai-research.md:339,446` | `examples/labs/character-ai/…`, `tests/unit/test_character_ai_lab.cpp`, `test_entity_determinism.cpp`, `assets/gltf_loader.cpp` | The plan's names. The lab shipped as `examples/labs/character/`. |
| `docs/camera-presets.md:198`, `docs/quality-lab/*.md` (9 refs) | `examples/camera/behaviors`, `examples/quality/aliasing(-dolly)` | Stems of real `.json`/`.scene.json` files. Leave them, or add `.json`. |
| `docs/help/start-projects.md:60` | `assets/composition.json` | Describes a file the app writes (`engine.cpp:3895`). Not broken. |
| `docs/development/gpu-ci-private-assets.md:62` | `.github/workflows/gpu.yml` | It is in the private repository. Not broken. |
| `docs/live-quality/PROGRESS.md:4` | `docs/live-quality/REPORT.md` | The report is `docs/investigations/live-projection-2026-10-02.md`. |
| `docs/glowmere-valley-2/12-phase-8.md:50` | `docs/camera-director.md` | Never existed. |
| `docs/prototypes/sonic-garden/SCENE-CATALOG.md` (15), `PROGRESS-vfx-art.md` (2) | `scenes/*.py` | `tools/sonic_vfx/scenes/*.py` |
| `docs/prototypes/astral-forge/README.md:39` | `tools/render_clips.sh` | `prototypes/astral-forge/tools/render_clips.sh` (the path is relative to the prototype). |
| `docs/prototypes/liminal-space/00-brief.md:32` | `docs/prototypes/liminal-space/PROGRESS.md` | The file was split into `PROGRESS-art.md` and `PROGRESS-eng.md`. |

### In historical records (leave them; listed for completeness)

These are 134 references in 56 docs. The main clusters:
- ADR-390 and ADR-441 point at Cosmic Ocean files, which ADR-441 deliberately removed.
- ADR-207, ADR-410, ADR-500 and `reports/entity-effects-refactor.md` point at `src/world/effects.hpp`, `src/world/world_effects/` and `shaders/world_effects.wgsl`. They were reorganised into `src/world/effects/`.
- ADR-340 and ADR-344 point at `tests/unit/test_glowmere_valley_3.cpp` and `tools/glowmere3_shot.py`, both retired.
- ADR-1140, ADR-1143, ADR-1144 and ADR-1153 point at `prototypes/astral-forge/shaders/*.wgsl`, which moved when production merged.
- ADR-1122 and `research/gpu-world-productionization.md` point at `examples/phonotaxis/`, which the owner dropped.
- `research/references.md` and `research/rendering.md` cite external repositories' paths.
- ADR-1081 and ADR-1082 point at `docs/live-quality/REPORT.md`.

The full machine output is reproducible with the checker described above. It is about 70 lines of Python and
was not committed.

---

## 3. Doc-vs-code contradictions

| # | Doc:line | Claim | Code evidence | Proposed correction |
|---|---|---|---|---|
| C1 | `docs/build.md:13-18` (presets table), `:37-45` (options) | Four presets (`debug`, `release`, `relwithdebinfo`, `asan`) and five options. | `CMakePresets.json` also has `tsan` and `ubsan` (configure, build and test presets). `CMakeLists.txt:30-35,57,63` and `cmake/Dependencies.cmake:159` also define `AVGEN_ENABLE_TSAN`, `AVGEN_ENABLE_UBSAN`, `AVGEN_ALLOC_COUNTERS`, `AVGEN_GPUWORLD_PROTOTYPE`, `AVGEN_ASTRAL_FORGE_PROTOTYPE` and `AVGEN_PATHTRACE_DENOISE`. Note also that `relwithdebinfo` has no test preset. | Add the two presets and six options to the tables. |
| C2 | `README.md:42-43` | "Python 3 (only for the test-audio generator)" | `tools/ci/run-suite.sh:94` runs `generate-audio.sh` for every CPU suite. `tools/ci/requirements.txt` pins `numpy==2.0.2` for `make_sonic_material.py` and `examples/bioluminescent/build.py --scene-only` (used by `test_rift`). Across the repo's Python, numpy is imported 67 times, PIL 13 and `bpy` 9. | "Python 3 with numpy (`pip install -r tools/ci/requirements.txt`) for the test suites' generated inputs. Some tools also need Pillow or Blender." |
| C3 | `docs/architecture.md:33-58` (module table), `:267-275` (layout) | 16 modules. The layout says `tools/  make_test_audio.py`. | `src/` also has `directing, entity, graph, help, labs, organism, pathtrace, search, seq, song, sonic, stage, world`, plus `comp`, `ai` and `share`, which the layout line omits. `tools/` has 132 entries. | Add rows for the missing modules, or mark the doc "as of 1.2" and point to ADRs. Regenerate the layout line. |
| C4 | `docs/help/reference-command-line.md:5` ("Every flag, every AVGEN environment variable…") | Complete reference. | Of the 114 flags in `usageText()` (`src/app/application.cpp`), **73 are missing**, for example `--labs`, `--lab-case`, `--pathtrace`/`--pt-*`, `--live*`, `--song-plan`, `--supersample`, `--save-scene`, `--validate-space`. Of the 36 `AVGEN_*` variables read via `getenv` in `src/`, 22 are missing. 8 appear in no doc at all: `AVGEN_ASTRAL_TRACE`, `AVGEN_CAPTURE_WORLD_INSPECTOR`, `AVGEN_HELP_SELFTEST`, `AVGEN_NO_CONTROL_REPLAY`, `AVGEN_NO_SEEK_DEFERRAL`, `AVGEN_PROFILE_CONTROL_REPLAY`, `AVGEN_SEEK_BODY_STEPS`, `AVGEN_TRACE_DEBUG`. | Either narrow the summary ("the common flags; `avgen --help` is complete") or add the sections. A test that compares `usageText()` with this page (like the shortcut test) would stop the drift. |
| C5 | `docs/engineering-labs.md:447` | `avgen --lab-case <lab>:<n> --print` prints the reproduce command. | No `--print` flag exists anywhere in `src/` (`parseArgs` would reject it). | Remove the line, or implement the flag. |
| C6 | `docs/testing.md:103-105` | "The test glob is configure-time, so an incremental build after a merge silently omits test files." | `tests/CMakeLists.txt:3,146` use `file(GLOB_RECURSE … CONFIGURE_DEPENDS …)` and have since `8483030b` (2026-09-09). With Ninja that re-globs on every build. | **NEEDS OWNER/verify.** The failure was observed (memory "stale test binary"), so the cause may be something else, such as a build of a single target or a different generator. Re-word it with the actual mechanism once it is known. |
| C7 | `--live-profile`, `--trace-jumps` | Missing from `usageText()`. | `src/app/live_profile*.cpp` and `src/app/main.cpp:22` handle both. | Add them to `--help` (code change, so not mine to make). |
| C8 | `docs/dependencies.md:44` | "Planned, not yet added: OpenImageDenoise …; meshoptimizer…" | `cmake/Dependencies.cmake:77-80` pins **meshoptimizer v1.2**, used by `src/assets/mesh_lod.cpp` and `src/scene/procedural.cpp`. `:159-165` fetches **OIDN v2.3.3** prebuilt, SHA-pinned, behind `AVGEN_PATHTRACE_DENOISE`. | Add rows: meshoptimizer v1.2 (MIT, ADR-078), and OIDN 2.3.3 (Apache-2.0, ADR-353; optional, off by default; it bundles oneTBB, also Apache-2.0). Remove both from "Planned". |
| C9 | `assets/farm.manifest.json` `"license"` | "Not recorded…" | `assets/farm/ATTRIBUTION.md`: "purchased, commercially licensed. NOT for distribution. Confirmed by the project owner 2026-09-28." | Change the string to match. It is a free-text field: `asset_library.cpp:236` only stores it. |
| C10 | `docs/decisions/README.md:20-47` (number-range table) | "701+ unassigned; ask before taking" | ADRs exist in 701-979 and 1110-1239 (for example ADR-1114 to ADR-1124, ADR-1160 to ADR-1168, ADR-1180 to ADR-1182, ADR-1200 to ADR-1239), and none of those ranges is in the table. | Add the ranges that were used, and move "unassigned" to 1300+. |
| C11 | `CMakeLists.txt:37-38` | A comment about a "Transitional (ADR-023)" stub option. | No option follows it. | Delete the dead comment (code-adjacent; for the C++ audit). |
| C12 | `src/scene/tree_generated.{hpp,cpp}` | The name suggests generated code. | Hand-written (ADR-175, `PrimitiveKind::Generated`). No generator exists. | No action. Recorded so nobody goes looking for a generator. |

Checked and consistent: `docs/development/ci.md` against `.github/workflows/ci.yml`, `sanitizers.yml` and
`tools/ci/run-suite.sh` (suites `cpu`, `cpu-assets`, `gpu-hosted`, `gpu`, `asan:<job>`, `ubsan`, `tsan`); the
`ctest -L unit|gpu` labels in `testing.md` (they match `tests/CMakeLists.txt`); the Dawn pin in `build.md`
(it matches `Dependencies.cmake:207-213`); and the binary paths in `build.md`.

---

## 4. ADR index

- 691 ADR files, 691 distinct numbers. There are **no duplicate numbers**.
- 691 index rows. No file is missing from the index, no row is dangling, every row's link resolves, and no row appears twice.
- Numbering runs from 1 to 1292 with 601 gaps. That is expected because numbers are assigned in ranges.
- ADR numbers cited in code (`src shaders tests tools examples cmake`) but missing a file: **none**.
- Cited in docs but missing a file: 946, 1167, 1291 and 1294. All four are explained in the text: never needed, reverted, rejected by the owner, or on the unmerged water branch.
- Status mismatches: the index says "Accepted" while the file says something else in four cases. Seven ADRs, ADR-384 among them, say `proposed`. ADR-569 says `finding … resolved by ADR-705`. ADR-575 has a split status. ADR-340 says `retired`. These are low priority.
- The range table is stale (C10).

---

## 5. Dependencies and licences

| Item | `Dependencies.cmake` | `dependencies.md` | `CREDITS.md` | Status |
|---|---|---|---|---|
| fmt 12.2.0, spdlog v1.17.0, glm 1.0.3, nlohmann v3.12.0, miniaudio 0.11.25, kissfft 131.2.0, fastgltf v0.9.0, stb 2c980bb, tinyexr v3.2.0 (SHA), Embree v4.4.0, Catch2 v3.16.0, SDL3 release-3.4.16, Dawn v20260907.201642/c4e47b5e (SHA), ImGui v1.92.9b-docking, ImPlot v1.0, Syphon 71351d4, CPM 0.43.1 | pinned | listed, same versions | n/a (permissive) | OK |
| meshoptimizer | **v1.2 pinned and linked** | "planned, not yet added" | n/a | **Mismatch (C8)** |
| OpenImageDenoise | **v2.3.3 prebuilt, SHA-pinned, optional** | "planned… licence unverified" | not mentioned | **Mismatch (C8)**. Apache-2.0 needs its licence text with redistributed binaries. CREDITS' library paragraph already covers Apache-2.0. |
| Python packages | none | not listed | n/a | numpy is pinned only in `tools/ci/requirements.txt`. Pillow and Blender/`bpy` are undocumented tool dependencies. |
| 100STYLE (CC BY 4.0) | n/a | listed | credit line present | OK, and enforced by test. |
| Tracked third-party assets | | | | Kenney Nature Kit: 31 `.glb`, CC0, recorded in `assets/manifest.json`, and every tracked file is listed there. Quaternius: 11 fixtures plus `License_Standard.txt`, CC0, recorded in `assets/imported/ATTRIBUTION.md`. `alien.gltf`, `ufo.gltf` and `camera_01.glb`: CC0, recorded. **No purchased asset is tracked at HEAD.** |
| Purchased assets (aliens, farm, musicians) | | | | Gitignored, which is correct. `assets/farm.manifest.json` licence field is stale (C9). The farm GLBs remain in public **git history** (`4bdc42ff`), a known issue left to the owner (`gpu-ci-private-assets.md:211`). `tools/ci/test-assets.list` puts `aliens/*.glb` and `farm/*.glb` in the **private** `av-gen-test-assets` repository, pinned at `61ff6dd`. Whether private GitHub hosting is allowed under "never upload" is an owner question: the policy text says never upload, and the plan is parked. |
| Treeisle (`assets/treeisle.manifest.json`) | | | | Licence "Not recorded"; gitignored. Fine as long as it stays untracked. |
| Songs | | | | None tracked. Tests SKIP without `~/Desktop/Rebuild.mp3`. |

---

## 6. Tests that read docs, and what they enforce

- **`tests/unit/test_repo_hygiene.cpp`**
  - No line in `src tests docs shaders tools` (`.cpp .hpp .wgsl .md .sh .py .json`) may start with a merge-conflict marker.
  - The set of `ADR-<n>` numbers in `docs/decisions/` filenames must equal the set of index rows matching `^\|\s*\[(\d+)\]` in `docs/decisions/README.md`. Both directions are checked, and there must be more than 100.
  - `CREDITS.md` must contain `The 100STYLE Dataset - Ian Mason`, `CC BY 4.0` and the CC BY 4.0 URL.
  - `docs/dependencies.md` must contain the same credit line.
- **`tests/unit/test_help.cpp`** loads `docs/help/` and SKIPs if it is absent.
  - Every table row has as many cells as its header.
  - The content loads and is not a shell, and it answers the acceptance questions.
  - Every panel and command found by the source scan is documented. The content does not disagree with the app.
  - Every shortcut in `docs/help/features.json` and `reference-shortcuts.md` is bound, and every binding is documented.
  - Every cited parameter path is registered.
  - No two sidebar rows collide as ImGui IDs.
  - **It does not check CLI flags**, hence C4.
- **`tests/unit/test_material_program.cpp:840`** ("docs/procedural-materials.md example programs…") and **`tests/unit/test_procedural.cpp:1798`** ("The docs/grammar-and-hierarchy.md examples…") **embed copies** of the doc examples. They never open the `.md`. The copies currently match (for example `alienMetal` at `procedural-materials.md:319-329`), but a doc edit would not be caught.
- `tests/support/project_assets.hpp:105`, `tools/fetch-test-assets.sh` and `tools/import_tree_isle.py` only cite doc paths in messages.

---

## 7. Validator outputs (verbatim summaries)

- `python3 tools/check_project_integrity.py` → exit 0:
  ```
  9 project(s) carrying effect parameters, 12 fingerprinting a scene
  0 problem(s)
  ```
- `assets/audio/manifest.json`: all 9 on-disk scores match their recorded sha256 (glowmere-valley, night-shift and 7 sonic-*).
- ADR index check: see §4.
- `git ls-files -ci --exclude-standard`: 36 tracked files match ignore rules (see `artifacts-inventory.md`).
- Not run (they need the built engine): `avgen_help_lint`, `avgen --audit-routes`, `tools/certify.py`.

---

## 8. Proposed safe actions

SAFE means no judgement is needed. NEEDS OWNER means a decision or a data change is involved.

### Repository hygiene
1. **SAFE**: `git rm indtune.cpp`. It is 0 bytes, no CMake file references it, and `docs/engineering-labs.md:514` already calls it out. Added `db2034ca`. Then update `engineering-labs.md` item 8.
2. **SAFE**: `git rm temporal-plain.png temporal-echo-3.png temporal-echo-10.png temporal-echo-cold.png temporal-ring-full.png temporal-ring-settling.png` (98 KB). These are outputs of the `[.capture]` case in `tests/rendering/test_temporal_gpu.cpp:277`, which writes to the cwd when `AVGEN_CAPTURE_DIR` is unset. They were committed by accident in `e841d1e0`, and nothing references them. Also add `/temporal-*.png` to `.gitignore`, or make the test default to a temp dir (code change).
3. **SAFE**: `git rm -r .agent/`. These are 5 agent-scratch files frozen at 2026-09-19, and nothing references them. Their content is in ADRs and memory.
4. **NEEDS OWNER**: `git rm 05-static-reference-vs-f027-full.png` (3.7 MB, unreferenced). The owner added it in `7b83fd58` ("ref"). It looks like a biovalley-2d comparison still that belongs in `~/Desktop/av-gen-review/`.
5. **NEEDS OWNER**: `git rm` the 9 unreferenced `examples/treeisland/renders/cosmicocean/*.png` (25 MB, for a feature ADR-441 removed). Keep `01-crop-the-square-lattice.png` (ADR-393:78) and `14-quarter-vs-full-diff-x20.png` (ADR-450:117), or move both to `docs/decisions/img/`. They are tracked against the `renders/` ignore rule.
6. **NEEDS OWNER**: The `examples/treeisland/renders/cosmickey/{before,after}/*/frame_000000.png` set (19 files, 27 MB). Eight are cited by ADR-358 and ADR-372 or named by `tools/make_cosmickey_arms.py`. The `before/` orbit, shadow, wide and under frames and the `_ck-drift-t0` and `t45` frames are unreferenced. `_ck-drift-t0` is byte-identical to `_ck-view-wide`. Removing them from the tip does not shrink history.

### Doc fixes (SAFE: text only, no behaviour)
7. **SAFE**: `docs/build.md`: add the `tsan` and `ubsan` presets and the options `AVGEN_ENABLE_TSAN`, `AVGEN_ENABLE_UBSAN`, `AVGEN_ALLOC_COUNTERS`, `AVGEN_PATHTRACE_DENOISE`, `AVGEN_GPUWORLD_PROTOTYPE` and `AVGEN_ASTRAL_FORGE_PROTOTYPE` (C1).
8. **SAFE**: `README.md:42-43`: fix the Python requirement (C2). Replace "Ten example worlds" with a reference to `examples/index.json` (84 entries).
9. **SAFE**: `docs/dependencies.md`: add rows for meshoptimizer v1.2 and OIDN 2.3.3, and remove both from "Planned" (C8). Optionally add a "Python tooling" line (numpy pinned in `tools/ci/requirements.txt`; Pillow and Blender optional).
10. **SAFE**: `docs/engineering-labs.md:447`: remove the `--print` line (C5). In `:180`, change `src/world/effects.hpp` to `src/world/effects/`.
11. **SAFE**: `docs/pathtrace/overview.md:258`: `shaders/skinning.wgsl` → `shaders/pbr_skinned.wgsl`.
12. **SAFE**: `docs/tree-of-life-cosmic-plan.md:40`: `docs/adr` → `docs/decisions/`. `docs/director-poc-plan.md:81`: `src/scene/placement` → `src/app/placement.cpp`.
13. **SAFE**: `docs/live-quality/PROGRESS.md:4`: point at `docs/investigations/live-projection-2026-10-02.md`.
14. **SAFE**: `docs/prototypes/sonic-garden/SCENE-CATALOG.md` and `PROGRESS-vfx-art.md`: `scenes/` → `tools/sonic_vfx/scenes/`. `docs/prototypes/liminal-space/00-brief.md:32`: `PROGRESS.md` → `PROGRESS-eng.md` / `PROGRESS-art.md`.
15. **SAFE**: `assets/farm.manifest.json` `license`: align with `assets/farm/ATTRIBUTION.md` (C9).
16. **SAFE**: `docs/decisions/README.md` range table: add the used ranges (701-979, 1110-1139, 1160-1239) and change "701+ unassigned" to "1300+ unassigned" (C10).
17. **SAFE**: `docs/help/reference-command-line.md:5`: narrow the summary to "the common flags; `avgen --help` lists all" until the reference is completed (C4).

### Needs a decision
18. **NEEDS OWNER**: `docs/architecture.md`: rewrite the module table (C3), or label it historical and add a pointer.
19. **NEEDS OWNER**: `docs/testing.md:103`: settle the stale-binary mechanism (C6).
20. **NEEDS OWNER**: Move scratch docs into an archive location: `docs/TODO-glowmere-handoff.md`, `docs/upgrade-status.md`, `docs/development/STATUS-2026-09-25.md`, and the top-level `*-spec.md`, `*-plan.md` and `*-brief.md` files. See `maintenance-conventions-draft.md`.
21. **NEEDS OWNER**: The private test-asset repository holds purchased aliens and farm GLBs (`tools/ci/test-assets.list`), which is in tension with the "never upload" rule in the ATTRIBUTION files.
22. **NEEDS OWNER / code**: Add `--live-profile` and `--trace-jumps` to `usageText()`. Make the material and grammar doc tests read the `.md` instead of embedded copies.
