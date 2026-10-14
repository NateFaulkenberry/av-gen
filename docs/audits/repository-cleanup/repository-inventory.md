# Repository inventory: branches, worktrees and stashes

The 2026-10-14 consolidation. Every disposition below was checked by commit containment
(`git merge-base --is-ancestor`), unique patches (`git cherry`) and the tree diff against `main`, not by branch
name.

## Starting state (2026-10-14, before any change)

- `main` = `origin/main` = `7b83fd58` ("ref", 2026-10-12). The owner's checkout `av-gen` was on `main` and clean.
- 16 local branches, 7 remote branches (plus `origin/HEAD`), 9 worktrees, 3 stashes. No worktree had uncommitted or
  untracked-unignored changes.
- CI: `CI` had been red since `37655065611` (2026-10-07); `Sanitizers` was green. See `ci-and-test-report.md`.

## Branch dispositions

"Ahead" means commits not in `main`. "Unique" means patches with no equivalent in `main` (`git cherry`).

| Branch | Ahead / unique | Class | Disposition | Why |
|---|---|---|---|---|
| `proto/bio-valley-assets` (worktree `av-gen-biovalley-assets`) | 1 / 1 | **Merge candidate** | **merged** (`717d9ab7`) | The warm and magenta colourways. `main`'s merged Biovalley macro already places and routes the `*_amber`, `*_coral`, `*_rose`, `*_magenta` and `filigree_ambertip` variants, but `main`'s `colorways.py` could not produce them. |
| `proto/astral-furnace-eng` | 60 / 49 | contains a **fix main needs** | `2ade3979` **cherry-picked** (`efe5f2f2`); branch **deleted** (tip `b5a1fe3a`, fully contained in `proto/astral-furnace`); worktree removed | The `test_rift` clean-checkout fix (generate `rift.scene.json` or SKIP with the command). It is generic, and it was the main cause of red CI. |
| `investigate/live-render-perf` | 4 / 2 | **Needs repair** (split) | 2 report commits **cherry-picked** (`769f30f1`, `631d9c2f`); branch **retained** | The two reports (`docs/investigations/live-*-2026-10-02.md`) are history worth keeping. The other two commits are labelled "TEMPORARY … investigation only, not for main" and must not merge. The reports cite the probe code by its commit on this branch, so the branch stays. |
| `proto/sonic-garden` (local) | 126 / 126 | **Superseded** | local branch **deleted** (tip `fd61230e`); `origin/proto/sonic-garden` keeps it | It was squash-merged as `38dcc348`. The tree diff against `main` adds no file under `examples/ src/ tools/ tests/ shaders/`, and the only differing doc line is older than `main`'s. |
| `live/quality`, `qa/ci`, `proto/liminal-space`, `merge/bio-valley-macro` | 0 / 0 | **Superseded** (merged) | local branches **deleted** (tips `5fcd7ad4`, `2f89010a`, `b3862d23`, `cee3169b`); the remotes are kept | Ancestors of `main`. |
| `proto/biovalley-2d` | 27 / 27 | **Superseded** | **deleted** (tip `ed9c5b97`) | It is an ancestor of `proto/biovalley-original`, which is retained. |
| `proto/bio-valley` (worktree `av-gen-biovalley`) | 0 / 0 | **Superseded** (merged as `cee3169b`) | branch and worktree removed **after** its generated outputs were moved (below) | The worktree held the only built copy of the Biovalley macro's gitignored meshes, sky and shots (1.3 GB). |
| `proto/astral-furnace` (worktree `av-gen-furnace`) | 80 / 68 | **Unclear: owner's call** | **retained** | Paused at cp17 for the owner's verdict (PAUSED.md: cp18 list and merge blockers). The owner unshelved it the same day. Not complete work. |
| `proto/astral-forge-2` (worktree `av-gen-astral2`) | 7 / 7 | **Unclear: owner's call** | **retained** | Paused 2026-10-07. The suites were never run on its final commit, and the owner wants to repurpose its ocean renderer later. |
| `proto/rift-jungle` (worktree `av-gen-jungle`) | 11 / 11 | **Unclear: owner's call** | **retained** | THE ROOTWORKS, parked when the owner moved the scene to a new direction. Its `avgen_tests` failures were never investigated. |
| `proto/bio-valley-water` (worktree `av-gen-biovalley-water`) | 10 / 8 | **Needs repair; owner's call** | **retained, must not merge as is** | WIP ADR-1296 (paused, `cs_reflect` unwritten). The branch history includes ADR-1291, which the owner rejected. |
| `proto/biovalley-original` (worktree `av-gen-biovalley-2d`) | 33 / 33 | **Unclear: owner's call** | **retained** | Biovalley 2D, "Wetland of Veils", stopped for the owner's verdict (stage E). |
| `origin/live/optimizer`, `origin/qa/coord`, `origin/live/quality`, `origin/qa/ci`, `origin/proto/liminal-space`, `origin/proto/sonic-garden` | merged or superseded | | **not touched** | Remote branches are deleted only with the owner's authorisation. All of them could go. |

