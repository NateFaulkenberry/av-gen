# GPU CI and the private test assets

How CI gets the assets this public repository cannot hold, and how a real Apple GPU runs the GPU
suite. Written 2026-09-28 (QA pass, W2). **Nothing here has been switched on:** creating the private
repository, the secret and the runner are the owner's decisions, and the steps below are for the owner
to run. Until then every job described here is skipped, and the run summary says why.

## The constraints

- The repository is **public**. The alien models (`assets/aliens/`) and the farm animals (`assets/farm/`)
  are purchased, and the song `Rebuild.mp3` is copyrighted. None of them may be committed here, placed
  in an Actions cache, or uploaded as an artifact, and that includes a rendered frame of them.
- The hosted macOS VM's GPU ("Apple Paravirtual device") cannot compile the renderer's Metal pipelines:
  363 of 443 GPU cases fail there for reasons unrelated to the change under test. So the hosted GPU job
  stays **informational**, and an authoritative GPU verdict needs real Apple silicon.
- GitHub recommends self-hosted runners **only for private repositories**: anyone can fork a public
  one and open a pull request, and a pull request can change the workflow that chooses the runner.
  ([Secure use reference](https://docs.github.com/en/actions/reference/security/secure-use),
  [Adding self-hosted runners](https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/add-runners).)

## The design

```
 NateFaulkenberry/av-gen-test-assets (PRIVATE)            NateFaulkenberry/av-gen (public)
   assets/<same paths as here>                              tools/ci/test-assets.lock  (repo + commit)
   SHA256SUMS                                               tools/fetch-test-assets.sh (clone, verify, link)
   .github/workflows/gpu.yml  (the runner lives here) ───┐  .github/workflows/ci.yml
                                                         │    cpu-assets (hosted, nightly, secret)
 self-hosted Mac [self-hosted, macOS, ARM64, avgen-gpu] ◀┘    gpu        (self-hosted; opt-in, see below)
```

1. **A private repository holds the minimal asset set**, at the same relative paths as `assets/`
   here, with a `SHA256SUMS`. `tools/ci/make-test-assets-repo.sh` builds it from the owner's machine
   out of `tools/ci/test-assets.list`.
2. **This repository pins one commit of it** in `tools/ci/test-assets.lock`, so a test run is
   reproducible and an asset change is a reviewed commit here.
3. **`tools/fetch-test-assets.sh` is the only way in**, for CI and for a developer alike. It clones
   the private repository *outside* the checkout, checks out the pinned commit, verifies every file
   against `SHA256SUMS`, and symlinks each file to its path under `assets/`, **refusing any path git
   does not ignore** (a link on a tracked path is one `git add -A` away from publishing the asset).
   It fails with an `::error::` naming what is missing: the secret, the repository, or the pinned
   commit.
4. **Two jobs use it**, both on trusted events only:
   - `cpu-assets` in `ci.yml`: a hosted runner, nightly and on dispatch. The CPU suite with the assets,
     which is the Glowmere validation tier (the abduction films, the aliens, the multicam). It runs
     without the owner's machine. Its only credential is a read-only secret, which GitHub never gives
     to a fork's pull request, and the job is never started for `pull_request` at all.
   - the GPU suite on a **self-hosted Apple-silicon runner**, registered on the **private**
     repository (recommended), or on this one (possible, with the settings below).
5. **Nothing is cached or uploaded.** The clone lives in `$RUNNER_TEMP` on a hosted VM, which is
   discarded, and beside the workspace on the self-hosted Mac. The jobs upload text results only
   (JSON, Markdown, XML, logs). They upload no PNG, EXR or anything under `assets/`.

### Where the runner is registered

**Recommended: on the private repository.** Its `.github/workflows/gpu.yml`
(`tools/ci/private-repo-gpu-workflow.yml`, installed by the helper) checks out itself (the assets,
no secret needed) and av-gen's public code at a chosen ref, and runs the GPU suite and the CPU suite
with the assets. No one outside can reach the runner: the repository is private, has no forks, and
its workflows only check out code its owner points them at. The cost is that the results appear in the
private repository's Actions tab, not on av-gen's commits. It runs nightly and on dispatch
(`gh workflow run gpu.yml -R NateFaulkenberry/av-gen-test-assets -f ref=<branch>`). A trigger on every
av-gen main push would need a token in av-gen that can dispatch to the private repository; that is not
set up.

**Possible: on av-gen itself (`ci.yml`'s `gpu` job).** The job is gated three ways:
- `vars.AVGEN_GPU_RUNNER == 'true'`, set only once the runner exists. Without it a queued job would
  wait a day for a machine that does not exist;
- trusted events only: a push to main, the nightly schedule, or a dispatch. Never `pull_request`;
- it uploads text only.

**But a job's `if` cannot protect a runner from a pull request**, because a fork's pull request runs
the workflow file *from the pull request*, which can declare any job on any label. On a public
repository the only protection is the repository setting that makes GitHub wait for the owner's
approval before running any fork's workflow. If the runner is registered on av-gen, that setting is
**mandatory** (step 5b), and a pull request that touches `.github/` must never be approved to run.

Either way:
- Run the runner as **a dedicated macOS user** if the Mac is also a workstation. Metal needs that user
  logged in, so use fast user switching. Its jobs then cannot read the owner's home directory, keys or
  other checkouts.
- `tools/gpu-lock.sh` serialises GPU users through `$TMPDIR/avgen-gpu.lock`. A runner running as a
  different user has a different `$TMPDIR`, so it does not wait for the owner's own GPU runs. Either
  accept that (a CI result taken during the owner's GPU work is not evidence; `tools/gpu-lock.sh`
  says why), or give both the same `TMPDIR`.

## The minimal asset set

`tools/ci/test-assets.list`, **measured**: every file under `assets/` that the test binaries actually
opened on 2026-09-28, traced with a `DYLD_INSERT_LIBRARIES` open()/fopen() interposer over the whole
GPU suite (549 cases) and over the 280 CPU cases a hosted runner skips or fails for want of an asset.

| Tier | Needed by | Files | Size |
|---|---|---:|---:|
| 1 | `avgen_render_tests`: 5 aliens, 3 farm animals, 55 Quaternius meshes and textures | 63 | 65 MB |
| 2 | the Glowmere-tier `avgen_tests` cases: the rest of the aliens and farm, 55 more Quaternius, 55 Kenney city pieces, 15 nature, 1 HDRI | 133 | 44 MB |
| 2 | the Tree of Life island meshes (the tree-island cases only; optional) | 6 | 152 MB |

So the private repository is about **110 MB** without the island and 260 MB with it, against 1.4 GB in
`assets/`. It leaves out:
- `assets/audio/*.wav`: CI regenerates them;
- the songs (`~/Desktop/Rebuild.mp3`, `~/Desktop/MP3/bass.mp3`): the owner's decision;
- the motion packs 17 cases need (retargeted 100STYLE, the augmented scout pack and its database):
  they are not on the owner's machine either, so **no machine runs those 17 cases today**.

With every tier present locally, the 280 asset-bound CPU cases give 263 passed, 17 skipped (the
packs above), 0 failed, and the GPU suite 548 passed, 1 skipped, 0 failed.

## Owner's steps

Each step can be done alone; nothing runs until step 6.

1. **Build the private repository locally** (copies, never moves; creates no remote):
   ```sh
   tools/ci/make-test-assets-repo.sh ~/Documents/GitHub/av-gen-test-assets
   ```
   It copies `tools/ci/test-assets.list` out of `assets/` (following the worktree symlinks), refuses any
   path this repository tracks and any file over 100 MB (GitHub's limit), writes `SHA256SUMS`, a
   README that says the repository is private and not for distribution, and the runner workflow, and
   commits. It prints the commit sha. It deliberately **does not include `Rebuild.mp3`**: whether the
   song may go into a private repository is the owner's call (see "Decisions" below).
2. **Create it on GitHub, private, and push:**
   ```sh
   gh repo create NateFaulkenberry/av-gen-test-assets --private --source ~/Documents/GitHub/av-gen-test-assets --remote origin --push
   ```
   Check that it says **Private** (`gh repo view NateFaulkenberry/av-gen-test-assets --json visibility`).
3. **Pin it here:** put the printed sha in `tools/ci/test-assets.lock` (`commit <sha>`), commit, and push.
4. **Give av-gen read access**, for the hosted `cpu-assets` job. Either:
   - (preferred) a **deploy key**: scoped to one repository, read-only, and tied to no person.
     ```sh
     ssh-keygen -t ed25519 -N '' -C av-gen-ci-read -f /tmp/avgen-assets-key
     gh repo deploy-key add /tmp/avgen-assets-key.pub -R NateFaulkenberry/av-gen-test-assets --title av-gen-ci-read
     gh secret set AVGEN_TEST_ASSETS_SSH_KEY -R NateFaulkenberry/av-gen < /tmp/avgen-assets-key
     rm /tmp/avgen-assets-key /tmp/avgen-assets-key.pub
     ```
     (`gh repo deploy-key add` makes a read-only key unless given `--allow-write`.)
   - or a **fine-grained personal access token** with *Repository access: only
     av-gen-test-assets* and *Contents: Read-only*, stored as `AVGEN_TEST_ASSETS_TOKEN`
     (`gh secret set AVGEN_TEST_ASSETS_TOKEN -R NateFaulkenberry/av-gen`). It expires; renew it.

   The secret is never in a workflow file or a log: the script passes a token as a one-command
   HTTP header, masked, and writes a key to a mode-600 temporary file removed on exit.
5. **Turn the jobs on:**
   - a. the hosted CPU-with-assets job: `gh variable set AVGEN_TEST_ASSETS -R NateFaulkenberry/av-gen --body true`;
   - b. only if the runner will be registered on **av-gen**: *Settings → Actions → General → Approval
     for running fork pull request workflows from contributors → Require approval for all external
     contributors*, then `gh variable set AVGEN_GPU_RUNNER -R NateFaulkenberry/av-gen --body true`.
6. **Register the runner** on a Mac with Apple silicon and macOS 26 (the Dawn archive's floor):
   - Xcode, CMake and Ninja (as for local development), plus `brew install ccache`, and Python 3;
   - on GitHub, go to the repository that will own it (the private one, recommended), then *Settings →
     Actions → Runners → New self-hosted runner → macOS, ARM64*. Follow its download lines, then
     configure it with the label the workflows ask for:
     ```sh
     ./config.sh --url https://github.com/NateFaulkenberry/av-gen-test-assets --token <from that page> \
                 --name <mac-name> --labels avgen-gpu --work _work
     ./svc.sh install && ./svc.sh start      # a LaunchAgent: the user must be logged in for Metal
     ```
   - check it: `gh workflow run gpu.yml -R NateFaulkenberry/av-gen-test-assets -f ref=main`, then
     `gh run watch -R NateFaulkenberry/av-gen-test-assets`.
7. **Locally**, a developer gets the same set the same way:
   ```sh
   tools/fetch-test-assets.sh           # clone to ~/.cache/avgen-test-assets, verify, link into assets/
   tools/fetch-test-assets.sh --check   # say what would be linked, link nothing
   ```
   It uses the developer's own git credentials (an ssh key in the agent, or `gh auth setup-git`), and
   only fills gaps: a real file already in `assets/` is left alone. The owner's checkout already has
   every asset and does not need it; `tools/link-worktree-assets.sh` still covers worktrees.

## Updating the set

Edit `tools/ci/test-assets.list`, re-run step 1 (it commits a new version into the same local
repository), push it (`git -C ~/Documents/GitHub/av-gen-test-assets push`), and update the lock's sha in
the same av-gen commit as the change that needs it.

## Decisions for the owner

- **The song.** 41 CPU cases skip on CI without `Rebuild.mp3` (the multicam project's audio). A
  private repository is not distribution, but this pass does not decide that for a copyrighted file.
  To include it, add it to the list and teach the fetch script to link it where the projects look
  (`~/Desktop/Rebuild.mp3`, or `AVGEN_REBUILD_AUDIO`).
- **Where the runner lives**: the private repository (recommended) or av-gen (needs 5b).
- **The farm models in git history.** `4a138886` removed them from the tree, but the nine GLBs remain
  in this public repository's history (added in `4bdc42ff`). Removing them from history means
  rewriting it (`git filter-repo`) and force-pushing, which breaks every clone and worktree. That is
  the owner's call; nothing in this pass touches history.
