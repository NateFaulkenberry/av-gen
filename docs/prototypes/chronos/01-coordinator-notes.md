# CHRONOS: coordinator notes

The governing brief is `00-brief.md` (owner, verbatim). These notes add the standing rules and the
stops.

## Standing rules that override the brief's workflow section

- **One agent, no subagents.** The owner's standing rule overrides brief §12.5. One agent covers
  all research and implementation.
- **Merging:** the coordinator merges into main after the owner approves. You prepare a
  fast-forwardable, rebased head with both suites at exit 0. You do not push or merge.
- **Scene registration:** the scene stays OUT of `examples/index.json` until the owner approves
  it. If a test requires live scenes to be indexed (`sonic.live` plus the index's `"live": true`
  are test-enforced to agree), keep `sonic.live` off, or stop and ask. Don't add it to the index.

## Things already in the repo you must audit before designing anything

- **Existing temporal effects:** `src/rendering/temporal_effects.cpp`, `shaders/temporal.wgsl` and
  `src/scene/temporal_settings.hpp`. AV Gen already ships frame echo, datamosh, feedback and a
  slit-scan as post effects. Extend or generalise them into the reusable temporal imaging system.
  Don't build a parallel one. Document what exists, what it lacks against brief §3, and the
  refactor.
- **Digital Mosh** (ADR-1160..1166) and the world-space corruption from Strange Attractor
  (ADR-1873).
- **Strange Attractor's terrain:** the RTIN watertight mesher (ADR-1857, 1867), the analytic
  cirrus (1864/1865), the water fixes (1868–1871), and the double-precision camera basis lessons
  (1807/1855/1866/1870: never rebuild rays or depth through float `invViewProj` with a huge far
  plane). Reuse the techniques; make a distinct landscape. Main also holds Strange Attractor's
  portal and multi-world code (ADR-1872..1884), which CHRONOS doesn't need.
- **Audio:** there is no kick/snare/hat classifier any more (it was cut). Use `response.*` (onset,
  onsetLow, bass, mid, high, level and the rest), `beat.*`, `stem.*`/`role.*` when stems exist,
  and MIDI.
- **Render audit work** (ADR-1807..1821): the compiled material programs, lit-shader
  specialisation, and the quality-ladder levers. Add the temporal effects as live-optimizer
  levers if that's sensible.

## Stops (stop and hand back at each; the owner reviews)

- **Gate 0: reconnaissance, plan and a cheap proof.** Write:
  - `02-recon.md`: the architecture assessment, the existing temporal code, reuse and risks;
  - `03-plan.md`: the temporal imaging architecture, the integration points, the phase plan,
    budgets, and the music choice.

  Include one or two throwaway proof clips of slit-scan structure on a moving camera, using
  whatever exists today, so the owner sees the look early. STOP.
- **Gate 1:** Phase 1 (the frame-history ring plus vertical and horizontal slit scan, with tests)
  and Phase 2 (the CHRONOS canyon and camera, compelling with no effects). STOP.
- **Gate 2:** Phases 3–4 (directional slit scan, displacement, fracture, feedback, the audio
  mappings), with per-effect audition clips. STOP.
- **Gate 3:** Phase 5 (the five-act progression, tuning, the 1080p30 profile, the live perf gate
  where it applies), a full render with sound, and both suites at exit 0. STOP, ready for merge.

## Rules

- Worktree `../av-gen-chronos`, branch `scene/chronos`, from main 214dafd8. ADR range 1930–1969.
- Media goes in `~/Desktop/av-gen-review/104-chronos/gate0/`, then `gate1/` … `gate3/`. Give exact
  file names in hand-backs.
- **Shared machine:** another agent (Strange Attractor Art Pass 2) shares the GPU. Every
  avgen/GPU run goes through `tools/gpu-lock.sh`, and any perf measurement must be repeated if
  the machine was loaded. The live perf gate reports CONDITIONS DIFFER when the refresh rate or
  load differs (ADR-1808). Never `write --add` to the live baseline.
- **Suites:** `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/ci/run-suite.sh gpu` (run bare) and
  `tools/gpu-lock.sh tools/ci/run-suite.sh cpu-assets`. The exit code is the only signal.
  Reconfigure CMake after adding test files.
- **Assets:** free assets from clearly licensed sources (Poly Haven, AmbientCG, Quaternius,
  Kenney, and Sketchfab per licence) are allowed. Record each one's source and licence in
  `docs/prototypes/chronos/assets.md`. Never commit third-party assets, because the repo is
  public; keep them in a gitignored path. Stop and ask if a download needs a login, or the licence
  is unclear. Blender is at /Applications/Blender.app (not on PATH); run it headless.
- **Audio:** default track Feline Footwear (`~/Desktop/Feline Footwear/` is read-only) or
  Trench.wav, plus their stems where available. Never commit audio or stems.
- **Owner sensitivities:**
  - unintended flicker, which must be measured on video;
  - no visible world edges;
  - no vortex or funnel heroes;
  - the world must fill the frame.

  Here intentional temporal disruption is the art, but stationary geometry must not smear when
  an effect is meant to be off (brief Test C).
- No compat shims (ADR-441). Generic engine features go in the engine with tests. Scene art goes
  in the scene.
- Keep a `PROGRESS.md` beside these notes.
