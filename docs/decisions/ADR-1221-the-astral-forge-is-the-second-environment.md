# ADR-1221: The Astral Forge is the second Environment

- Status: Accepted (prod/astral-forge)
- Builds on: ADR-1200, the Environment seam (cherry-picked from proto/bioluminescent `a8599186` as `abdcaef9`). It
  adds nothing to the interface.
- Found by: the owner's brief for a production Astral Forge for LIVE and OFFLINE (2026-10-07). The owner chose the
  look of the prototype's TEST 06 clips (iterations 1 and 2) and asked for slightly closer shots and longer holds
  on the formed face.
- Evidence: `docs/prototypes/astral-forge/09-production.md`.

## Problem

The prototype (`prototypes/astral-forge/`) makes a picture production cannot. Iteration 4 ported its look into
production primitives (ADR-1144..1156), and at the formed face production was about 9× slower (110.5 ms against
12 ms). The cost is structural:
- 66 ms of the 110.5 ms draws about three million fully shaded flake billboards;
- the density surface marches the exact compiled anatomy rather than a cached one.

The documented fallback is a native hero-entity node that runs the prototype's own pipeline. ADR-1200 has since
supplied the seam for exactly that: a scene-level block, owned by a specialised renderer, drawing into the shared
targets.

## Decision

**The Astral Forge is a scene block, `"astral"`, drawn by `rendering::AstralRenderer` on the Environment seam.**
The prototype's code moved into the engine rather than being re-derived:
- `src/scene/astral_song.*` is the song as the conductor reads it. `buildSong` reads a track production has
  already analysed.
- `src/scene/astral_conductor.hpp` is the conductor.
- `shaders/astral/*.wgsl` are the shaders.
- The prototype now builds from the same files and stays as the reference, behind its off-by-default option.

**The block (JSON).** Unknown keys are refused.
- `particles`, `grid`, `gridSize`.
- `conductor`: `auto`, `song` or `live`.
- `camera`: the conductor places the scene's camera.
- `preroll`.
- `controls`: the rest values of the parameters below.

**Parameters.** Every one is a real `astral/<leaf>` parameter, so routes, MIDI, macros, scene states, the timeline
and the editor reach it:

| Leaf | Range | What it does |
|---|---|---|
| `summon` | 0..1 | coherence floor |
| `hold` | 0..1 | holds the formed face |
| `collapse` | 0..1 | a trigger: fires on rising past 0.5 |
| `god` | -1 or 0..6 | -1: the score chooses |
| `intensity` | 0..2 | matter energy |
| `palette`, `light`, `atmosphere`, `godRays` | 0..1 | v2's look, off by default |
| `legibility` | 0..1 | |
| `zoom` | | distance divisor |
| `exposure` | | |
| `camera` | 0..1 | 0 hands the camera back to the scene |
| `density` | 0.05..1 | fraction of the particles simulated |

**Two conductors, one vocabulary, both in the Composition.** Each runs every frame, before the camera.
- **SONG.** Used when the frame's audio is an analysed track.
  - The engine hands the Composition its `AnalysisTrack` (`setAnalysisTrack`). The song and its score (sections,
    16-beat phrases restarted at section boundaries, kick-opened collapses) are built once per track.
  - The state is a pure function of the song second: the prototype's TEST 08 arc, with the controls applied.
- **LIVE.** Used with live input.
  - It reads the engine's live `AudioHistory`: the spectrogram rows, and the beat and band onsets.
  - It keeps the same envelopes the song analysis computes, opens a phrase every sixteen beats it hears, and marks
    the phrase collapsed when a strong kick lands on its downbeat. Virtual sections of eight phrases cycle the gods.
  - A performer's `collapse` opens a collapsed phrase at once.
  - The state comes from the same `test06` conductor.

When `camera` is on, the conductor writes the scene camera's position, target and field of view. Shake and breath
still compose on top. A change of shot advances `cutSerial` (ADR-912), so temporal history resets on a cut and
never across one.

**The renderer.**
- **Simulation.** It steps at a fixed 60 Hz whatever the frame rate. Each step is conducted at its own second (the
  song conductor is pure, so the renderer evaluates it itself; live sub-steps interpolate between frames). A density
  splat follows every step, so the simulation does not depend on the render rate.
- **Per frame:**
  1. the latent cache, framed on the shot;
  2. a half-resolution march;
  3. a full-resolution refine and shade of the engraved, tempered metal surface;
  4. flakes splatted in compute, with near flakes drawn as shards;
  5. a combine pass into the scene's HDR target and depth buffer. The surface writes its own clip depth, and the
     void writes the far plane, so the god occludes and is occluded like any other geometry.

  AV Gen's bloom, tonemap, AOVs and every output path follow.
- **Lazy compile.** Pipelines compile on the first frame that wants them, so a scene without the block pays
  nothing, and records nothing (the gate is tested).

**Determinism (ADR-1168's discipline).**
- **Offline tier:** a discontinuity re-simulates from the song's start, so `--range` and any seek land on exactly
  the frame play produces. The test compares byte for byte, and play at 60 fps equals play at 30.
- **Live tiers:** ADR-360's contract. A seek pre-rolls `preroll` seconds and lands in the same phase of the score,
  with the matter visually equivalent.

## Consequences

- **Cost model.** Production now costs what the prototype costs, because it runs the prototype's pipeline: about
  6.5 ms of simulation per step. The measured tiers are in ADR-1222.
- **Exact seek is slow.** A deep exact seek re-simulates the song up to that point: about 0.4 s per song second at
  2M particles. That is fine for an offline `--range`; the editor's live tier never pays it.
- **Look differences from the prototype.** The look matches the prototype except in three places:
  - **the post chain** is AV Gen's (AgX tonemap and bloom), where the prototype had its own filmic curve. The
    production scene sets the post to match;
  - **v2's look** (palette, atmosphere, god rays) is opt-in through parameters;
  - **legibility** is on by default.
- **No shadow casting.** The god does not cast into the scene's shadow maps: ADR-1200 has no shadow hook.
  `SdfRenderer::drawShadow` is the model if a HYBRID scene ever needs it.
- **Tests.**
  - `tests/unit/test_astral_forge.cpp` covers JSON, parameters, routes, the camera hand-back, the pure song
    conductor, the live conductor's phrases and collapse trigger, the live project's MIDI map, and the tier ladder.
  - `tests/rendering/test_astral_forge_gpu.cpp` covers the draw and the gate, seek equal to play at the offline
    tier (with the pre-roll control), and the tiers' particle counts.