### Cleanup branches created and merged

- `cleanup/repo-consolidation`: the integration branch, merged into `main`.
- `cleanup/code-fixes` and `cleanup/docs-fixes`: the two fix branches, merged into it and then deleted.

## Stashes (kept, documented)

| Stash | Date | Content | Disposition |
|---|---|---|---|
| `stash@{0}` "seek parity investigation" (on main) | 2026-09-24 | `engine.hpp`, `composition.*`, `seq/events.*` (165+/14-) | **Kept.** Superseded: the seek-parity work landed as ADR-800/870/1114. No unique value found, but it costs nothing to keep, so the owner can drop it (`git stash drop stash@{0}`). |
| `stash@{1}` "WIP ADR-349 scene-colour capability (incomplete bind-group surgery)" (on agent/waterworld) | 2026-09-19 | `scene_renderer.*` (28+/11-) | **Kept.** Incomplete WIP against a renderer that has changed a great deal since. |
| `stash@{2}` "user editor save of glowmere-valley-2.json before merging agent alignment work" | 2026-09-14 | `examples/world/glowmere-valley-2.json` (+5126) | **Kept.** The owner's own editor save; only the owner can say whether anything in it is wanted. |

## Generated outputs preserved

Gitignored and generated, so git would never have protected them:

- **The Biovalley macro.** `examples/biovalley/meshes/` (1.2 GB), `sky/` and `shots/` were moved from the
  `av-gen-biovalley` worktree into the owner's checkout. The macro is now reviewable without Blender, and the scene
  review renders it. `av-gen-biovalley-assets`'s 665 mesh files are a byte-size-identical subset of these, so that
  worktree held nothing extra.
- **THE RIFT.** `examples/bioluminescent/rift.scene.json` was copied into the owner's checkout. `test_rift`
  generates it with `build.py --scene-only`, which writes the same file as `build.py`.

## Ending state (2026-10-14)

- **`main`** = `origin/main`, fast-forwarded from `7b83fd58` to the cleanup branch (`99fabf5f`, plus the final
  results commit). The owner's checkout is on `main` and clean.
- **Local branches (8):**
  - `main`;
  - `cleanup/repo-consolidation`: merged, kept until the final commit, then deleted along with its worktree;
  - `investigate/live-render-perf`: retained, the probe code the reports cite;
  - the five paused prototypes, each with its worktree: `proto/astral-forge-2`, `proto/astral-furnace`,
    `proto/bio-valley-water`, `proto/biovalley-original`, `proto/rift-jungle`.
- **Worktrees:** `av-gen` (`main`) and the five prototype worktrees. Removed: `av-gen-furnace-eng`,
  `av-gen-biovalley`, `av-gen-biovalley-assets`, and this sprint's `av-gen-cleanup-code` and `av-gen-cleanup-docs`.
- **Deleted local branches**, with their tips (restore with `git branch <name> <sha>`):

  | Branch | Tip |
  |---|---|
  | `live/quality` | `5fcd7ad4` |
  | `merge/bio-valley-macro` | `cee3169b` |
  | `qa/ci` | `2f89010a` |
  | `proto/liminal-space` | `b3862d23` |
  | `proto/sonic-garden` | `fd61230e` |
  | `proto/biovalley-2d` | `ed9c5b97` |
  | `proto/astral-furnace-eng` | `b5a1fe3a` |
  | `proto/bio-valley` | `74cf7f30` |
  | `proto/bio-valley-assets` | `b50bcf97` |
  | `cleanup/code-fixes` | `53dbfb6e` |
  | `cleanup/docs-fixes` | `28b4fd5a` |

- **Remote branches:**
  - unchanged: `live/optimizer`, `live/quality`, `proto/liminal-space`, `proto/sonic-garden`, `qa/ci`, `qa/coord`;
  - added: `cleanup/repo-consolidation`, this sprint's integration branch, merged.

  Any of them can be deleted once the owner agrees.
- **Stashes:** the three listed above, untouched.
