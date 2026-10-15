# Digital Ocean: final report

*The art agent's report on the brief `00-brief.md` (deliverable 10). Plan: `01-plan.md`; the stage-by-stage log with every
decision and wrong turn: `PROGRESS.md`; the scene guide (intent, controls, mappings, implementation): `03-guide.md`.
Branch `art/digital-ocean`. Review media: `~/Desktop/av-gen-review/50-digital-ocean/`.*

## What it is

**The Tidewriter.** A stylized ocean at electric twilight, seen from a still, composed camera 12 m over the water:
Gerstner swells roll in from under a low cream sun across indigo water, cyan light glows through their backlit crests,
mint foam caps the highest of them, the sun's path breaks into crisp glints, twilight stratus burn coral round the sun
and a hazy island holds the left third. The music is written into the sea -- what the bass played seconds ago arrives
at your feet as swell, every kick sends a lifted front in from the horizon -- and dolphins leap across the sun's path.
A performer can raise a hero wave, fire a leap, bloom the crests with bioluminescence, flare the sun or let night fall.

- **Palette:** electric twilight (chosen from four rendered on the real scene); its two related ends on controls,
  read as a time of day: golden hour (CC 7) and a bioluminescent night (CC 1: navy sea, emerald crests, a pale moon).
- **Signature features:** tidewriting (the bass's history as swell), the dolphins (ambient pods and a pad-fired hero
  leap across the sun's path), the hero wave, the night transformation.
- **Registered:** `examples/index.json`, "Sonic Abstract - Digital Ocean", `"live": true`, `sonic.live` project.

## Stages completed

| stage | deliverable | media |
|---|---|---|
| 0 | reconnaissance, plan | `01-plan.md` |
| 1 | three compositions + three refinements; C2 chosen | `stage1-compositions.png` |
| 2 | screen-density sea mesh; Gerstner trains from sine-deformer pairs | `stage2-swell-*.{png,mp4}` |
| 3 | unlit sea/sky programs; four palettes compared; smooth vs banded | `stage3-*.png`, `stage3-motion.mp4` |
| 4 | drifting twilight stratus round the viewer, the island, the sun's lit pool | `stage4-sky*.png` |
| 5 | foam caps on reinforced crests | `stage5-foam-caps.png` |
| 6 | dolphins on path deformers (pods + hero), timed splashes | `stage6-*.{png,mp4}` |
| 7 | audio map: tidewriting, kick fronts, per-band responses; ADR-1370 | `stage7-*.{png,mp4}` |
| 8 | MIDI map (8 CCs, 5 pads), verified live end to end | `stage8-*.{png,mp4}`, `live-midi/` |
| 9 | rarer leaps, a seamless camera breath | `stage9-silent-60s*.{mp4,png}` |
| 10 | three critique rounds across seven conditions | `stage10-critique-round{1,2,3}.png` |
| 11 | profiling, elimination costs, suites, review_examples | this report |

## Audio and MIDI

See `03-guide.md` for the tables. In short: bass -> tidewriting swell (spectrum field, 32-55 Hz, delayed by distance,
ADR-1370-smoothed); kick -> lifted fronts rolling in + crest/foam flash; level -> swell height; intensity -> sharper
crests; mids -> wind sea; snare -> spray; hats/highs -> glints; sustain -> the sun's halo and path; brightness -> palette
drift; MIDI notes -> sparks on the sun's path by pitch. CCs 1-8: night, swell, chop, foam, sun, tide, golden, spray.
Pads 36-40: leap, hero wave, bloom, flare, spray. All `response.*` signals are shaped by the Live panel's sliders.

## Performance (M2 Max, headless `tools/live_scene_profile.py`, under `tools/gpu-lock.sh`; live target 30 fps)

| case | GPU median | GPU p95 | frame median / p95 | CPU work | status |
|---|---|---|---|---|---|
| 1080p, silence, Ultra (baseline, before Stage 10/11 changes) | 21.6 ms | 22.2 | 24.0 / 28.7 | 1.2 | achieved |
| 1080p, Trench from 60 s, Ultra (baseline) | 27.1 ms | 27.7 | 32.7 / 35.4 | 4.7 | achieved |
| 1080p, silence, Ultra (final) | 22.4 ms | 22.8 | 25.2 / 30.0 | 1.6 | achieved, headroom |
| 1080p, silence, auto (final; settles at Ultra) | 22.4 ms | 22.9 | 24.8 / 29.3 | 1.3 | achieved, headroom |
| 1080p, Trench from 60 s, Ultra (final) | 26.1 ms | 26.5 | 28.3 / 34.4 | 1.7 | achieved, headroom |
| 2160p, silence, Ultra (final) | 61.6 ms | 62.1 | 64.2 / 68.8 | 1.4 | over: the live ladder scales it |

28 draws, 260k triangles, 5 material programs (166 ops), no lights, no shadows, no volumes.

**Where the time goes** (elimination, 1080p silence, GPU median against 22.4 ms): the sky dome 3.1 ms; the clouds 1.6;
the near sea's detail (micro-ripples, foam, near fade) 0.85; dolphins and every particle system 0.4; the sea itself
(three meshes through ten deformers, two programs) the remaining ~16 ms. With music (Trench) the frame costs 3.7 ms
more: the scene pass +1.9 ms (the audio fields' vertex work, ADR-1370's taps, and the reacting surfaces) and a depth
prepass of 1.6 ms that the renderer only runs in that case (not investigated further); the tide's cost was cut ~1 ms by
a narrower band and window. The frame is fragment-bound (4K costs
2.7x 1080p), so the live quality ladder's render scale degrades it gracefully; the composition never depends on
anything the ladder drops (no shadows, volumes, DOF or motion blur in use).

**A hypothesis the measurement rejected:** a full sky sphere centred on the camera looked like 100 % sky-program
overdraw under the sea; replacing it with a hemisphere changed nothing measurable (early depth already rejected it).
Kept, as harmless.

## Engine and tool changes

- **ADR-1370** (engine): a spectrum field can read a smoothed history (`audioSmooth`, a tap per history row, 2..32;
  CPU and GPU in step; 0 = unchanged, unwritten, unhashed). Needed because a history laid out in space by `audioSpeed`
  carries the analysis hop's jitter as spatial noise. Tests: a unit case and the CPU/GPU parity harness.
- **Probe** (`tools/sonic_live_probe.cpp`): a `controller` scenario that plays CC ramps and pads as a CoreMIDI virtual
  source (the live end-to-end MIDI test).
- **Generator kit**: `kit.Scene.sources` (lfo/envelope/timeline sources), `kit.upsert_index_entry`; `abstract.py build`
  re-applies a module's own entry after a full-category `write_index`.

## Files

`tools/sonic_vfx/scenes/digital_ocean.py` (the generator), `tools/sonic_vfx/{kit,abstract}.py`,
`tools/sonic_vfx/scenes/__init__.py`, `examples/sonic-abstract/digital-ocean{,.scene}.json`,
`examples/sonic-abstract/meshes/do-{sea-near,sea-mid,sea-far,dome,isle,dolphin}.glb`, `examples/index.json`,
`src/spatial/{field.hpp,field.cpp,audio_history.hpp}`, `src/scene/field_params.cpp`, `shaders/fields.wgsl`,
`tests/unit/test_audio_history.cpp`, `tests/rendering/test_audio_fields_gpu.cpp`, `tools/sonic_live_probe.cpp`,
`docs/decisions/ADR-1370-*.md`, `docs/decisions/README.md`, `docs/prototypes/digital-ocean/*`.

## Validation

After merging origin/main (`b498a314`) and regenerating the scene (unchanged); each binary's own exit code:

- `python3 tools/check_project_integrity.py`: 0 problems.
- `tools/ci/run-suite.sh cpu-assets`: **exit 0** -- 4231 passed, 0 failed, 19 skipped, 1 expected failure, 4251/4251.
- `tools/ci/run-suite.sh gpu --bin build/release/tests`: **exit 0** -- 657 passed, 0 failed, 1 skipped, 658/658.
- `python3 tools/review_examples.py --only "Digital Ocean"`: **ok** (File > Examples > Live > Sonic Abstract, Ready,
  exit 0).
- ADR-1370's own tests: `avgen_tests [audio-fields]` exit 0 (82 assertions); `avgen_render_tests [audio-fields]` exit 0
  (1008 assertions, CPU/GPU parity with a smoothed field).
- Live, end to end (Stage 8): the editor in `--live` with BlackHole input and the probe as a MIDI controller; 370
  messages received; every gesture visible in the capture.

## Findings for the owner

- **Planar audio fields ignore `point`** (ADR-1370, Consequences): `waveDistance` for `waveGeometry: planar` is
  `dot(q, axis)` in the field's frame; ADR-1116 says the delay is measured "about `point`". Place planar fields with the
  node's `position`. Fixing it would move existing planar fields whose `point` is not at their origin.
- **This machine's saved MIDI device overrides a project's `*` filter** (`SE49 MIDI1` in the Live panel): with another
  controller, pick it in the Live panel. With a name filter, a source that appears after the editor opens is never
  connected.
- **The GPU lock was the bottleneck of this pass**: three other agents' suites held it for most of the night; renders
  waited up to an hour, and gpu-lock's default 60 min wait timed one batch out.

## Remaining limitations and opportunities

- The clouds are better after critique but still lean toward stacked lenses -- the weakest element. ADR-1289's
  sculptural lobes or authored cloud meshes would lift them (cost first).
- The hero wave restarts when its pad is hit again before it arrives (a triggered field keeps only its latest event).
- CC 1 (night) and CC 7 (golden) are alternatives; both up adds both.
- Wave speed and direction are not on controls (changing a deformer's speed or axis jumps every wave).
- The material programs are interpreted per fragment; a compiled path (PROGRESS-abstract's engine need 12) would buy
  most of the 4K headroom.
- Opportunities: reflections of the dolphins in the glitter path (a mirrored twin, Alpine's trick); a bioluminescent
  wake behind the hero dolphin at night; per-pitch glints for MIDI melodies along the sun's path.
