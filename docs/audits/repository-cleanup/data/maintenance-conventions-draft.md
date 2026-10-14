# Repository maintenance conventions (draft)

This draft writes down what the repository already mostly does, so that a new agent knows where things go.
It proposes no new tooling beyond two cheap checks.

## 1. Which docs are authoritative

- **Current reference** is `README.md`, `CREDITS.md`, `docs/build.md`, `docs/testing.md`,
  `docs/architecture.md`, the subsystem guides at `docs/*.md` (audio, rendering, lighting, project-format, sequencer, …),
  `docs/help/` (shipped in-app help), `docs/development/ci.md`, the lab READMEs (`docs/*-lab/`,
  `docs/quality-lab/`), and `docs/visual-cookbook/`. These must match the code. A change that alters behaviour
  updates them in the same commit.
- **Canonical sources, by question:**
  - Flags: `avgen --help` (`usageText()`); `docs/help/reference-command-line.md` mirrors it.
  - Presets and options: `CMakePresets.json` and `CMakeLists.txt`; `docs/build.md` mirrors them.
  - Library pins: `cmake/Dependencies.cmake`. Licences: `docs/dependencies.md`. Published credits: `CREDITS.md`.
  - Asset licences: `assets/**/ATTRIBUTION.md` and `assets/*.manifest.json`, which must agree.
- Everything else is a **record**. Records are never "fixed" to match today's code. They carry a date and they are not linked from `README.md` as current.

## 2. Where records go

| Kind | Location | Rule |
|---|---|---|
| Decisions | `docs/decisions/ADR-<n>-<slug>.md` + an index row | Immutable once Accepted; supersede with a new ADR. Numbers come from the range table in `docs/decisions/README.md`, and that table is updated when a range is opened. `test_repo_hygiene` enforces index parity. |
| Dated investigations and reports | `docs/investigations/<topic>-YYYY-MM-DD.md`, `docs/reports/` | Read-only after landing. |
| Workstream and prototype logs | `docs/prototypes/<name>/` (`00-brief.md`, `PROGRESS*.md`, `PAUSED.md`, `README.md` as the resume entry point) | One folder per workstream. Nothing at `docs/` top level. |
| Owner briefs and specs | the workstream folder, as `00-brief.md` | not `docs/*-spec.md` / `*-brief.md` at top level |
| Agent session state (handoffs, CURRENT_TASK, RESUME, HANDOFF, TODO) | **not in git**. Use the agent's memory or `~/Desktop/av-gen-review/`. | When a workstream pauses, fold its state into that folder's `PAUSED.md` and delete the scratch. `.agent/` should go. |

Existing top-level scratch (`TODO-glowmere-handoff.md`, `upgrade-status.md`, `*-plan.md`, `*-spec.md`,
`group-d-city-brief.md`, `engine-brief.md`) can be moved into `docs/archive/` in one commit, which keeps the
top level for current reference only. That move NEEDS OWNER.

## 3. Scene catalogue

- `examples/index.json` is the catalogue, and the only list. The README points at it and never counts entries.
- A shipped example is either a project or a recipe named there. Live scenes need `"live": true` and a `sonic.live` block (test-enforced).
- Diagnostic arms are written beside their deliverable with a leading `_` (`_fog-*`, `_ck-*`, …). Arms are gitignored by pattern unless a test reads them; in that case the test names them.

## 4. Generated artifacts

- Every generated tracked file must name its generator: in a docstring that says "never edit the generated JSON", or in a manifest field (`regenerate`, `command`).
- The generator is the source of truth. **Never hand-edit an output.** Change the generator and re-run it.
- After changing a generator, or a scene a generator derives from, re-run it and commit the outputs in the same commit.
- Proposed check (cheap; no engine needed): a `tools/check_generated.sh` that runs the engine-free generators verified clean in this audit, then runs `git diff --exit-code`. It would have caught the chorus-field p7 and foot-IK drift.
- Outputs that are bulky, regenerable, or carry restricted licences are gitignored, and their manifest is tracked. This is already done for audio, HDRIs, treeisle, aliens, farm, musicians and the biovalley meshes.
- `python3 tools/check_project_integrity.py` must exit 0 before a commit that touches `examples/`.

## 5. Measurements, renders and visual baselines

- Pixels do not go in git. Review renders go to `~/Desktop/av-gen-review/<NN-topic>/`.
- In-repo evidence is only the **queue or command** plus small JSON or TXT numbers (`renders/<topic>/queue.json`, `MEASUREMENTS.txt`), force-added under the ignored `/renders/`.
- An image that an ADR must show goes in `docs/decisions/img/` (or the workstream's `img/`), downscaled to 200 KB or less, and the ADR cites it. Unreferenced images are removed.
- Quality Lab outputs (`/quality-results/`) and benchmark scenes (`_bench/`) are never committed. Their manifest and metrics are the record.
- Tests and tools that write images default to a temp directory, never the cwd. That is how the root `temporal-*.png` files got committed.

## 6. Keeping experiments from leaving stale docs

- An experiment lives on its `proto/<name>` branch with its docs under `docs/prototypes/<name>/`.
- On merge, the experiment's lasting decisions become ADRs. Any current-reference doc it changed is updated. Its prototype folder gets a `README.md` that says merged, paused or abandoned, with the date and the commit.
- On abandon, keep the folder, but nothing outside it may link to it as current. Delete the example files, or gitignore them, together with their index entries.
- Before merging, run the doc path checker (a link and backtick-path check over the `.md` files the branch touched) and treat broken paths in current docs as failures. Records are exempt.
- Never commit from the main checkout's root by accident. `git status` before `git add -A`. Keep root-level files limited to the existing set (README, CREDITS, LICENSE, CMake, dotfiles).
