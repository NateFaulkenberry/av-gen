# A Windows self-hosted runner for AV Gen

*2026-10-14, repository cleanup. This guide is based on reading the repository. Nothing here has been run on the
Windows machine. Every recommendation carries one of three labels:*

- **Verified**: shown by a build, test or workflow run.
- **Ready to configure**: grounded in the repository, but not yet tried on Windows.
- **Requires investigation**: blocked by unknown hardware, dependencies or backend behaviour.

## Summary

- **Can the existing workflows use Windows today? No.**
  - Every job in `ci.yml` and `sanitizers.yml` builds or runs AV Gen with Apple clang and Xcode 26 on `macos-26`,
    except the summary `report` job, which uses `ubuntu-latest`.
  - AV Gen has never been built on Windows. `docs/build.md` says "Windows/Linux: architecturally supported … Not
    built yet". **Verified** by inspection.
- **Is Windows behind the current CI failures? No.** They were caused by:
  - generated test inputs missing on a clean checkout (`rift.scene.json`);
  - numpy missing on the hosted runner;
  - a song that is on no runner.

  All three are macOS-side problems, fixed in this cleanup (`ci-and-test-report.md`).
- **Best first jobs for the PC:**
  1. A connectivity job.
  2. Platform-independent validators (Python, no build).
  3. An *informational* MSVC configure that reports the first porting gap.

  The workflow is checked in as `.github/workflows/windows.yml`. It is opt-in and manual. **Ready to configure.**
- **What must stay on macOS:**
  - all of the gating CI: build, the CPU suite, the asset tier and the sanitizers;
  - everything that exercises Metal: the renderer, `avgen_render_tests`, Syphon, CoreMIDI, CoreText and
    AVFoundation video.

  Windows adds *portability* coverage. It is not a substitute for any of these.
- **Cost:** standard GitHub-hosted runners are free for public repositories, so the PC saves no money. It adds
  coverage the hosted macOS jobs cannot give (MSVC, D3D12). The PC is also not subject to GitHub's limit on
  concurrent macOS jobs, which is a capacity point rather than a cost one.

## 1. The current CI, and what could run on Windows

| Workflow / job | Runner | Platform-specific because | On Windows |
|---|---|---|---|
| `ci.yml` Build (Release) | `macos-26` | Xcode 26.6, Apple clang, the macOS-arm64 Dawn archive, ObjC++ (`*.mm`) | after porting only |
| `ci.yml` CPU tests (avgen_tests, full) | `macos-26` | the macOS binary; POSIX in ~26 test/tool files (`fcntl`, `flock`, `unistd`) | after porting only |
| `ci.yml` CPU tests with private assets | `macos-26` | as above, plus the private asset secret | **never**: no secret on the PC |
| `ci.yml` GPU tests (hosted, informational) | `macos-26` | Metal (paravirtual) | no (it tests Metal) |
| `ci.yml` GPU tests (self-hosted Apple GPU) | `[self-hosted, macOS, ARM64, avgen-gpu]` | Apple GPU + Metal + private assets | no |
| `ci.yml` Report | `ubuntu-latest` | none | could, but it is free and fast where it is |
| `sanitizers.yml` ASan / UBSan / TSan | `macos-26` | clang sanitizers on the macOS build | no |

Workload categories:

| Workload | Feasible on the PC? | Value | Label |
|---|---|---|---|
| Source-level checks: JSON, the examples index, `tools/check_project_integrity.py` | yes, Python only | low: hosted Ubuntu does it just as well, for free | Ready to configure |
| Formatting and linting | the repository has no clang-format or clang-tidy gate today | none until one exists | none |
| Windows compile and link (MSVC, C++23) | needs porting work first (§2) | **high**: it catches non-portable code the moment it lands | Requires investigation |
| Platform-independent CPU unit tests | after the compile works | medium: a second compiler and standard library | Requires investigation |
| Asset and scene-manifest validation | yes (Python) | low (as the first row) | Ready to configure |
| Graphics smoke test (headless D3D12) | after porting, if the GPU supports D3D12 feature level 12_0 | medium: proves the Dawn D3D12 path runs at all | Requires investigation |
| GPU rendering and visual regression | not advised on a low-end GPU | the baselines are Metal's; a D3D12 image is a different image | not recommended |

