# Musical Geography: coordinator split and rules (2026-11-08)

The owner's specification, verbatim, is `00-brief.md`. Where this file and the brief disagree, this file wins
(standing project rules).

## The owner's references

`~/Desktop/musical geography/` (read-only; **never copy them into the repo or commit them**). `IMG_4270.PNG` is the
original square-terrain frame from the earlier character/pathfinding work. The other ten images are mood references:
- tilted tile fields with selective emissive tiles;
- a wave of dot-squares;
- rainbow wave-tiles with depth of field;
- a stepped colour landscape;
- a smooth teal tile sheet;
- stylised platform-jumping characters.

They are references for the feel (colour, wave, selective light, buoyant characters), not targets to copy. Some of
them show things the brief rules out (a neon grid, a mostly teal field, block-world and game-art looks). **Where a
reference and the brief disagree, the brief wins.** Don't recreate any specific existing game's look or characters.

## Two agents, split by ownership

**Engineering agent** (`scene/musical-geography`, `../av-gen-geography`)
- **Brief phases:** Phase 1 (discovery and the technical plan), 2 (the terrain foundation), 3 (dynamic terrain),
  5 (the camera controller mechanics), 6 (characters, navigation, floating-platform response) and 7 (audio wiring),
  plus the performance half of 8 and the tests (§13).
- **It owns:**
  - engine and renderer code;
  - the tile instancing and GPU deformation;
  - the terrain query (the same surface for rendering and navigation);
  - the modulation wiring;
  - the camera controller;
  - character navigation;
  - the tests;
  - profiling.

**Art agent** (`scene/musical-geography-art`, `../av-gen-geography-art`)
- **Brief role:** the art director and look developer: §3 and the art half of §4 (which formations, where, and how
  they compose), §6 (materials and edge light), §7 (the palettes and the environment controller's looks), the camera
  path's composition (§9), and the cinematic evaluation (Phase 8, with the Creative Critic).
- **It owns:**
  - the scene file(s) and the look parameters;
  - the palettes, the material settings and post;
  - the camera path's shape;
  - the art-direction docs and the evaluation.
- **It does not edit engine code.** When it needs an engine feature or a parameter exposed, it writes the request
  into `02-requests.md` (on the engineering branch, through the coordinator) and the engineering agent builds it.
- It merges the engineering branch into its own as engineering lands (`git merge scene/musical-geography`).

## Order and the one stop

1. **Phase 1, in parallel:**
   - **Engineering:** the investigation (§2: find the original square-terrain experiment, audit what exists) and the
     concise technical plan (`03-technical-plan.md`).
   - **Art:** research like a VFX/art-direction professional and write the art direction (`04-art-direction.md`):
     - the palette states;
     - how the formations read;
     - the material and edge-light language;
     - the camera's journey;
     - how this avoids the brief's "explicit artistic restrictions".
   - Prove ideas cheaply: stills from the existing renderer, quick shader tests or Blender mock-ups. Commit no
     third-party assets.
2. **Then one stop for the owner (Gate 1):** the technical plan, the art direction and a handful of look stills.
   No implementation of Phases 2+ before the owner's OK.
3. **After the OK:**
   - engineering builds Phases 2, 3, 5, 6 and 7 in order;
   - art follows each landing with look development, from Phase 4 on;
   - Phase 8 (evaluation and optimisation) is done together.
   - The owner's checkpoints after Gate 1 are set in the plan; propose them there.

## Rules

- **Research tooling:** one research subagent at a time per agent at most, and no other helper subagents. The
  brief's "independent investigation subagents" is replaced by this rule.
- **Live scene rules (the owner's standing rules):**
  - the world fills every frame, with no visible edge or dead space;
  - no vortex, funnel or spiral-tower hero;
  - the camera looks where it travels (no orbits, hero shots or head turns at characters; dives and climbs are
    fine).
- **The live target:** 1080p at 30 fps or better on the M2 Max, measured. The brief's Windows machine: profile it
  only if a documented Windows GPU environment exists in the repo's docs. Otherwise say it was not available; don't
  invent numbers.
- **Audio:**
  - use the existing modulation, the response bus, the beat clock and stems (the drum classifier was cut, so don't
    rebuild one);
  - test on at least three materially different tracks from `assets/audio/` (not All You Got);
  - include `Feline Footwear.wav`.
- **ADRs:** engineering uses 1950–1969, art (through requests) 1970–1979. Ask the coordinator if a range runs out.
- **Don't add the scene to `examples/index.json`** until the owner approves it.
- All GPU work goes through `tools/gpu-lock.sh`; another agent (Strange Attractor) is rendering on the same GPU.
- **Assets:**
  - never commit third-party assets;
  - use free, licence-logged assets only;
  - send the coordinator the URLs of any download that needs a login;
  - run `tools/link-worktree-assets.sh` if assets are missing (already done for both worktrees).
- No gore. No compat shims (ADR-441). Don't run the full suites or merge to main until the owner approves;
  run the tagged tests for your own work.
- **The Critic** is `~/Documents/GitHub/creative-critic/.venv/bin/critic` (start it with `critic start --daemon` if it
  is down). The Critic is evidence; the owner's eye wins.
- **Media** goes to `~/Desktop/av-gen-review/105-musical-geography/`, in numbered folders:
  - `01-gate1/`
  - `02-terrain/`
  - `03-dynamic/`
  - `04-look/`
  - `05-camera/`
  - `06-characters/`
  - `07-audio/`
  - `08-evaluation/`
  - `10-full/`
  Use exact file names.
- **Before you hand back:** look at your own frames, not only the numbers.
