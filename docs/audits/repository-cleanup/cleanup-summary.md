# Repository cleanup: summary

*2026-10-14, for the owner's review. Every claim here links to the document that holds the evidence.*

## What changed

- **Consolidation** ([repository-inventory.md](repository-inventory.md)):
  - **Integrated:**
    - the Biovalley colourways that `main`'s macro already depended on;
    - the `test_rift` fix stranded on a paused branch;
    - two live-render investigation reports.
  - **Deleted:** 8 stale local branches, all contained in `main`, a remote branch, or a retained sibling, with tips
    recorded; and 2 worktrees (`av-gen-furnace-eng`, `av-gen-biovalley`).
  - **Retained, for the owner's verdict:** five paused prototype branches and their worktrees.
  - **Kept:** the three stashes.
  - **Generated outputs that git would not have protected** (the Biovalley meshes, sky and shots, 1.3 GB) were moved
    into the owner's checkout.
- **CI recovered** ([ci-and-test-report.md](ci-and-test-report.md)):
  - generated test inputs for clean checkouts (the Rift world, the sonic scores via numpy);
  - honest SKIPs for the owner's songs;
  - a non-hermetic build fixed (Homebrew simdjson leaking into the binaries), with a CI guard so it cannot recur.
- **Code fixes** ([code-audit.md](code-audit.md)):
  - a data race between the Director's worker Engines and the main Engine (TSan-verified, with a negative control);
  - path-trace tests that passed when they should skip or fail;
  - leftover profiling, a silently dropped timeout, triplicated tool source lists, and stray and dead files.
- **Scenes** ([scene-inventory.md](scene-inventory.md), ADR-1340):
  - File > Examples now has exactly two submenus, **Live** and **Offline**.
  - 11 scenes that were in no menu were added (84 → 95 entries).
  - An entry whose generated files are missing is shown greyed, with the command that makes them.
  - Every entry was opened through the menu path and rendered (`tools/review_examples.py`).
- **The owner's songs:** Rebuild and All You Got now live in the checkout as `assets/audio/rebuild.mp3` and
  `assets/audio/all-you-got.wav`. Both are gitignored and never committed; every project, test, tool and doc points
  there. The Desktop originals were copied, not moved, and can be deleted.
- **Docs reconciled with the code:**
  - build presets and options, the README, dependencies and licences, CI's generated inputs, the testing doc, the ADR
    range table, the CLI reference summary, help pages for the new menu;
  - a short [maintenance-conventions](../../development/maintenance-conventions.md) page.
- **Windows:** a staged, opt-in self-hosted runner integration ([windows-self-hosted-runner.md](windows-self-hosted-runner.md)).

## State of `main`

- **`main`** was fast-forwarded to the cleanup branch. The pre-cleanup tip was `7b83fd58`; the branch history and its
  merge commits are kept. The owner's checkout is clean: the songs and the Biovalley outputs are present but
  gitignored.
- **Buildable:** a full `cmake --preset release` build exits 0, and all four binaries link only system libraries.
- **Tests, at `b7b8a0f5`, locally:**
  - CPU suite with this machine's assets: **exit 0**, 4222 passed / 0 failed / 19 environmental skips / 1 expected
    failure, 4242 of 4242 ran;
  - GPU suite: **exit 0**, 655 / 0 / 1 (NDI not installed), 656 of 656.
- **Remote CI is green again:**
  - `38597524868` and `38601296548` on the branch, the latter 3971 passed / 0 failed / 4242 ran. These were the
    first green CI runs since 2026-10-07.
  - On `main`, the push run `38608340513` and a dispatched full run `38608348660` are both green. The full run
    includes the private-asset tier (4153 passed / 0 failed / 4242 ran), which had failed every nightly since
    2026-10-08.
  - Sanitizers was dispatched on `main` (`38608392071`); it takes about 3.5 h.
- **Not covered by CI:** no self-hosted Apple GPU runner is registered, so the GPU suite's authoritative evidence is
  the local run above.