## 2. Building on Windows: requirements and the known gaps

What the repository needs, from `CMakeLists.txt`, `cmake/Dependencies.cmake`, `cmake/Warnings.cmake` and `src/`:

- **Compiler:** C++23 (`CMAKE_CXX_STANDARD 23`). MSVC from Visual Studio 2022 17.10 or newer is the realistic
  choice, and `cmake/Warnings.cmake` already has an MSVC branch (`/W4 /permissive- /utf-8`). clang-cl is an
  alternative. **Requires investigation**: the code base has only ever met Apple clang, and the C++23 library
  features it uses have not been audited against MSVC's STL.
- **Build tools:** CMake 3.28 or newer (4.0.1 tested on the Mac), Ninja, Git, and Python 3 for the generators.
- **Dependencies**, all fetched by CPM (fmt, spdlog, glm, nlohmann_json, miniaudio, kissfft, fastgltf, meshoptimizer,
  stb, tinyexr, embree, Catch2, SDL3, imgui, implot). All are portable.
  - OIDN is optional (`AVGEN_PATHTRACE_DENOISE`) and is only wired for the macOS arm64 archive.
- **Dawn (WebGPU):** today only the macOS arm64 prebuilt is wired (`cmake/Dependencies.cmake:209`). On Windows,
  CMake falls through to the from-source path. That path has never been exercised on any platform and builds slowly.
  - The pinned release does ship a Windows archive, checked with the GitHub API on 2026-10-14:
    `Dawn-c4e47b5eddc06f271cb07c3108cfccb1bb4704ec-windows-latest-Release.tar.gz`, 118,022,397 bytes,
    `sha256:f0a315b242ea06d80f2955925e4f59929657c6d4fcfb68a98e5c813c75f6f881`.
  - The first porting step is a `WIN32` branch beside the Apple one. **Ready to configure**; whether the archive's
    CMake package works with AV Gen's link line is **Requires investigation**:

    ```cmake
    elseif(NOT AVGEN_DAWN_FROM_SOURCE AND WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        CPMAddPackage(
            NAME dawn_prebuilt
            URL "https://github.com/google/dawn/releases/download/${AVGEN_DAWN_RELEASE}/Dawn-${AVGEN_DAWN_COMMIT}-windows-latest-Release.tar.gz"
            URL_HASH SHA256=f0a315b242ea06d80f2955925e4f59929657c6d4fcfb68a98e5c813c75f6f881
            DOWNLOAD_ONLY YES)
        # then the same find_package(Dawn) / target wiring the Apple branch uses
    ```

- **Graphics backend:** `gpu::Context` (`src/gpu/context.cpp`) has two paths.
  - **Window surface:** created only from a Metal layer (`SurfaceSourceMetalLayer`), and `platform::Window` creates
    the layer with `SDL_Metal_CreateView`. The windowed app on Windows needs a `SurfaceSourceWindowsHWND` branch fed
    from SDL's `SDL_PROP_WINDOW_WIN32_HWND_POINTER`.
  - **Adapter:** the backend is forced to Metal only under `__APPLE__`. Elsewhere Dawn chooses, which on Windows
    means D3D12, else Vulkan or D3D11. A headless run (the GPU tests, `--render`) creates no surface, so it does not
    need the HWND work, only a compile.

  **Requires investigation.**
