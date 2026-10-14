# Repository maintenance conventions

This page records what the repository already does, so that a newcomer knows where things go. When
the code and a document disagree, the code and its executable behaviour win, and the document gets
fixed.

## Current reference and records

- **Current reference** must match the code, and a change that alters behaviour updates it in the
  same commit. That covers `README.md`, `CREDITS.md`, `docs/build.md`, `docs/testing.md`, the
  subsystem guides at `docs/*.md`, `docs/help/` (shipped in-app help, test-enforced),
  `docs/development/ci.md`, the lab READMEs and `docs/visual-cookbook/`.
- **Canonical sources.** Flags come from `avgen --help`. Presets and options come from
  `CMakePresets.json` and `CMakeLists.txt` (`docs/build.md` mirrors them). Library pins live in
  `cmake/Dependencies.cmake`, licences in `docs/dependencies.md`, and published credits in
  `CREDITS.md`. Asset licences are in `assets/**/ATTRIBUTION.md` and `assets/*.manifest.json`,
  which must agree.
- **Records** are everything else: ADRs, dated reports, prototype logs. Nobody "fixes" a record to
  match today's code. If a current-reference doc becomes obsolete, it gets a one-line banner at the
  top: `> Historical (as of <date>): superseded by <link>.`

## Where things go

| Kind | Location | Rule |
|---|---|---|
| Decisions | `docs/decisions/ADR-<n>-<slug>.md` plus a row in the [index](../decisions/README.md) | Take numbers from your branch's range in the index's range table, and add the range there first. A record is immutable once Accepted; supersede it with a new ADR. `test_repo_hygiene` checks that files and index rows match. |
| Investigations, reports, measurements | `docs/investigations/<topic>-YYYY-MM-DD.md`, `docs/reports/`, `docs/measurements/` | Dated, with machine, build and commit. Read-only after landing. |
| Workstreams and prototypes | `docs/prototypes/<name>/`: `00-brief.md`, `PROGRESS*.md`, then `README.md` or `PAUSED.md` as the resume point | One folder per workstream, nothing at the top of `docs/`. Code lives on `proto/<name>`. |
| Agent session state (handoffs, RESUME, TODO) | not in git | Fold it into the workstream's `PAUSED.md` or `README.md` when the work stops. |
| Review media (renders, clips, contact sheets) | `~/Desktop/av-gen-review/<NN-topic>/`, outside the repository | Pixels stay out of git. An image an ADR must show goes in a small `img/` beside it, and the ADR cites it. |

## Scenes and examples

`examples/index.json` is the scene catalogue, and the app's File > Examples > Live and Offline
menus read it (ADR-1340). No doc counts its entries. A live scene needs `"live": true` and a `sonic.live` block,
and the tests enforce that. Diagnostic arms sit beside their deliverable with a leading `_`. Run
`python3 tools/check_project_integrity.py` before committing a change under `examples/`. It must exit 0.

## Generated artifacts

The generator is the source of truth. Never hand-edit its output. Change the generator, re-run it and
commit the outputs together. Every generator says what it writes (in its docstring), or its manifest
records the command. Outputs that are bulky, regenerable or licence-restricted are gitignored with a
tracked manifest. The common commands:

- Scores: `tools/ci/generate-audio.sh`. The commands and sha256s are in `assets/audio/manifest.json`. It needs numpy (`tools/ci/requirements.txt`).
- Scene fingerprints: `python3 tools/refresh_scene_fingerprint.py [scene…]`.
- Derived examples: the `build.py` beside them (`examples/chorus-field/`, `examples/bioluminescent/`,
  `examples/digital-mosh/`), or the `tools/make_*.py` script that writes them.
- Gitignored test inputs: [ci.md, "Generated inputs"](ci.md#generated-inputs).

Tests and tools that write images default to a temp directory, never the working directory.

## Ending an experiment

An experiment ends in one of three ways. On **merge**, its lasting decisions become ADRs, the
current-reference docs it changed are updated, and its folder's `README.md` gives the date, the
commit and "merged". On **pause**, it gets a `PAUSED.md` with the branch, the commit and how to
resume. On **abandon**, nothing outside its folder may link to it as current, and its example files
leave the repository along with their index entries. Before merging, check the relative links and
backticked repository paths in the `.md` files the branch touched. A broken path in a current doc
counts as a failure; records are exempt. Keep the repository root to the existing set: README,
CREDITS, LICENSE, the CMake files and dotfiles.
