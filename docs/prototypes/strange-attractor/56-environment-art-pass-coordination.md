# Environment Art Pass (Pass 3): coordinator split and rules

The owner's brief, verbatim, is `55-environment-art-pass-brief.md`. It was given on 2026-11-03, before the full M3g
render. **Hold the full M3g render**: this pass comes first, and the full render happens at the end of it.

## The owner's overriding instruction

> "be sure to preserve all of what we have now in terms of portal animations, camera paths, general visual ideas -
> dont introduce any regressions in the scenery"

- Everything approved up to M3g stays:
  - every portal animation and burst;
  - the camera paths and the §1 camera rule in 54- (look where you travel, no hero shots, characters glimpsed);
  - the timing (Mars four bars shorter);
  - AgX for the whole film;
  - the night lighting the owner likes;
  - the craft by the alien with its beat pulse;
  - the hero assets (H2 alien, Sasquatch v1, campfire v1, UFO v1, octopus-v1);
  - the ocean's base illumination and the covert base rework;
  - the beat pulses and the Earth aura.
- Where the brief's text could be read as a redesign (for example "substantial environment redesign" for Mars and
  underwater), it means the environment art (geology, materials, atmosphere, lighting) inside the existing
  journey. It does not mean new camera paths, new portal positions or a new composition of the legs.
  - If a geological change would put geometry in a camera path or a portal opening, move the geology, not the
    camera.
- **Regression guard:**
  1. Before any change, render a fixed set of journey frames at the start of the pass (at least 4 per leg,
     plus every portal crossing at ±0.5 s) as the "pass-3 baseline".
  2. After each phase, re-render the same frames and compare them side by side.
  3. Anything that got worse outside the environment being worked on is a regression; fix it before
     moving on.
  4. Re-run the per-frame camera clearance and forward-vs-velocity traces at the end. They must not get worse
     than M3g.

## Two agents, split by ownership (never the same files at once)

**SA agent** (scene/strange-attractor, `../av-gen-attractor`)
- **Brief role:** "Agent B" (the renderer and Cinematic Critic audit), plus the scene integrator.
- **It owns:**
  - all engine and renderer code (shaders, post, fog, materials, new ADRs);
  - the shared scene and timeline files (`strange-attractor.json`, cameras, portals, the looks and post);
  - the Ocean (Phase E), continuing its in-progress M3g ocean build;
  - the Moon (Phase F);
  - Phase G (common post and material refinement);
  - the Cinematic Critic baselines and the regression guard;
  - the remaining M3g §6 items.

**Hero agent** (scene/sa-hero, `../av-gen-sa-hero`)
- **Brief role:** "Agent A" (the environment and asset audit, and the art direction document), then the
  Blender environment artist.
- **It owns:**
  - the canyon day benchmark (Phase B);
  - the campfire hero clearing and the nearby canyon walls (Phase C);
  - Mars (Phase D);
  - their geometry, materials, Blender sources and builders, and their world scene files (the canyon and
    Mars `.scene.json` and builders).
- **First:** rebase or reset its worktree onto the SA agent's current HEAD, so it starts from M3g. Coordinate
  through the coordinator.
- **It does not edit** engine code or the shared timeline, camera or portal files. When it needs an engine
  feature or a shared-file change, it writes the request into `57-env-pass-requests.md`, and the SA agent
  implements it.

**Merging:** the hero agent's branch merges into scene/strange-attractor at the end of each of its phases. The
coordinator calls the merge; the SA agent performs it and re-runs the regression guard. Because ownership of the
world files is split, these should be clean merges.

## Process

- **Cinematic Critic** (brief §2): the existing service lives in `~/Documents/GitHub/creative-critic`. Its CLI is
  `~/Documents/GitHub/creative-critic/.venv/bin/critic`; it is not on PATH.
  - Start it with `critic start --daemon` if it is down.
  - Use `critic submit --mode preview ... --wait` and `critic compare a b`.
  - Audit it as the brief asks. Don't build a new evaluator.
  - The Critic is evidence; the owner's eye wins.
- **Baselines:** the SA agent runs the baselines for all six environments, at establishing, traversal and hero
  distances, and shares the camera set with the hero agent, so both use the same cameras.
- **Order:**
  - **Phase A runs in parallel:** the hero agent does the environment audit and the art direction doc, and
    the SA agent does the Critic and renderer audit plus the baselines.
  - **Then one stop for the owner:** the art direction doc, the plan and the baseline results.
  - **After the owner's OK:**
    - the hero agent does B, then C, then D;
    - the SA agent does E, then F, alongside;
    - G comes last, together.
- **ADRs:**
  - the SA agent uses 1910–1919;
  - the hero agent uses 1920–1929, through requests (it does not write engine code itself).
  - If either runs out, ask the coordinator.
- **Media:** `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/`, in numbered folders with exact file names:
  - `00-baseline/`
  - `01-audit-and-direction/`
  - `02-canyon-day/`
  - `03-camp/`
  - `04-mars/`
  - `05-ocean/`
  - `06-moon/`
  - `07-common/`
  - `08-regression/`
  - `10-full/` (the final full render with sound)
- **Standing rules:**
  - never commit third-party assets;
  - free, licence-logged assets only;
  - send the coordinator the URLs of any download that needs a login;
  - all GPU work through `tools/gpu-lock.sh`;
  - no gore;
  - no compat shims;
  - don't run the suites or merge to main until the owner approves;
  - one research agent at a time, and no helper subagents.