- **Platform code that will not compile as is:**
  - About 18 `src/` files use POSIX headers or calls (`unistd.h`, `sys/*`, `fcntl.h`, `popen`), for example
    `src/app/application.cpp` (`getpid`), the live-profile and director runners, `src/sonic/live.cpp` and
    `src/control/osc.cpp`.
  - About 26 test and tool files do the same, for example `tests/integration/test_rift.cpp` (`flock`) and
    `tests/support/per_process_tmpdir.cpp`.
  - Already handled:
    - the ObjC++ files are excluded off Apple (`src/CMakeLists.txt`, `if(APPLE)`);
    - MIDI falls back to `src/control/midi_stub.cpp`;
    - `src/assets/video_writer.cpp` and `src/share/ndi_runtime.cpp` already carry `_WIN32` branches.

  **Requires investigation** (a porting task, estimated medium).
- **Audio:** miniaudio picks WASAPI on Windows with no code change. Live input is untested there.
- **Assets and environment:**
  - The tests read `AVGEN_SOURCE_DIR`, which is compiled in.
  - Gitignored assets are linked per checkout (`tools/link-worktree-assets.sh`, a bash script; Windows needs an
    equivalent, or Git Bash).
  - The PC should **not** receive the purchased packs or the owner's songs. Their cases SKIP, as they do on hosted
    runners.

## 3. Setup on the Windows machine (the owner does this)

Do the stages in order and stop at the first that fails.

### 3.1 Prerequisites

| Item | Version / note | Why |
|---|---|---|
| Windows 10 22H2 or Windows 11, x64 | | the runner and MSVC |
| Visual Studio 2022 Build Tools, 17.10+ | workloads: *Desktop development with C++*; components: MSVC v143 x64, Windows 11 SDK (10.0.22621 or newer), C++ CMake tools | C++23 compiler, SDK |
| CMake 3.28+ | the VS component, or cmake.org's installer on `PATH` | configure |
| Ninja | the VS component or `winget install Ninja-build.Ninja` | the generator CI uses |
| Git for Windows | then `git config --system core.longpaths true` | deep CPM paths exceed 260 characters |
| Python 3.12 (python.org, "Add to PATH") | `python -m pip install numpy==2.0.2` only if the CPU suite is ever run there | validators, generators |
| PowerShell 7 (`pwsh`) | `winget install Microsoft.PowerShell` | the workflow's shell |
| GPU driver | current vendor driver with D3D12 (feature level 12_0); check with `dxdiag` | the stage-4 smoke only |
| Disk | at least 40 GB free on the runner's drive | CPM cache (~3 GB), Dawn archive, build trees, the runner's work folder |
| Hardware details | **unknown to this guide**: GPU model, VRAM, CPU and RAM. Record them in the runner's description; the connectivity job prints the GPU | |

Enable Windows long-path support too: `HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled = 1`, then
reboot.

### 3.2 A restricted account for the runner

1. Create a local **standard** (non-admin) user, e.g. `avgen-runner`, with a strong password. It must have no
   Microsoft-account sign-in, no saved browser sessions and no access to your personal folders.
2. Give it a working folder on a non-system drive if possible, e.g. `D:\actions-runner`. Only that account and
   Administrators should have access.
3. Do not put SSH keys, `gh` tokens, cloud credentials or the private test-asset repository on this account.

### 3.3 Register the runner

The repository is owned by a user account (`NateFaulkenberry/av-gen`), not an organisation, so the runner is a
**repository-level** runner. Organisation runner groups are not available.

1. On GitHub: **Settings → Actions → Runners → New self-hosted runner → Windows, x64**. The page shows a download
   link and a one-time **registration token**, valid for one hour.
   - Never paste the token into a file, an issue, a commit or a chat.
