# Merge notes: live/optimizer + proto/sonic-garden -> main (2026-10-03)

Resumable cold. Worktree `../av-gen-merge`, branch `merge/live-sonic`, made from `origin/main` (b3862d23).
Assets: per-file symlinks from `../av-gen/assets` (never commit anything under `assets/`).

## Steps
1. DONE `git merge --no-ff live/optimizer` (contains live/quality) -> 2a1217b6. Clean.
2. DONE `git merge --squash proto/sonic-garden` -> 38dcc348. Conflicts and resolutions are in that commit's message
   (README rows, three renderer includes, composition camera near/roll, post_settings wave/glitch/outline,
   live_panel "Input gain" width).
3. DONE (exit 0, at 7c6cf669, which also merges live/optimizer's ADR-1107 follow-up 22fd7ab6) fresh build: `cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release && cmake --build build/release -j 10`.
4. DONE at 7c6cf669 both full suites under `tools/gpu-lock.sh`, one after the other, detached with nohup; read each binary's exit
   code with its summary header (docs/testing.md).
5. DONE when both exit 0: `git push origin HEAD:main` (must fast-forward from origin/main), then push
   live/optimizer and live/quality.

## Status
- `avgen_tests`: binary exit 0 -- 4090 cases: 4070 passed, 19 skipped, 1 failed as expected (the known slope-lean shouldfail).
- `avgen_render_tests`: binary exit 0 -- 586 cases: 585 passed, 1 skipped.
- Pushed to main (fast-forward from b3862d23); live/optimizer (22fd7ab6) and live/quality (5fcd7ad4) pushed.
- Before pulling: settings keys changed. ADR-1083 removed `general.adaptiveCanvasScale` / `adaptiveCanvasBudgetMs`
  (replaced by `liveQuality`, `liveTargetFps`); ADR-1107 adds `general.liveFrameCap` (default true). A
  settings.json with the old 0.5 `adaptiveCanvasFloor` keeps Emergency at 0.5 until "Lowest scale" is lowered.
