# Scene removal impact

For each unique scene: what removing it would touch. Taken at `ed9926db`. Machine-readable version, with every reference as `file:line`: [`data/scene-removal-impact.json`](data/scene-removal-impact.json). The inventory is in [`scene-inventory.md`](scene-inventory.md).

## Classes

- **A**: easy/standalone: no code/test/tool reference beyond the index entry and docs
- **B**: minor cleanup: skip-tolerant example lists, count/name assertions, own generator, docs
- **C**: needs test/benchmark/lab migration: a test, lab or tool asserts on this scene's content
- **D**: significant shared dependency or unique engine functionality: src hard-codes it by name, it is the sole user of an engine subsystem, or many tests/tools/scenes depend on it
- **E**: unclear: cannot be inspected (generated-missing) or status undecided

Counts: **A** 7, **B** 46, **C** 17, **D** 10, **E** 1 (total 81).

Caveat: References are textual matches (file path, basename with directory confirmation, distinctive ids/display names) with known false positives removed; generic sweeps that include every example are listed separately. Line numbers are at HEAD ed9926db.

## Generic sweeps that include every scene

Removing any file never breaks these, but they constrain what an index edit may do:
- `tests/integration/test_project_system.cpp` "Every example in the index exists and loads": every index entry must load its project **and its scene** (`requireProjectScene`, since 2026-10-14). An entry whose files are generated must carry a `generate` command; it is then reported, not loaded, on a checkout without them. Removing a scene means removing its entries.
- `tests/integration/test_project_system.cpp` "Examples > Live lists exactly the sonic.live projects": the Live submenu must list exactly the `sonic.live` projects, with a floor of `>= 28` live entries. The current count is 29, so removing **two** live scenes fails it. Lower the floor deliberately when cutting live scenes.
- `tests/unit/test_example_menu.cpp:86`: Sonic VFX set == 17 (`:122`), Sonic Abstract set == 9 (`:125`); named live entries at `:102-103`; no category in both menus.
- `tests/unit/test_scene_authored_lights.cpp:448,527`, `tests/integration/test_volumetric_noise_authoring.cpp:125`, `tests/unit/test_beam_lab.cpp:637,769`: sweeps over `examples/` (or `examples/world`). They tolerate removals.
- `tests/integration/test_procedural_engine.cpp` and `tests/rendering/test_procedural_examples_gpu.cpp` name explicit example lists. They used to skip a missing file silently; since 2026-10-14 they REQUIRE it. Deleting a listed scene fails them until it is taken off the list, which is deliberate: that is when its determinism and load coverage moves elsewhere.

## Scene-specific code in src/

| Location | What it does | Scene |
|---|---|---|
| `src/app/live_scenes.cpp:33` | `if (e.name == "Sonic Live")` puts it first in the Sonic VFX switcher | Sonic Live |
| `src/app/live_scenes.cpp:39` | `e.name != "Sonic Live"` excludes it from the category walk | Sonic Live |
| `src/app/application.cpp:2068` | `if (ex.name == "Sonic Live") liveDemoPath_ = ex.file;` sets the Live panel demo button target | Sonic Live |
| `src/app/application.cpp:2081` | status "the Sonic Live example was not found" | Sonic Live |
| `src/app/live_scenes.hpp:36,38` | `kLiveSceneCategory = "Sonic VFX"`, `kLiveSceneSets = {"Sonic VFX","Sonic Abstract"}` (index category names) | Sonic VFX / Abstract families |
| `src/labs/lab.cpp:45,55-56,64,72,80,90,99,107,115,123,131,139,147,154,165` | Engineering Labs registry: hard-coded fixture paths (character-animation-lab, character-intelligence-lab, renderer-qa.scene, lod-geometry-lab, camera/behaviors, shadow/lighting/hdr/volumetric/particle lab scenes, quality/aliasing(-dolly), stress/stress, **world/glowmere-valley-2-multicam.json**) | labs + Character Animation Lab, Renderer QA, Quality Lab, Stress, Glowmere Valley 2 |
| `src/labs/case.cpp:52` | `examples/labs/<key>/cases.json` path convention | lab fixtures |
| `src/world/art_direction.cpp:16-18` | `glowmere` ArtDirectionProfile, values "read out of examples/world/glowmere-stylized.scene.json" (copied, not loaded) | Glowmere stylized |
| `src/organism/mushroom.cpp:458` | `findArtProfile("glowmere")` fixes the hero-mushroom palette to Glowmere | Glowmere family |
| `src/world/world_map.cpp:1009` | `defaultWorld()` is named "glowmere" (built-in default world) | Glowmere family |
| `src/rendering/astral_renderer.*`, `src/scene/astral_forge.*`, `astral_conductor.hpp`, `astral_song.*`, `src/app/interactive_resolution.cpp:647-709`, `src/app/live_profile.cpp:387-391` | a renderer, conductor and live-quality levers that exist for one scene (the only `astral` block author besides its prototype arms) | The Astral Forge |
| `src/scene/ecosystem.*`, `src/rendering/environment_renderer.hpp` | the Environment seam (ADR-1200), authored by THE RIFT and by the generated Biovalley macro | THE RIFT, Biovalley macro |