2. In an elevated PowerShell (the service is installed with admin rights, but runs as the restricted account):

   ```powershell
   mkdir D:\actions-runner; cd D:\actions-runner
   # download and extract exactly the package and version the GitHub page shows, and check its SHA-256 there
   .\config.cmd --url https://github.com/NateFaulkenberry/av-gen `
                --token <TOKEN-FROM-THE-PAGE> `
                --name avgen-win-01 `
                --labels avgen-win `
                --work _work `
                --runasservice --windowslogonaccount .\avgen-runner --windowslogonpassword <password>
   ```

   - `self-hosted`, `Windows` and `X64` are added automatically.
   - **Do not add a GPU label yet.** Add `avgen-win-d3d12` only after stage 4 passes, so no job is routed to a
     capability that has not been shown.
3. Check that the runner shows **Idle** under Settings → Actions → Runners.
4. In GitHub: **Settings → Secrets and variables → Actions → Variables → New repository variable**:
   `AVGEN_WINDOWS_RUNNER` = `true`. Until this is set, `windows.yml` is skipped.

### 3.4 Repository security settings (GitHub)

- **Settings → Actions → General → Fork pull request workflows from outside collaborators:** choose *"Require
  approval for all outside collaborators"*. It is currently *first-time contributors*: `gh api
  repos/NateFaulkenberry/av-gen/actions/permissions/fork-pr-contributor-approval` returned `first_time_contributors`
  on 2026-10-14.
- Keep **Workflow permissions** at *read repository contents* (`ci.yml` and `windows.yml` already declare
  `contents: read`).
- `windows.yml` is `workflow_dispatch`-only and receives no secrets. Do not add `pull_request` or
  `pull_request_target` triggers to any job whose `runs-on` contains `self-hosted`.

## 4. Workflow integration

`.github/workflows/windows.yml` (added in this cleanup) is the whole integration:

- `on: workflow_dispatch` with a `stage` input: `connectivity`, `validators` or `configure`;
- `if: vars.AVGEN_WINDOWS_RUNNER == 'true'`;
- `runs-on: [self-hosted, Windows, X64, avgen-win]`;
- `permissions: contents: read`, no secrets, `persist-credentials: false`;
- the `configure` stage is `continue-on-error: true` and uploads `configure.log`. It is informational, and must not
  be made a required check until a Windows build exists.

When a Windows build exists, the next step is a `build` stage and then a gated `cpu-tests` stage. Each runs on
`push` to `main` only, never on `pull_request`:

```yaml
on:
  push:
    branches: [main]      # trusted code only: the owner's merges
  workflow_dispatch:
jobs:
  windows-build:
    if: vars.AVGEN_WINDOWS_RUNNER == 'true' && github.event_name != 'pull_request'
    runs-on: [self-hosted, Windows, X64, avgen-win]
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with: { persist-credentials: false, clean: true }
      - shell: pwsh
        run: |
          # enter the VS dev shell as in windows.yml, then:
          cmake -S . -B build/win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPM_SOURCE_CACHE="$env:RUNNER_TOOL_CACHE\avgen-cpm"
          cmake --build build/win
          build/win/tests/avgen_tests.exe "~[gpu]" --reporter junit::out=cpu.xml --reporter console
