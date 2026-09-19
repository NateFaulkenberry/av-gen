# Current Task

## Objective
Two owner-priority tracks, both running as agents in worktrees:

1. **`glowmere-valley-2-multicam` performance regression** (priority). The owner reports it "has
   ground to a halt" since the project was ported to the new animation system. Also: get the
   animations behaving correctly on that system.
2. **Tree of Life COSMIC floating island art pass** (parallel). 26-phase owner spec: tree wind,
   falling leaves, tree energy, canopy shimmer, tree particles, and a large world-space cosmic
   vortex below the island. The owner has said this is likely the only Tree of Life variant they
   will develop further.

## Why
Both are the owner's stated priorities as of 2026-09-19 ~10:00. The cinematic key-light work is
**paused by owner instruction at its section 16** and is merged (`483cdaaa`); sections 17-20 are not
done and are not to be continued without the owner saying so.

## Current Status
- `agent/mcperf` -- performance agent, running.
- `agent/cosmicart` -- Tree of Life cosmic agent, running.
- Main is at the cosmickey merge plus the Aurora cleanup. Three test failures in
  `test_treeisland_example.cpp` are being fixed by the supervisor (build in flight).

## The strongest open lead on the performance regression
**An app save silently drops the baked camera.** Observed live: at 10:07 the running app wrote
`glowmere-valley-2-multicam.json` and the save removed two entire top-level keys --
`cameraAimFollow` (37 entries) and `cameraShotSpans` (42 entries), the bake from `9433044d`.
`parameters` went *up* 5502 -> 5530, so it is not a truncated write. Restored from git.

Both versions kept for comparison:
- `<scratchpad>/multicam-committed.json`   (good, has the bake)
- `<scratchpad>/multicam-app-save-1007.json` (the drop)

If the app destroys the bake on every save, the camera necessarily falls back to deriving the cut
live, which is a plausible cause of "ground to a halt" and fits "after I asked you to fix animation"
exactly. **This is a hypothesis, not a finding.** It must be shown that the absence of those keys
actually costs frame time; if it does not, it is still a serious correctness bug but not this bug.

## Owner decisions taken today
- **ADR-091 is relaxed for particle systems**: scrub need not exactly replay particle animation.
  Owner's words: "I think we can ease the scrub must exactly replay particle animations yeah?"
  Still required: a render must be reproducible, and the relaxation does not extend to tree wind,
  shimmer, energy or vortex density, which stay pure functions of time.
- `glowmere-valley-2.json`'s 47 dead `atmos/Aurora/` parameters: **dropped**, not restored. The base
  cut has no aurora, deliberately; the multicam cut does.
- `nodes/tree-foliage/emissiveBoost` **left at the owner's 2.2**, not the key-light branch's 0.5.
  Unresolved: 0.5 was the point of that branch (the foliage was lighting itself, so the key appeared
  to cast nothing). The cosmic-art agent is to settle it with a render.

## Known Problems
- **A render is evidence about the binary that produced it.** Build `--target avgen` explicitly; an
  agent lost an hour to byte-identical pairs from a stale `src/avgen`.
- **Reconfigure CMake after every merge** (`cmake -S . -B build/release`). The test glob is evaluated
  at configure time, so an incremental build omits test files a merge added and the suite passes
  without compiling them.
- Do not `pkill -f avgen_tests`; it kills other agents' runs. Copy to a distinct name and
  `codesign -s - -f` it, or the kernel SIGKILLs the copy silently.
- The owner edits projects in the running app while agents work. A file can change under you.

## Tests
Main baseline **2436 cases**. One `[!shouldfail]` by design (`test_character_lab_slopes.cpp`,
ADR-260). Three `test_treeisland_example.cpp` failures are the supervisor's to fix and are not a
regression in anyone's branch.

`python3 tools/check_project_integrity.py` -- new, read-only, exits non-zero. Checks stale scene
fingerprints and `worldfx/`|`atmos/` parameters naming no effect. Run it before every commit that
touches `examples/`.

## Next Action
Finish the `test_treeisland_example.cpp` fix (build in flight), run the suite, confirm 2436 green
minus the one expected failure. Then await the two agents.

## Last Updated
2026-09-19 11:05