Everything else in `src/` that names a scene file is a comment recording a measurement: `src/app/engine.cpp:2141,2199,2229,4103`, `engine.hpp:256`, `src/gpu/texture.cpp:22`, `src/app/interactive_resolution.hpp:10`, `src/scene/composition.hpp:1078,1377`, `src/scene/composition.cpp:10061`, `src/world/ecology.cpp:400`, `src/params/serialization.cpp:625`, `src/stage/stage_json.cpp:6`, `src/app/ui_script.cpp:646,659,787`, `src/ui/control_panel.{hpp,cpp}`, `src/ui/ui_logic.hpp:433`, `src/entity/entity.hpp:1601`, `src/app/render_settings.hpp:93`, `src/rendering/debug_visualizer.cpp:775`, `src/core/json_keys.hpp:40`, `src/scene/ecosystem.hpp:29`. These go stale but do not break anything. The Glowmere-named presets in effect catalogues (particle "Glowmere Canopy/Drift/Spores", force field "Glowmere Ward", meteor "Glowmere Fall", bioluminescence "Glowmere Creature", `glowmereAurora()`) are engine content named after the scene. They do not load any scene file.

## Easiest to cut (class A)

- **Alien Squad - Modular Characters**: No code/test/tool reference beyond the index entry; uses purchased assets/aliens.
- **Farm Animals - Imported Pack**: No code/test/tool reference beyond the index entry; uses purchased assets/farm.
- **Astronaut Musicians - Prototype**: No code/test reference; tools/make_astronaut_musicians.py writes it and tools/gv3/musicians.py reuses the validated prototype design (not the file).
- **Fungi**: Docs only.
- **Organic Curves**: Docs only.
- **The Tide (leviathan)**: No code/test/tool/doc reference; uses its own lightrig and material.
- **Open Vista (stress recipe)**: Docs only (renderer-upgrade scope contract).

Top five: The Tide, Fungi, Organic Curves, Alien Squad, Farm Animals. The last two also drop menu entries that need purchased assets. Open Vista and Astronaut Musicians are also class A, but each is a candidate fixture or still-active prototype work.

## Significant dependencies (class D) and unclear (class E)

