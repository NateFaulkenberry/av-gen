# Talking Heads: handoff to the implementation agent (start here)

**What it is.** A new Sonic Abstract **live** scene, 1080p30. A pale, elongated porcelain mannequin bust floats in a
dark indigo ether. Coloured beams pour from its mouth and eyes, and it vibrates, takes on colour and tears like a video
signal with the music. One head only.

**State on 2026-10-25.** Research and prep are done. The owner has decided D1-D4 (03 §8):
- custom bust;
- reference downloads approved;
- **ADR-1670 (the Light Beam follows its owner in local space) is the first engine task**, replacing the riding spot
  lights;
- tearing as full-screen bursts plus the local layers first.

D5-D9 are decided at the first review. Branch: `scene/talking-heads` in `../av-gen-talkingheads`.

## Read, in order (about 30 minutes)

1. `docs/prototypes/talking-heads/00-brief.md`: the owner's spec, verbatim. §10 holds the acceptance checks; §12 says
   what not to build.
2. `03-implementation-plan.md`:
   - §1 the slice;
   - §2 the ordered tasks and their checks;
   - §3 the binding table;
   - §4 the MIDI map;
   - §5 the budget;
   - §8 the decisions (D1-D4 decided; D5-D9 at stop 1).
3. `02-research-engine.md`:
   - §2 which engine system serves each feature, with file pointers;
   - §3 audio and latency;
   - §4 measured cost;
   - §5 optional gaps G1-G3.
4. `01-research-assets.md`: §4 the bust probe, §5 the export recipe, §6 art direction (palette, the hero cone and rays,
   porcelain).

## Starting material

| What | Where |
|---|---|
| Bust generator (probe; promote it to `tools/talking_heads/make_bust.py`) | `docs/prototypes/talking-heads/probe/make_bust.py`. Run it with `/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P <script> -- --out <dir>` (Blender 5.2.1; the Blender MCP was not connected) |
| Emitter anchors (GLB frame: +Y up, face +Z) | `probe/bust_anchors.json`. Mouth (0, 0.384, 0.081), eyes (±0.0326, 0.462, 0.0729) |
| Probe previews | `probe/bust-contact-sheet.jpg`; full size in `~/Desktop/av-gen-review/talking-heads/prep/` |
| Proof that the beams ride the head | `probe/engine-beams-follow-bust.jpg` |
| Cost probe (eight arms; how to bench) | `probe/cost_arms.py`. Results in 02 §4: 4.0-4.7 ms GPU p50 for the full stack against 29.3 ms |
| Reference downloads (gitignored; manifest `assets/talking-heads.manifest.json`) | `assets/talking-heads/reference/innerscene-modern-abstract-bust.glb` (CC0). Comparison: `~/Desktop/av-gen-review/talking-heads/prep/compare-candidates.png` |
| Mood reference (copyrighted: **never** copy it into the repo) | `~/Desktop/av-gen-review/talking-heads/reference/de-loused-in-the-comatorium.jpeg`. Take one dominant soft mouth cone with a bright core; do not reproduce the cover |

## Code you will touch or copy

- **Scene generator:**
  - `tools/sonic_vfx/kit.py`: `Scene`, `R`, `M`, the envelopes, `drift_camera`, `effect(owner=("light", id))`;
  - `tools/sonic_vfx/abstract.py build <module> [--projects DIR]`;
  - `tools/sonic_vfx/scenes/__init__.py` (`ABSTRACT`);
  - models to read: `scenes/glitch_signal.py` (glitch routes, Sine/Noise deformers) and `examples/digital-mosh/arc.py`
    (states, pads).
- **Signals:**
  - `tools/sonic_vfx/signals.py`;
  - `tools/drum_signals.py` (`signal.kick`, `kickEnv`, `bassLevel`, `synthLevel`; the pattern for `signal.voice`,
    `snare`, `hat`, `awake`, `overload`);
  - ADR-1602 (`signal` sources), ADR-1620 (a mix has no kick), ADR-1630 (roles), ADR-1604 (onset register),
    ADR-1606/1612 (pickup, channel rest).
- **Beams:**
  - `src/world/effects/kinds/light_beam_effect.cpp` (rows; Light-owner resolution);
  - authored lights riding a node: `src/scene/composition.hpp` `AuthoredLight.node`; light params
    `lights/<id>/{outerCone, range, azimuth, elevation, color}`.
- **Entity FX:** `src/world/effects/kinds/{shake,float,glow,pulse,color_cycling,rim_light}_effect.cpp`, and
  `src/world/effects/entity_fx.hpp`, `transform_frame.hpp`.
- **Materials:** `docs/procedural-materials.md` (ops: oklabRamp ADR-1470, quantize ADR-1162, fresnel, noise, mixBy);
  ADR-1572 (a program on a gltf node; procedural nodes draw blended materials opaque).
- **Glitch and echo:** `src/scene/post_glitch.hpp` (`post/glitch/*`, `post/split/*`),
  `src/scene/temporal_settings.hpp` (`temporal/echo/*`).
- **Deformers:** `src/scene/procedural.hpp` (`DeformerKind`; Sine semantics).
- **Perf:**
  - `tools/live_scene_profile.py`;
  - `tools/live_perf_baseline.py` with `docs/perf/live-baseline.json` (ADR-1608);
  - `tools/gpu-lock.sh` (read its header: the exit code rule, and the CPU suite counts as GPU work).

## Where things go

- Script: `tools/talking_heads/make_bust.py`.
- Meshes (committed, our own work): `examples/sonic-abstract/meshes/th-bust.glb`, `th-bust-ghost.glb`,
  `th-bust.anchors.json`.
- Scene module: `tools/sonic_vfx/scenes/talking_heads.py`. Output:
  `examples/sonic-abstract/talking-heads.{json,scene.json}`.
- Media: `~/Desktop/av-gen-review/talking-heads/`.
- ADRs: 1670-1689.
- No `.blend` in git. No third-party binaries in git.

## Rules that bite

- **Build.** Build your own tree: `cmake --preset release` with `-DCPM_SOURCE_CACHE=` pointing at an existing worktree
  cache, then `cmake --build build/release --target avgen`.
- **GPU.** Every GPU job goes through `tools/gpu-lock.sh`, one job per hold, because another agent shares the GPU.
  Check `pmset -g batt` (AC) before timing.
- **Agents and merging.** No helper agents. The owner approves visuals before any merge, registration or guide edit.
  No compat shims. Never touch `../av-gen` (the owner's checkout).
- **Two traps from the probe:**
  - a beam whose spot light has intensity 0 disappears, so route `fx/<id>/intensity`, not the light;
  - kit's environment block wants `sky: {zenithColor, horizonColor, groundColor, ...}`. Top-level sky keys are ignored
    with a warning.
- **Never read `macro.*`.** It arrives one frame late.
