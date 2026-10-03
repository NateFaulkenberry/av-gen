# Merge notes: live/optimizer + proto/sonic-garden -> main (2026-10-03)

Resumable cold. Worktree `../av-gen-merge`, branch `merge/live-sonic`, made from `origin/main` (b3862d23).
Assets: per-file symlinks from `../av-gen/assets` (never commit anything under `assets/`).

## Steps
1. DONE `git merge --no-ff live/optimizer` (contains live/quality) -> 2a1217b6. Clean.
2. DONE `git merge --squash proto/sonic-garden` -> 38dcc348. Conflicts and resolutions are in that commit's message
   (README rows, three renderer includes, composition camera near/roll, post_settings wave/glitch/outline,
   live_panel "Input gain" width).
3. TODO fresh build: `cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release && cmake --build build/release -j 10`.
4. TODO both full suites under `tools/gpu-lock.sh`, one after the other, detached with nohup; read each binary's exit
   code with its summary header (docs/testing.md).
5. TODO when both exit 0: `git push origin HEAD:main` (must fast-forward from origin/main), then push
   live/optimizer and live/quality.

## Status
(updated below as steps complete)
