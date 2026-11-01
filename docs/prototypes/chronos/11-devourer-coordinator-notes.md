# CHRONOS: The Devourer, coordinator notes

The governing brief is `10-devourer-brief.md` (the owner's, verbatim). It replaces the temporal-canyon
art direction. These notes add the working rules and the stops.

## What carries over from Gate 1, and what goes

- **Keep the temporal engine:** ADR-1931 (ring capacity, routable span, stride, metadata, the composed
  sampler), ADR-1932 (render pre-roll), the debug-capture fix, and the shared RTIN mesher (ADR-1930,
  already on main and used by Strange Attractor). This is the "existing working slit-scan" the brief
  means; reuse it.
- **Remove the canyon:** the canyon scene is abandoned. Per ADR-441 (no compat shims), remove the
  `chronosCanyon` block, its generator, its scene files and its tests in a clean commit, and mark
  ADR-1933 superseded. Git history keeps it. Make that the FIRST commit of this pass, so the temporal
  engine can merge to main without dead canyon code.
- The brief's §3.x temporal-effect plan (fracture, displacement, accumulate feedback) carries over only
  where the Devourer needs it: the mouth's feedback and the souls' fragmentation. Build what this scene
  needs, nothing speculative.

## Stops (stop and hand back at each one; the owner reviews)

- **Gate D0: references, recon and plan. STOP.**
  - Review all 15 reference images in `~/Desktop/chronos/`. Classify each as creature, temporal effect
    or atmosphere, and say what is useful in each.
  - Write `12-devourer-recon-plan.md`. Cover:
    - the creature route (sculpt in Blender headless, or adapt a free base), and how it holds up close;
    - jaw and eye rigging through the existing skinned pipeline;
    - the soul figures (MPFB/MakeHuman CC0 is proven in Strange Attractor; see its `assets-pass2.md`);
    - how to localize temporal effects per object: masks, isolated rendering or depth-aware, with
      cost;
    - the mouth treatment;
    - the background;
    - the drum-stem signals (`stem.*` / `role.drums.*`; Feline Footwear has a Drums stem);
    - budgets and risks.
  - Include 2-3 quick creature look-dev stills from Blender, so the owner sees the direction early.
- **Gate D1: Phase 1, the creature art pass (Test A).** The Chronos portrait in AV Gen: lit, with
  subtle eye and jaw life. The owner judges the face before anything else gets built. STOP.
- **Gate D2: Phases 2-3** (the procession and a single-soul consumption: Tests B and C). STOP.
- **Gate D3: Phases 4-6** (multiple souls, background, drums, polish, profile: Tests D, E and F), with
  a full render with sound and both suites at exit 0. STOP, ready for merge.

## Rules

- Same worktree and branch (`../av-gen-chronos`, `scene/chronos`). ADR range continues from 1934 up to
  1969.
- Media goes in `~/Desktop/av-gen-review/104-chronos/devourer-d0/`, then `devourer-d1/` …
  `devourer-d3/`. Give exact file names in each hand-back.
- **Reference images** in `~/Desktop/chronos/` are the owner's, and likely copyrighted, apart from
  the Goya. Look at them, describe them and learn from them, but never copy them into the repo or
  commit them. Treat that folder as read-only.
- **Assets:** free assets from clearly licensed sources only, logged in `docs/prototypes/chronos/assets.md`.
  Keep them in a gitignored path and never commit them; the repo is public. Stop and ask if a download
  needs a login or the licence is unclear. Blender is at /Applications/Blender.app; run it headless
  with a private user directory, the same as the Strange Attractor agent does.
- **Content:** disturbing through expression, anatomy, scale and movement. No gore, blood spray or
  graphic violence (brief §2.1). The souls have neutral, minimal clothing and are never sexualised.
- **Audio:** drum stem only. No kick/snare/hat classifiers. The scene must work with audio off.
- Every avgen/GPU run goes through `tools/gpu-lock.sh`; the GPU is shared with the Strange Attractor
  agent. Never `write --add` to the live baseline. The scene stays out of `examples/index.json`.
- **Suites:** run them in your clean CI worktree, as in Gate 1. The exit code is the only signal.
- One agent, no subagents. Keep `PROGRESS.md` current.