- **The Astral Forge** (D): Sole user of the `astral` scene block: the Astral Forge renderer and conductors exist for this scene only; three tests name its live project. Unique engine coverage: `astral` scene block -> AstralRenderer (latent SDF entity, particle dust, conductor); `note` kind.
- **Digital Mosh** (D): Sole authored user of the quantize / displaceField / points node kinds; an integration test checks its three projects, and the control-seek test defaults to it. Unique engine coverage: node kinds quantize, displaceField, points (only `displaceField`/`quantize` appear in 1-2 synthetic tests); grids also used by gpu-regression/mycelium and labs/grid-catchup-lab.
- **THE RIFT (Bioluminescent Environment)** (D): First Environment (ADR-1200 seam): ecosystem layers + excitable propagation grid; 5 integration test cases (tests/integration/test_rift.cpp) incl. the ADR-1168 seek-parity regression; CI action builds its generated scene. Unique engine coverage: ecosystem Environment + GridMode::Excitable propagation grid (Biovalley macro also uses the ecosystem).
- **Sonic Live** (D): Hard-coded by display name in shared app code: leads the Sonic VFX switcher and is the "Live demo" button target. Unique engine coverage: n/a.
- **Renderer QA** (D): Core renderer QA fixture: Visibility Lab fixture in src, 17 GPU-suite references, baseline snapshots. Unique engine coverage: renderer QA coverage scene with snapshot baselines (examples/qa/baselines).
- **Glowmere Valley - Painterly / Matched PBR / Lyrics** (D): The reference Glowmere scene: art-direction profile values, the glowmere recipe and many tests/tools derive from it. Unique engine coverage: source of the glowmere art-direction profile; glowmere.recipe.json is composed "from the same species the authored scene uses".
- **Glowmere Valley 2 (+ Multi-Camera, Song)** (D): The most-referenced scene: ~100 unit, 24 GPU, 18 integration refs, 45 tool refs, Interaction Lab fixture, CI exceptions list, effects/ufo-stack fixture reuses its multicam scene. Unique engine coverage: UFO set-piece/abduction staging, directing, song-section shot types (intro/refrain/...), five-alien cast.
- **Tree of Life - Floating Island / Celestial Night** (D): Hero asset showcase with 12 test refs (4 unit, 8 GPU), tools, and ~60 asset-LOD/treeisland measurement arms derived from it. Unique engine coverage: n/a.
- **Chorus Field (Trench god)** (D): Sole user of the Fiber source and Streamline deformer (ADR-1180/1181); 114 tracked iteration files; tools/chorus/* drive it. Unique engine coverage: `fiber` source, `streamline` deformer, `noiseModulated` (fiber: 2 tests, streamline: 0 tests name it).
- **Liminal Euclidean World: All You Got** (D): Sole user of liminal SDF kinds (stairs, warp, screw) and beatgrid; integration test reads its scenes; 42 tracked files across 4 passes. Unique engine coverage: SDF stairs/warp/screw/shell/scale kinds, beatgrid source.
- **Biovalley Macro Showcase** (E): Generated and gitignored; cannot be inspected on a clean checkout; needs Blender-built meshes. Unique engine coverage: ADR-1280..1292 (lobe-cloud Environment, cinematic water, 16 material slots) were built for it; ecosystem shared with the-rift.

## Per-scene table

| Scene | Class | Effort | Unit | Integr. | GPU | Bench/perf | Tools | src | Docs | Unique engine coverage | Reason | Verification level |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Alien Squad - Modular Characters | **A** | trivial | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | No code/test/tool reference beyond the index entry; uses purchased assets/aliens. | 5: frame ok (2026-10-14 review) |
| Astronaut Musicians - Prototype | **A** | trivial | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | No code/test reference; tools/make_astronaut_musicians.py writes it and tools/gv3/musicians.py reuses the validated prototype design (not the file). | 5: frame ok (2026-10-14 review) |
| Farm Animals - Imported Pack | **A** | trivial | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | No code/test/tool reference beyond the index entry; uses purchased assets/farm. | 5: frame ok (2026-10-14 review) |
| Fungi | **A** | trivial | 0 | 0 | 0 | 0 | 0 | 0 | 2 | no | Docs only. | 5: frame ok (2026-10-14 review) |
| Open Vista (stress recipe) | **A** | trivial | 0 | 0 | 0 | 0 | 0 | 0 | 2 | no | Docs only (renderer-upgrade scope contract). | 5: frame ok (2026-10-14 review) |
| Organic Curves | **A** | trivial | 0 | 0 | 0 | 0 | 0 | 0 | 1 | no | Docs only. | 5: frame ok (2026-10-14 review) |
| The Tide (leviathan) | **A** | trivial | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | No code/test/tool/doc reference; uses its own lightrig and material. | 5: frame ok (2026-10-14 review) |
| Ashfall | **B** | trivial | 2 | 0 | 0 | 0 | 0 | 0 | 0 | no | tests/unit/test_weather_examples.cpp lists every weather scene by name. | 5: frame ok (2026-10-14 review) |
| Bioluminescent Grove | **B** | trivial | 1 | 0 | 0 | 0 | 0 | 0 | 2 | no | Only in a hidden [.probe] project list. | 5: frame ok (2026-10-14 review) |
| Bioluminescent World | **B** | trivial | 1 | 0 | 0 | 0 | 0 | 0 | 1 | no | Only in a hidden [.probe] project list. | 5: frame ok (2026-10-14 review) |
| Dust motes | **B** | trivial | 2 | 0 | 0 | 0 | 0 | 0 | 1 | no | tests/unit/test_weather_examples.cpp lists every weather scene by name. | 5: frame ok (2026-10-14 review) |
| Fireflies | **B** | trivial | 2 | 0 | 0 | 0 | 0 | 0 | 0 | no | tests/unit/test_weather_examples.cpp lists every weather scene by name. | 5: frame ok (2026-10-14 review) |
| Glowmere Density ladder (Low/Medium/Dense/Extreme) | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 15 | no | Recipes; docs reference the ladder numbers; tools/certify.py and test_ai_tools use the recipe by name. | 5: frame ok (2026-10-14 review) |
| Impossible Chamber | **B** | trivial | 1 | 1 | 1 | 0 | 0 | 0 | 2 | no | Example lists and a help test that skips if missing. | 5: frame ok (2026-10-14 review) |
| Plant Grammar | **B** | trivial | 0 | 0 | 0 | 0 | 0 | 0 | 2 | yes: procedural `grammar` node kind (2 tests mention "grammar") | Docs only, but it is the only scene authoring `grammar` nodes. | 5: frame ok (2026-10-14 review) |
| Pollen, spores and seeds | **B** | trivial | 2 | 0 | 0 | 0 | 0 | 0 | 0 | no | tests/unit/test_weather_examples.cpp lists every weather scene by name. | 5: frame ok (2026-10-14 review) |
| Procedural Benchmark | **B** | trivial | 0 | 1 | 0 | 0 | 0 | 0 | 0 | no | Only in the skip-tolerant procedural example list. | 5: frame ok (2026-10-14 review) |
| Procedural Geometry Lab | **B** | trivial | 0 | 1 | 1 | 0 | 0 | 0 | 0 | no | Only in the two skip-tolerant procedural example lists. | 5: frame ok (2026-10-14 review) |
| Rain | **B** | trivial | 2 | 0 | 0 | 0 | 0 | 0 | 0 | no | tests/unit/test_weather_examples.cpp lists every weather scene by name. | 5: frame ok (2026-10-14 review) |
| Season shift | **B** | trivial | 3 | 0 | 0 | 0 | 0 | 0 | 2 | no | tests/unit/test_weather_examples.cpp lists every weather scene by name. | 5: frame ok (2026-10-14 review) |
| Snow | **B** | trivial | 2 | 0 | 0 | 0 | 0 | 0 | 0 | no | tests/unit/test_weather_examples.cpp lists every weather scene by name. | 5: frame ok (2026-10-14 review) |
| Sonic Abstract - 8-Bit Ocean | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | yes: effect type lightBeam (no test names it) | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. Sole example of effect type lightBeam (no test names it). | 5: frame ok (2026-10-14 review) |
| Sonic Abstract - Cel-Shaded Dream World | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic Abstract - Chromatic Topography | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic Abstract - Digital Alpine | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic Abstract - Glitch Signal | **B** | small | 0 | 1 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic Abstract - Infinite Color Geometry | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | yes: post kind saturate | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. Sole example of post kind saturate. | 5: frame ok (2026-10-14 review) |
| Sonic Abstract - Neon Vector World | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic Abstract - Organic Digital Garden | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic Abstract - Sacred Geometry Flight | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic Garden | **B** | small | 0 | 0 | 0 | 0 | 10 | 0 | 2 | no | Tools only (sonic_garden_review/look/variants); its seven audio clips are generated (tools/make_sonic_material.py) and absent on the owner checkout too. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Abyssal Bloom | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Aurora Tundra | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Cymatic Plate | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Datascape | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Ember Forest | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Event Horizon | **B** | small | 0 | 0 | 0 | 0 | 1 | 0 | 0 | yes: effect type gravLens (1 test) | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. Sole example of effect type gravLens (1 test). | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Feedback Mirror | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Ferrofluid Crown | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 2 | yes: effect type ripple, sprite rect | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. Sole example of effect type ripple, sprite rect. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Lantern Lake | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Salt Flat Mirage | **B** | small | 0 | 0 | 0 | 0 | 1 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Silk Theatre | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Stellar Nursery | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 3 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Storm Cell | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | yes: effect type lightning (no test names it) | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. Sole example of effect type lightning (no test names it). | 5: frame ok (2026-10-14 review) |
| Sonic VFX - Tesla Choir | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | yes: effect types arc, discharge, electricField (no test names electricField) | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. Sole example of effect types arc, discharge, electricField (no test names electricField). | 5: frame ok (2026-10-14 review) |
| Sonic VFX - The Breathing Deep | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | yes: effect types pulse, breathing | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. Sole example of effect types pulse, breathing. | 5: frame ok (2026-10-14 review) |
| Sonic VFX - The Corrupted Cathedral | **B** | small | 0 | 0 | 0 | 0 | 0 | 0 | 0 | no | Live switcher member; only count/name assertions and the tools/sonic_vfx generator reference it. | 5: frame ok (2026-10-14 review) |
| The Cathedral | **B** | trivial | 0 | 1 | 1 | 0 | 0 | 0 | 1 | no | Only in the two skip-tolerant example lists. | 5: frame ok (2026-10-14 review) |
| The Living Constellation | **B** | trivial | 0 | 0 | 1 | 0 | 0 | 0 | 9 | no | Only in the GPU determinism list. | 5: frame ok (2026-10-14 review) |
| The Temple | **B** | trivial | 0 | 1 | 1 | 0 | 0 | 2 | 0 | no | Example lists; also the doc example in src/app/examples.hpp:6 (comment). | 5: frame ok (2026-10-14 review) |
| Wanderer - Behaviour and Navigation | **B** | trivial | 0 | 0 | 1 | 0 | 0 | 0 | 1 | no | Only in the skip-tolerant GPU determinism list. | 5: frame ok (2026-10-14 review) |
| Weather front | **B** | trivial | 2 | 0 | 0 | 0 | 0 | 0 | 0 | no | tests/unit/test_weather_examples.cpp lists every weather scene by name. | 5: frame ok (2026-10-14 review) |
| Worlds (preset morph) | **B** | trivial | 0 | 1 | 1 | 0 | 0 | 0 | 5 | no | Only in the two skip-tolerant example lists. | 5: frame ok (2026-10-14 review) |
| Alien - Skinned Character | **C** | medium | 3 | 0 | 5 | 0 | 0 | 0 | 5 | no | GPU and unit tests load characters/alien.scene.json directly for skinning/locomotion. | 5: frame ok (2026-10-14 review) |
| Bioluminescent Valley (recipe) | **C** | small | 1 | 0 | 0 | 0 | 0 | 0 | 2 | no | test_asset_library loads it as the recipe fixture. | 5: frame ok (2026-10-14 review) |
| Character Animation Lab | **C** | small | 0 | 0 | 0 | 0 | 0 | 2 | 5 | no | Animation Lab fixture in the labs registry. | 5: frame ok (2026-10-14 review) |
| Glowmere Atmospherics | **C** | medium | 6 | 0 | 0 | 0 | 5 | 0 | 12 | no | Unit tests read its scene/project (atmospherics, material program, farm locomotion sweep). | 5: frame ok (2026-10-14 review) |
| Glowmere Valley | **C** | medium | 3 | 0 | 0 | 1 | 0 | 2 | 33 | no | Unit tests and a perf tool load terrain.scene.json; heavy doc history. | 5: frame ok (2026-10-14 review) |
| Glowmere Valley 3 | **C** | medium | 0 | 0 | 0 | 0 | 8 | 0 | 29 | yes: project directingPlans, cue/return beats, song shot types establishing/groove/peak/drop..., colorCycling effect | Generated by tools/make_glowmere_valley_3.py and analysed by tools/gv3/*; sole user of directingPlans/colorCycling/cue. | 5: frame ok (2026-10-14 review) |
| Hero Metal | **C** | small | 0 | 1 | 0 | 0 | 0 | 0 | 6 | no | test_project_lifecycle uses it as project B. | 5: frame ok (2026-10-14 review) |
| Hyperspace | **C** | small | 0 | 2 | 2 | 0 | 0 | 0 | 1 | no | A GPU image-formation test renders it, plus example lists. | 5: frame ok (2026-10-14 review) |
| Metallic Reassembly | **C** | small | 1 | 1 | 0 | 0 | 0 | 0 | 3 | yes: `anisotropy` material kind | A material-program test asserts on its scene; sole user of the anisotropy material key. | 5: frame ok (2026-10-14 review) |
| Night Shift | **C** | medium | 15 | 0 | 0 | 0 | 15 | 5 | 13 | yes: `city` node kind (also first-block fixture), sequence shots with keys/fadeIn/fadeOut | City/sequencer showcase; unit tests and tools load it; sole user of keyed-camera sequence shots with fades. | 5: frame ok (2026-10-14 review) |
| Procedural Space (POC) | **C** | small | 3 | 0 | 0 | 0 | 7 | 0 | 11 | yes: SDF `fold`, `recurse` kinds and `random` source | test_space_example loads space.json and infinite-hall.json; sole user of fold/recurse SDF kinds. | 5: frame ok (2026-10-14 review) |
| Quality Lab: aliasing (+ dolly) | **C** | small | 2 | 0 | 0 | 0 | 0 | 4 | 5 | no | Three labs (Temporal, AOV, Rendering) name it in src/labs/lab.cpp; lab-case tests use it. | 5: frame ok (2026-10-14 review) |
| Stress Test (1K to 1M) | **C** | small | 0 | 1 | 1 | 0 | 0 | 2 | 0 | no | Integration/Stress Lab fixture in the labs registry, plus two example-list tests. | 5: frame ok (2026-10-14 review) |
| The Helix | **C** | small | 1 | 2 | 1 | 0 | 0 | 0 | 2 | no | Two AI tests load helix.scene.json as their fixture. | 5: frame ok (2026-10-14 review) |
| The Infinite Temple | **C** | small | 1 | 1 | 2 | 0 | 0 | 0 | 5 | yes: linearGradient / radialGradient kinds | GPU test at test_procedural_examples_gpu.cpp:137 and material-program table use it; directors/infinite-temple.json fixture is its director. | 5: frame ok (2026-10-14 review) |
| The Living Machine | **C** | small | 2 | 1 | 1 | 0 | 0 | 2 | 8 | no | Serialization and material-program tests assert on its exact content (twelve routes). | 5: frame ok (2026-10-14 review) |
| Tree of Life - Ocean World | **C** | small | 0 | 0 | 1 | 0 | 0 | 0 | 4 | no | A GPU water/sky test renders it. | 5: frame ok (2026-10-14 review) |
| Chorus Field (Trench god) | **D** | medium | 0 | 0 | 0 | 0 | 0 | 0 | 2 | yes: `fiber` source, `streamline` deformer, `noiseModulated` (fiber: 2 tests, streamline: 0 tests name it) | Sole user of the Fiber source and Streamline deformer (ADR-1180/1181); 114 tracked iteration files; tools/chorus/* drive it. | 5: frame ok (2026-10-14 review) |
| Digital Mosh | **D** | medium | 0 | 5 | 0 | 0 | 0 | 0 | 7 | yes: node kinds quantize, displaceField, points (only `displaceField`/`quantize` appear in 1-2 synthetic tests); grids also used by gpu-regression/mycelium and labs/grid-catchup-lab | Sole authored user of the quantize / displaceField / points node kinds; an integration test checks its three projects, and the control-seek test defaults to it. | 5: frame ok (2026-10-14 review) |
| Glowmere Valley - Painterly / Matched PBR / Lyrics | **D** | large | 10 | 12 | 4 | 2 | 7 | 9 | 104 | yes: source of the glowmere art-direction profile; glowmere.recipe.json is composed "from the same species the authored scene uses" | The reference Glowmere scene: art-direction profile values, the glowmere recipe and many tests/tools derive from it. | 5: frame ok (2026-10-14 review) |
| Glowmere Valley 2 (+ Multi-Camera, Song) | **D** | large | 101 | 18 | 24 | 7 | 45 | 9 | 106 | yes: UFO set-piece/abduction staging, directing, song-section shot types (intro/refrain/...), five-alien cast | The most-referenced scene: ~100 unit, 24 GPU, 18 integration refs, 45 tool refs, Interaction Lab fixture, CI exceptions list, effects/ufo-stack fixture reuses its multicam scene. | 5: frame ok (2026-10-14 review) |
| Liminal Euclidean World: All You Got | **D** | medium | 0 | 1 | 0 | 1 | 14 | 0 | 21 | yes: SDF stairs/warp/screw/shell/scale kinds, beatgrid source | Sole user of liminal SDF kinds (stairs, warp, screw) and beatgrid; integration test reads its scenes; 42 tracked files across 4 passes. | 5: frame ok (2026-10-14 review) |
| Renderer QA | **D** | large | 0 | 0 | 17 | 0 | 0 | 2 | 11 | yes: renderer QA coverage scene with snapshot baselines (examples/qa/baselines) | Core renderer QA fixture: Visibility Lab fixture in src, 17 GPU-suite references, baseline snapshots. | 5: frame ok (2026-10-14 review) |
| Sonic Live | **D** | medium | 0 | 1 | 0 | 0 | 7 | 4 | 7 | no | Hard-coded by display name in shared app code: leads the Sonic VFX switcher and is the "Live demo" button target. | 5: frame ok (2026-10-14 review) |
| THE RIFT (Bioluminescent Environment) | **D** | large | 0 | 6 | 0 | 0 | 0 | 2 | 10 | yes: ecosystem Environment + GridMode::Excitable propagation grid (Biovalley macro also uses the ecosystem) | First Environment (ADR-1200 seam): ecosystem layers + excitable propagation grid; 5 integration test cases (tests/integration/test_rift.cpp) incl. the ADR-1168 seek-parity regression; CI action builds its generated scene. | 5: frame ok (2026-10-14 review) |
| The Astral Forge | **D** | large | 1 | 1 | 0 | 0 | 0 | 5 | 3 | yes: `astral` scene block -> AstralRenderer (latent SDF entity, particle dust, conductor); `note` kind | Sole user of the `astral` scene block: the Astral Forge renderer and conductors exist for this scene only; three tests name its live project. | 5: frame ok (2026-10-14 review) |
| Tree of Life - Floating Island / Celestial Night | **D** | large | 4 | 0 | 8 | 0 | 12 | 2 | 22 | no | Hero asset showcase with 12 test refs (4 unit, 8 GPU), tools, and ~60 asset-LOD/treeisland measurement arms derived from it. | 5: frame ok (2026-10-14 review) |
| Biovalley Macro Showcase | **E** | medium | 0 | 0 | 0 | 0 | 0 | 0 | 3 | yes: ADR-1280..1292 (lobe-cloud Environment, cinematic water, 16 material slots) were built for it; ecosystem shared with the-rift | Generated and gitignored; cannot be inspected on a clean checkout; needs Blender-built meshes. | 5: frame ok (2026-10-14 review) |

The counts are reference lines (file:line) found by the search. Each line is listed in the JSON. The generic sweeps above are not counted.

## Migration notes for classes B to E

### Bioluminescent Grove (B, trivial)
- tests/unit/test_director_sightline.cpp:279 ([.probe])

### Bioluminescent World (B, trivial)
- tests/unit/test_director_sightline.cpp:276-279 kProjects ([.probe], skips invalid)

### Glowmere Density ladder (Low/Medium/Dense/Extreme) (B, small)
- tests/unit/test_ai_tools.cpp:1025 world.generate recipe "glowmere-dense"
- tools/certify.py:6
- Open question: glowmere.recipe.json is the base recipe the ladder scales; not indexed itself.

### Impossible Chamber (B, trivial)
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)
- tests/unit/test_help.cpp:755 (guarded by exists)

### Plant Grammar (B, trivial)

### Procedural Benchmark (B, trivial)
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`): drop the entry

### Procedural Geometry Lab (B, trivial)
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)

### Sonic Abstract - Glitch Signal (B, small)
- tests/unit/test_example_menu.cpp:125 CHECK(abstract.size() == 9): update the count
- tests/integration/test_project_system.cpp:786 liveEntries floor >= 28
- tests/unit/test_example_menu.cpp:102 and tests/integration/test_project_system.cpp:788 name it: swap for another member

### Sonic Garden (B, small)
- tools/sonic_garden_*.py target it
- Open question: assets/audio/sonic-*.wav are missing on the owner checkout: run tools/make_sonic_material.py (or tools/ci/generate-audio.sh) before review.

### Sonic VFX - Event Horizon (B, small)
- tests/unit/test_example_menu.cpp:122 REQUIRE(vfx.size() == 17): update the count
- tests/integration/test_project_system.cpp:786 liveEntries floor >= 28: check the floor still holds
- tests/unit/test_example_menu.cpp:103 names "Sonic VFX - Event Horizon": swap for another member
- Open question: Regenerate via tools/sonic_vfx/ after removal? (the generator lists every scene in tools/sonic_vfx/scenes/__init__.py)

### Sonic VFX - Storm Cell (B, small)
- tests/unit/test_example_menu.cpp:122 REQUIRE(vfx.size() == 17): update the count
- tests/integration/test_project_system.cpp:786 liveEntries floor >= 28: check the floor still holds
- Open question: Regenerate via tools/sonic_vfx/ after removal? (the generator lists every scene in tools/sonic_vfx/scenes/__init__.py)

### Sonic VFX - Tesla Choir (B, small)
- tests/unit/test_example_menu.cpp:122 REQUIRE(vfx.size() == 17): update the count
- tests/integration/test_project_system.cpp:786 liveEntries floor >= 28: check the floor still holds
- Open question: Regenerate via tools/sonic_vfx/ after removal? (the generator lists every scene in tools/sonic_vfx/scenes/__init__.py)

### The Cathedral (B, trivial)
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)

### The Living Constellation (B, trivial)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)

### The Temple (B, trivial)
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)
- src: comment: src/app/examples.hpp:6

### Wanderer - Behaviour and Navigation (B, trivial)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently) (ADR-088 autonomy determinism case: replace with another autonomous-entity project if removed)

### Worlds (preset morph) (B, trivial)
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)

### Alien - Skinned Character (C, medium)
- tests/rendering/test_composition_gpu.cpp:143,214,1735,4033,4114
- tests/unit/test_skeleton.cpp:489,568
- tests/unit/test_alien_locomotion.cpp:721
- -> move the scene to a tests/data fixture (still needs gitignored assets/aliens)
- Open question: Keep as a fixture even if dropped from the menu?

### Bioluminescent Valley (recipe) (C, small)
- tests/unit/test_asset_library.cpp:292: move to tests/data

### Character Animation Lab (C, small)
- src/labs/lab.cpp:45 Animation Lab fixture: keep the file as a lab fixture or repoint
- src: src/labs/lab.cpp:45 (lab registry data)

### Glowmere Atmospherics (C, medium)
- tests/unit/test_atmospherics.cpp:730 (SKIPs if missing)
- tests/unit/test_material_program.cpp:1764
- tests/unit/test_farm_locomotion.cpp:927
- tests/unit/test_director_sightline.cpp:278,367 ([.probe])

### Glowmere Valley (C, medium)
- tests/unit refs (see list)
- benchmarks/perf tool refs
- src: comment: src/scene/composition.cpp:10061
- Open question: Owner rule: Glowmere Valley is protected.

### Glowmere Valley 3 (C, medium)
- tools/gv3/*.py and tools/make_glowmere_valley_3.py target it (remove together)
- Open question: Owner rule: Glowmere Valley is protected.

### Hero Metal (C, small)
- tests/integration/test_project_lifecycle.cpp:43 projectB(): repoint to another light project (e.g. temple)

### Hyperspace (C, small)
- tests/rendering/test_image_formation_gpu.cpp:439: repoint
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)
- tests/integration/test_project_particle_authoring.cpp:87 comment only

### Metallic Reassembly (C, small)
- tests/unit/test_material_program.cpp:1627 table entry
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)

### Night Shift (C, medium)
- tests/unit (15 refs: test_city, test_sequence*, ...)
- tools/make_city_project.py, make_city_scene.py, make_city_score.py generate it
- src: comments only: src/ui/control_panel.cpp:333, control_panel.hpp:475, ui_logic.hpp:433, entity/entity.hpp:1601

### Procedural Space (POC) (C, small)
- tests/unit/test_space_example.cpp:25,121,178,273

### Quality Lab: aliasing (+ dolly) (C, small)
- tests/unit/test_lab_case.cpp:29,202
- src/labs/lab.cpp:131,139,147
- src: src/labs/lab.cpp:131,139,147 (lab registry data)

### Stress Test (1K to 1M) (C, small)
- src/labs/lab.cpp:154 Integration lab (Planned) fixture: pick another stress subject (e.g. open-vista recipe)
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)
- src: src/labs/lab.cpp:154 (lab registry data)

### The Helix (C, small)
- tests/integration/test_ai_control_plane.cpp:40 and tests/unit/test_ai_tools.cpp:50: move helix.scene.json to tests/data
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)

### The Infinite Temple (C, small)
- tests/rendering/test_procedural_examples_gpu.cpp:137
- tests/unit/test_material_program.cpp:1630
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)

### The Living Machine (C, small)
- tests/unit/test_serialization.cpp:720
- tests/unit/test_material_program.cpp:1028
- tests/integration/test_procedural_engine.cpp:122-125 example list (skips a missing file silently: `if (!fs::exists(file)) continue;`)
- tests/rendering/test_procedural_examples_gpu.cpp:73-86 determinism list (skips a missing file silently)
- src: comment: src/params/serialization.cpp:625

### Tree of Life - Ocean World (C, small)
- tests/rendering/test_water_sky_agreement_gpu.cpp:107

### Chorus Field (Trench god) (D, medium)
- No test reads the example; tools/chorus/{batch,song,song_inner,still,times}.sh call build.py and the projects
- Open question: Keep only p8-god-trench (the show) and drop the 56 p1..p7 iteration projects? They are regenerable by build.py.

### Digital Mosh (D, medium)
- tests/integration/test_digital_mosh.cpp:21 (all three projects): delete with the scene, or keep one generated project as a fixture
- tests/integration/test_control_seek.cpp:165 defaults to examples/digital-mosh/digital-mosh.json (env-overridable): repoint to another seekable project (e.g. the-rift or a fixture)
- tests/integration/test_project_system.cpp:787, tests/unit/test_example_menu.cpp:102: drop from must-lists
- Open question: Offline projects digital-mosh.json (Feline Footwear) and digital-mosh-trench.json are not indexed: index them or drop them?

### Glowmere Valley - Painterly / Matched PBR / Lyrics (D, large)
- tests/integration (12 refs, e.g. test_project_system.cpp:418 world loop) and tests/unit (10) load it: needs a deliberate fixture decision
- tools (make_city_score? see list) and perf tools reference it
- src: src/world/art_direction.cpp:16-18 `glowmere` ArtDirectionProfile values "read out of examples/world/glowmere-stylized.scene.json" (values copied, not loaded); src/organism/mushroom.cpp:458 findArtProfile("glowmere"); src/world/world_map.cpp:1009 defaultWorld() named "glowmere"; comments: src/scene/composition.hpp:1377, src/world/ecology.cpp:400, src/app/engine.cpp:2199, src/ui/control_panel.hpp:511
- Open question: Owner rule: never touch Glowmere Valley without approval.

### Glowmere Valley 2 (+ Multi-Camera, Song) (D, large)
- Do not remove without a dedicated migration: tests/unit/test_glowmere_multicam*.cpp, test_beam_lab, test_abduction_*, test_director_*, tests/rendering/test_shell*_gpu.cpp, ...
- src: src/labs/lab.cpp:165 Interaction Latency Lab fixture (glowmere-valley-2-multicam.json); comments citing measurements: src/app/engine.cpp:2141,2229, engine.hpp:256, interactive_resolution.hpp:10, gpu/texture.cpp:22, stage/stage_json.cpp:6
- Open question: Owner rule: Glowmere Valley is protected.
- Open question: glowmere-valley-2-song.json is not indexed.

### Liminal Euclidean World: All You Got (D, medium)
- tests/integration/test_liminal_journey.cpp:135 (liminal.scene.json), :451 (all-you-got.scene.json, SKIPs if missing)
- tools/liminal/* generators
- tools/liminal_critic.py
- Open question: Audio is ~/Desktop/All You Got.wav (owner machine only).
- Open question: Keep pass4 only?

### Renderer QA (D, large)
- tests/rendering/* (test_composition_gpu, test_resource_lifetime_gpu, test_post_artifact_forensics_gpu, ...): keep as a fixture; do not remove
- src/labs/lab.cpp:64 Visibility Lab fixture
- src: src/labs/lab.cpp:64 (lab registry data)
- Open question: Should it leave the Examples menu but stay as a fixture (move to tests/data)?

### Sonic Live (D, medium)
- tests/unit/test_example_menu.cpp:102,123 (front of the VFX set must be "Sonic Live")
- tests/unit/test_live_scenes.cpp uses a synthetic "Sonic Live" entry (no file) -- unaffected unless the name rule is removed
- tests/integration/test_project_system.cpp:788 must-list
- src: src/app/live_scenes.cpp:33 `if (e.name == "Sonic Live")` and :39 `e.name != "Sonic Live"` (name-based ordering of the switcher); src/app/application.cpp:2068 `if (ex.name == "Sonic Live") liveDemoPath_ = ex.file;` and :2081 status "the Sonic Live example was not found"; src/app/application.hpp:542 liveDemoPath_; src/ui/live_panel.cpp:108 UI text
- Open question: If removed, what should the Live panel demo button open, and should the VFX switcher stop special-casing a name?

### THE RIFT (Bioluminescent Environment) (D, large)
- tests/integration/test_rift.cpp (whole file: parameter coverage, ecosystem active, excitable grid, ADR-1168 seek/play parity x3, flight path length): move the seek-parity cases to a small committed Environment fixture before removal
- tests/integration/test_control_seek.cpp:178 comment-only provenance
- .github/actions/test-inputs/action.yml:7 and tools/ci/requirements.txt:4 exist to generate rift.scene.json on CI: remove with the scene
- src: src/scene/ecosystem.{hpp,cpp} (ADR-1200 ecosystem; also used by the Biovalley macro generator); src/rendering/environment_renderer.hpp (Environment seam)
- Open question: rift.scene.json is gitignored and 14 MB: commit it, generate it on build, or keep the generate-or-skip test?
- Open question: Does `Every example in the index exists and loads` pass on a clean checkout where rift.scene.json is absent? (coordinator to verify)

### The Astral Forge (D, large)
- tests/unit/test_astral_forge.cpp:209 reads astral-forge-live.json: point it at a minimal astral fixture (e.g. compare-t01-face.scene.json, already a GPU-test fixture) or delete with the engine code
- tests/integration/test_project_system.cpp:787 and tests/unit/test_example_menu.cpp:102 name it: drop it from the must-lists
- src: src/rendering/astral_renderer.{hpp,cpp} (ADR-1221/1222, exists for this scene); src/scene/astral_forge.{hpp,cpp}, astral_conductor.hpp, astral_song.{hpp,cpp}; src/app/interactive_resolution.cpp:647-709 astralTier live-quality levers; src/app/live_profile.cpp:387-391 astral.* profiler phases; src/scene/composition.cpp/.hpp (astral wiring, ~105 mentions)
- Open question: Cut the scene only, or the Astral engine subsystem too? The compare-t01/t02/latent fixtures and three GPU tests also use the astral path.
- Open question: Should astral-forge-trench.json (offline Trench) be indexed?

### Tree of Life - Floating Island / Celestial Night (D, large)
- tests/unit/test_treeisland_example.cpp, tests/rendering (8 refs)
- examples/assetlod/* and examples/treeisland/_* arms are copies of its scene
- src: comment: src/scene/composition.hpp:1078
- Open question: Assets (assets/treeisle GLBs) are gitignored and not reproducible; the owner holds the only copy.

### Biovalley Macro Showcase (E, medium)
- No tests reference it.
- Open question: Commit the generated shots (or a reduced macro scene) so it can be indexed?
- Open question: Live entry needs sonic.live (PAUSED.md).

The Sonic VFX, Sonic Abstract and Weather members not listed above all follow one pattern. Update the set count (`test_example_menu.cpp:122/125`) or the weather list (`tests/unit/test_weather_examples.cpp:44-45`), check the live floor (`test_project_system.cpp:786`), and remove the scene from `tools/sonic_vfx/scenes/__init__.py` so it is not regenerated. Storm Cell, Tesla Choir, 8-Bit Ocean, Breathing Deep, Ferrofluid Crown, Event Horizon and Infinite Color Geometry are each the only example of an effect or post kind (see the JSON `unique_json_features`). Removing them leaves that catalogue entry with no example scene.

