# Why Glowmere Valley 3 ran at 3 FPS, and what it costs now

The QA pass's performance findings (W1), written for the owner. The working notes with every table and
command are in `docs/qa-pass/perf.md` on branch `qa/perf`. All numbers are from this machine, headless
at 1280x720 unless stated, the median of 3 interleaved runs, with no other `avgen` process running.

## The short answer

- **The collapse was an engine regression, not the project and not the renderer.** Since ADR-834 (merged
  2026-09-25), every frame traced a line of sight from the camera to every character in shot, stepping over
  the ground every 2 m, to publish a `character.<name>.visibility` signal that no project reads. A wide
  shot has many characters far away, so the cost grew with exactly what a wide shot has. Across GV3's 73
  shots this cost correlates with (sightlines x distance) at r = 0.975.
- **Fixed** on `qa/perf` by ADR-950 (each step of the line asks the ground for two numbers instead of a
  full terrain sample) and ADR-951 (the line is only traced for a character whose visibility something
  actually asks for).
- **What is left is ordinary renderer cost.** A fixed GV3 wide runs at 31-33 FPS at 720p and 43-52 FPS at
  360p, and it is limited by the GPU drawing the valley's foliage.

| GV3 r7b shot | before (`main`) | after (`qa/perf`) |
|---|---:|---:|
| s66 "The veil lit" (the worst wide) | 376 ms, 2.7 FPS | 32.2 ms, 31 FPS |
| s24 "The valley floor lit, low and wide" | 297 ms, 3.4 FPS | 30.8 ms, 32 FPS |
| s26 "The veil at the water's edge" | 334 ms, 3.0 FPS | 31.7 ms, 32 FPS |
| s27 "Under the elder: the gold gills" (close-up) | 23.8 ms, 42 FPS | 15.8 ms, 63 FPS |
| s17 "The cairn on the spur" (no characters) | 26.5 ms, 38 FPS | 26.5 ms, 38 FPS |
| GV2 multicam, Valley Wide | 64.2 ms, 16 FPS | 27.7 ms, 36 FPS |
| grove (small scene, for scale) | 13.2 ms, 76 FPS | 13.2 ms, 76 FPS |

In the live editor the same cost applied **even while paused**: a paused wide spent 75 ms of every frame on
it, which is why navigating felt slow. After the fix the paused wide is vsync-limited (16.7 ms).

## The three hypotheses

**1. Is GV3 simply too complex, or at the renderer's limit? Not for the collapse; yes for what remains.**
GV3's world is GV2 multicam's world almost unchanged: same 18 scatter layers and 197,100 instance cap,
same meshes, same lights. With the fix, the wide's frame is GPU-bound at about 24 ms of GPU at 720p, and
19.5 ms of that is the main scene pass (614k triangles and 2,415 plant instances on the wide against 135k
and 20 on a close-up). No optional feature owns it: switching off shadows, AO, volumetrics, post, water,
transparency, particles, animation or the ecology lights one at a time each saves 0.6-1.9 ms. The scene pass
scales with both geometry and pixels (about 9 ms fixed plus about 10 ms per megapixel), so **by
extrapolation, not measurement**, a GV3 wide at 1920x1080 would be near 24 FPS on this machine. The levers
are content (scatter density and view distance on the wides) or renderer work beyond a QA pass.

**2. Is there bad or stale state in the project? No.** Ten copies of GV3, each with one category of state
removed (the 26 ground-pulse effects, all effects, staging and directing plans, all 81 routes, the 44
automation tracks, the scout, the hidden 32k-particle beam pools, GV3's ground-pool settings), measured
inside the spread of the untouched copy on the fixed build. Only removing the characters themselves moved
the old build, because that removed what the sightline was traced to. A static audit found no duplicate,
orphaned or dead state.

**3. Did the engine regress? Yes, in three steps, measured on the same GV2 multicam file on older builds:**

| build | frame | engine update | GPU |
|---|---:|---:|---:|
| `3e09f9e1`, before ADR-834 | 25.6 ms | 3.2 ms | 17.2 ms |
| `29e6918e`, ADR-834 merged | 43-47 ms | 22-24 ms | 17.2 ms |
| `0b623b88`, ADR-893/894 merged | 59 ms | 38.8 ms | 16.8 ms |
| `77ea4247`, main | 62-66 ms | 37-41 ms | 19.1 ms |
| `qa/perf` (ADR-950 + 951) | 27-28 ms | 3.3 ms | 19.2 ms |

- **ADR-834** added the per-frame sightline (+20 ms here; on GV3's worst wide it was most of a 348 ms engine update). Fixed.
- **ADR-893/894** (continuous path levels, blended water surfaces) made each terrain height query dearer,
  so each sightline became 1.8x as expensive. ADR-951 takes the sightline out of the frame, and with it this
  cost: the fixed build's engine update is back to 3.3 ms against 3.2 before ADR-834.
- **The GPU is +1.9 ms (+11%)** at identical geometry. 1.3 ms of it is ADR-945's glow pools (fewer ecology
  lights, but each lighting a pool twice as wide), which is the look you asked for and not a defect. The other
  ~1.1 ms crept in across several merges between `0b623b88` and `22ce5c3d`, the largest single step
  +0.5 ms at the emission merge (ADR-903 to 906); each part is under 0.5 ms and is the shading cost of
  features the revision added (perf.md section 6).

## One more editor finding: a two-frame hitch about 2.6 times a second

While a project has no unsaved changes, the editor serialises the whole project every ~380 ms to check
whether it has become dirty (ADR-440). On GV3 that takes about 36 ms on the UI thread, so the editor drops
two frames about 2.6 times a second, paused or playing. It stops once the project has an unsaved change.
This was a deliberate design (it bounds the duty cycle to 10%), so it was not changed in the QA pass. See
the decision below.

## Decisions for you

- **The unsaved-changes check's hitch (ADR-440).** Options: skip the check while the transport is playing;
  run the comparison off the main thread; or compare a cheaper digest of the project. Or accept it.
- **`--ui-ab` ignores `--start-at`** (a tooling note, not user-facing): the navigation benchmark played from
  0 s instead of holding the requested shot. Worth fixing if that benchmark is used again.

## What changed on `qa/perf`

- ADR-950 (`edf62e2d`): a sightline asks the ground for two numbers, not a full sample. Bit-identical
  output. Plus the measurement tools (`tools/perf_sweep.py`, `tools/scene_complexity_report.py`,
  `tools/make_gv3_state_arms.py`).
- ADR-951 (`97925c18`): a character's visibility is traced only once something asks for it; an unread
  visibility reads 0. Revert that one commit to restore the old behaviour.
- Tests on the head build: the full CPU suite (`avgen_tests`) exit 0, 3,843 cases, 3,823 passed, 19
  skipped, 1 failed as expected (the known `[!shouldfail]` slope-lean case); the GPU suite
  (`avgen_render_tests`) exit 0, 549 cases, 548 passed, 1 skipped (texture sharing).