## Scenes

- **81 unique scenes: 53 Offline, 28 Live**, in 95 menu entries: 66 Offline, 29 Live. The difference is variants;
  see the grouping in [scene-inventory.md](scene-inventory.md).
- **Verification:** all 95 entries opened through `avgen --example` and rendered a valid frame (exit 0, not black,
  not flat), and both contact sheets were inspected. That is level 5 for all 81 scenes. Audio/MIDI reactivity and
  live budget were not measured. The sheets are in `~/Desktop/av-gen-review/47-repository-cleanup/`.
- **Removal classes** ([scene-removal-impact.md](scene-removal-impact.md)): **A** 7, **B** 46, **C** 17, **D** 10,
  **E** 1.
- **Five easiest to cut (A):** The Tide, Fungi, Organic Curves, Alien Squad, Farm Animals. Open Vista and Astronaut
  Musicians are also A.
- **Significant dependencies (D):**
  - **The Astral Forge:** sole user of the `astral` renderer.
  - **Digital Mosh:** sole user of the `quantize`, `displaceField` and `points` kinds.
  - **THE RIFT:** the ecosystem Environment and excitable grid.
  - **Sonic Live:** named in `src/`.
  - **Renderer QA:** the Visibility Lab fixture, with 17 GPU tests.
  - **Glowmere stylized:** the source of the `glowmere` art profile.
  - **Glowmere Valley 2:** about 190 test and tool references, and the Interaction Lab fixture.
  - **Tree of Life:** about 60 measurement arms.
  - **Chorus Field:** sole fiber/streamline user.
  - **Liminal:** sole user of the stairs/warp/screw kinds and the beat grid.
- **E:** the Biovalley macro. It was not inspectable at audit time; its outputs are now in the owner's checkout and it
  renders, but its removal impact has not been reassessed.
- **Cutting a scene:** the removal-impact table names the files to edit. Watch for the live-entry floor (`>= 28`)
  and the Sonic set sizes (17 and 9) in the menu tests.

## The five highest-value remaining cleanup tasks

1. **CA-003:** decide whether an offline render may ship fallback content when an asset fails to load. Today it exits
   0, and CA-009's timeout is waiting on this too.
2. **Register the self-hosted Apple GPU runner** (`vars.AVGEN_GPU_RUNNER`). The GPU suite has no authoritative CI run
   today; it is only verified on this Mac.
3. **Move wall-clock assertions out of the default and sanitizer sets** (CA-011). They are the likeliest hosted-CI
   flakes.
4. **Decouple scenes from shared code:**
   - replace the "Sonic Live" name checks with an index flag;
   - name a default world and art profile rather than "glowmere";
   - point lab fixtures at dedicated fixtures.

   These are what make the D-class scenes hard to cut.
5. **Decide on the six `src/` modules with no product caller**, plus CA-016 (legacy double generation) and CA-019
   (parser aliases and old-project migration), then delete.

## Decisions requiring the owner

1. **The menu change.** File > Live Projects is now File > Examples > Live (ADR-1340), as the brief's "exactly two
   top-level categories" required. If you want the one-click Live Projects menu back beside it, it is a small change.
2. **Paused branches** (`proto/astral-furnace`, `proto/astral-forge-2`, `proto/rift-jungle`, `proto/bio-valley-water`,
   `proto/biovalley-original`): merge, keep paused, or archive. None was merged; each is mid-art-direction or awaiting
   your verdict. `bio-valley-water` contains the rejected ADR-1291.
3. **The three stashes** (from 2026-09-14 to 09-24) and the merged remote branches (`live/optimizer`, `qa/coord`,
   `live/quality`, `qa/ci`, `proto/liminal-space`, `proto/sonic-garden`, and this sprint's
   `cleanup/repo-consolidation`): drop or keep. Nothing remote was deleted.