```

**Do not move** any existing macOS job to the PC. Do not make any Windows job a required status check until it has
passed on `main` repeatedly.

## 5. Validating the runner: the staged plan

Dispatch each stage with **Actions → Windows (self-hosted, opt-in) → Run workflow**, or with
`gh workflow run windows.yml -f stage=<stage>`.

| Stage | How | Passes when | Label |
|---|---|---|---|
| 0. Connectivity | `stage=connectivity` | the job runs; the log shows `runner: avgen-win-01`, the GPU name and the tool versions | Ready to configure |
| 1. Validators | `stage=validators` | `check_project_integrity.py` prints `0 problem(s)` and the index count matches `examples/index.json` | Ready to configure (path handling on Windows not yet seen) |
| 2. Configure | `stage=configure` | expected to fail at first: the log names the first gap (Dawn, §2). Each fix is a porting PR | Requires investigation |
| 3. Build + CPU tests | a new stage, after porting | `avgen_tests.exe "~[gpu]"` exits 0; SKIPs are named in the output | Requires investigation |
| 4. Headless D3D12 smoke | `avgen.exe --example "The Temple" --render out --range 5:5.02 --size 640x360` | exit 0 and a non-black PNG (`tools/review_examples.py` performs the same check) | Requires investigation |

**Confirming that a run used the PC:**

- the job header shows `Runner name: avgen-win-01`;
- the connectivity step prints `RUNNER_NAME` and the GPU;
- `gh run view <id> --json jobs --jq '.jobs[].runnerName'` returns it;
- artifacts appear under the run (`windows-configure-log`).

## 6. Maintenance

- **Updates:** the runner updates itself. If it stops picking up jobs, check the service (`Get-Service
  actions.runner.*`) and `_diag\Runner_*.log`.
- **Disk:**
  - `_work` keeps checkouts and build trees, and `clean: true` resets the checkout each run.
  - Delete `_work\*\build` monthly, or add a cleanup step.
  - The CPM cache lives in `RUNNER_TOOL_CACHE\avgen-cpm`; delete it to force a fresh download.
- **Disabling:** set `AVGEN_WINDOWS_RUNNER` to `false`. Every job then skips, and nothing else changes.
- **Removing:**
  1. On the PC, stop the service.
  2. Run `.\config.cmd remove --token <REMOVAL-TOKEN>`. The token comes from the same Settings page.
  3. Delete the runner folder and the `avgen-runner` account.
- **Ephemeral alternative:** `config.cmd --ephemeral` takes one job and then deregisters. It is safer, because no
  state survives between jobs, but it needs a re-registration script. A VM or Windows Sandbox snapshot reset per job
  is safer still. Consider either before ever running anything that is not the owner's own commit.

## 7. Security boundaries

- **Trust boundary:**
  - Code from the owner's own pushes to `main` (and manual dispatches of it) is trusted.
  - Code from a pull request, a fork above all, is untrusted. On a persistent self-hosted runner it executes as the
    runner account on a machine on a home network, and can persist, read anything that account can read, and probe
    the LAN.
  - GitHub's own advice is not to use self-hosted runners with public repositories, except for trusted code.
  - `windows.yml` therefore has no `pull_request` trigger, and any future job must keep `github.event_name !=
    'pull_request'`.
- **Least privilege:**
  - a standard account;
  - `contents: read`;
  - `persist-credentials: false`, so no token is left in `.git/config`;
  - no secrets, in particular never `AVGEN_TEST_ASSETS_TOKEN`: the purchased packs must not land on the PC;
  - no admin rights for the service account.
- **Never commit** a registration or removal token, a password, or a `.runner` or `.credentials` file. They live
  only in the runner folder on the PC.
- **Network:** if the PC holds anything personal, put it on a separate network segment or VLAN, or use a
  VM-hosted runner.

## 8. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| The job stays *Queued* | the runner is offline (service stopped), or its labels lack `avgen-win`. Check the runner list and `_diag` |
| `windows.yml` job is *Skipped* | `AVGEN_WINDOWS_RUNNER` is not `true` |
| `config.cmd`: token expired | registration tokens last one hour. Get a new one from the Settings page |
| `cl.exe` not found / C++23 errors | the step did not enter the VS dev shell (see `windows.yml`), or Build Tools are older than 17.10 |
| CMake cannot find Dawn | expected until the `WIN32` prebuilt branch (§2) exists. The from-source path needs Python and a long first build |
| Paths over 260 characters fail in CPM or git | long paths are not enabled in the registry and in git (§3.1) |
| `unistd.h` / `fcntl.h` not found | the POSIX uses listed in §2: a porting task, not a runner problem |
| Python scripts fail with `UnicodeDecodeError` | Windows' default encoding. Run with `python -X utf8`, or set `PYTHONUTF8=1` in the job's `env` |
| D3D12 adapter not found in stage 4 | old driver, or no D3D12 FL 12_0 support. Update the driver; if the GPU cannot do it, keep stage 4 off |
| Antivirus slows the build badly | exclude the runner's `_work` and the CPM cache folder from real-time scanning |