4. **Tracked files of unclear value:**
   - `05-static-reference-vs-f027-full.png` (3.7 MB, your "ref" commit);
   - the treeisland `cosmicocean`/`cosmickey` render PNGs (53 MB, mostly unreferenced);
   - the superseded Liminal passes 1-3 and the Chorus Field p1-p7 iterations (regenerable, about 150 files).
5. **Rebuild in the private test-asset repository.** You already allowed it; it would un-skip about 41 asset-tier
   cases.
6. **CA-003, CA-006, CA-016, CA-019 and the dead modules** ([code-audit.md](code-audit.md)).
7. **Windows:** whether to set up the runner, following the guide's stages.

## Windows self-hosted runner (summary)

- **Today:** no existing workflow can use Windows. AV Gen has never been built there, and every job builds or tests
  the macOS/Metal binary. **Verified** by inspection.
- **First candidates for the PC:** the opt-in `windows.yml` stages (connectivity, Python validators, an
  informational MSVC configure). **Ready to configure.**
- **Must stay on macOS:** the build, the CPU suite, the asset tier, the sanitizers, and all Metal/GPU work.
- **Known gaps:**
  - no Windows Dawn branch in CMake, though the pinned release ships the archive (hash recorded);
  - a Metal-only window surface (headless D3D12 needs no surface);
  - POSIX calls in about 18 source files and 26 test/tool files.

  **Requires investigation.**
- **Security:** a public repository on a persistent home machine. Never `pull_request`, no secrets, a restricted
  account, and require approval for all outside contributors.
- **Steps:** [windows-self-hosted-runner.md](windows-self-hosted-runner.md) §3 (setup), §4 (workflow), §5 (staged
  validation).

## Documentation and artifacts

- **Updated:**
  - `README.md`, `docs/build.md`, `docs/dependencies.md`, `docs/testing.md`, `docs/development/ci.md` (new
    "Generated inputs");
  - the `docs/decisions/README.md` ranges, plus ADR-1340;
  - `docs/help/reference-menus.md`, `start-projects.md` and `reference-command-line.md`;
  - `docs/engineering-labs.md`, `docs/pathtrace/overview.md`, `docs/director-poc-plan.md`,
    `docs/live-quality/PROGRESS.md`, the Sonic Garden and Liminal path fixes;
  - `assets/farm.manifest.json`'s licence text;
  - every doc that gave the Desktop path for Rebuild or All You Got.

  See [data/docs-fixes-applied.md](data/docs-fixes-applied.md).
- **Regenerated and verified:**
  - **Scores:** all 9 reproduce their manifest SHA-256 on this Mac and on CI.
  - **Liminal:** all its generators were rerun after the song move. Every edited project is byte-identical to its
    generator's output, except two that predate this sprint (below).
  - **`rift.scene.json`:** generated by `test_rift`.
  - **`tools/check_project_integrity.py`:** 0 problems.
- **Removed:** `indtune.cpp`, six root `temporal-*.png`, `.agent/` (agent scratch), `tools/_slope_probe.cpp`.
- **Validation:**
  - `test_repo_hygiene` (ADR index ↔ files) and `test_help` pass;
  - a link and path check over the changed docs and these reports: 2 hits remain, both deliberate mentions of the
    removed `tools/_slope_probe.cpp`;
  - `check_project_integrity.py` exits 0.
- **Remaining stale or unverifiable:**
  - **Drift that predates this sprint:**
    - `examples/chorus-field/build.py` would rewrite 7 committed `p7-god-*.scene.json` (a `brow` field);
    - `tools/make_foot_ik_lab.py` would change `examples/labs/footik/*.scene.json`;
    - Liminal's pass-1 `all-you-got.json` (a hand-added `live.qualityStrategy`) and `all-you-got-pass3.scene.json`.

    These are left as committed: regenerating would change scenes the owner approved.
  - **Not updated, for the owner:** `docs/architecture.md` lists 16 of 32 modules; `docs/help/reference-command-line.md`
    lacks 73 flags (now says so).
  - Historical records (ADRs, prototype logs) were left as records. Only the song paths in them were updated, per the
    owner's request.
