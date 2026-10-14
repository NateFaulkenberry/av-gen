# Scene inventory

Audit of every scene in `examples/` at `ed9926db` (branch `cleanup/repo-consolidation`), 2026-10-14. Machine-readable version: [`data/scene-inventory.json`](data/scene-inventory.json). Removal impact: [`scene-removal-impact.md`](scene-removal-impact.md).

## Summary

- **81 genuinely different scenes**: **53 Offline**, **28 Live**. Three of them also run in the other mode (`also_supports`): THE RIFT (offline, also live), The Astral Forge and Digital Mosh (live, also offline).
- The 84 entries of `examples/index.json` map onto 75 of these scenes (several entries are variants of one scene).
- Status: indexed-reviewable **74**, generated-missing **2**, not-indexed **5**.
- Not reviewable as scenes: **171 fixture/arm files** in 13 groups, and **63 support-data files** (light rigs, materials, looks, entity profiles, graphs, library, AI scripts).
- Branch-only scenes (not on main): **4** (Astral Furnace, Astral Forge 2, THE ROOTWORKS, Biovalley 2D / Wetland of Veils).

Counts by family: Astral Forge 1, Digital Mosh 1, The Rift 1, Sonic Garden 1, Sonic VFX 16, Sonic Abstract 9, Benchmark 2, Lab 11, Look development 7, Performance 2, Showcase 19, Weather 8, Chorus Field 1, Liminal 1, Biovalley 1.

## How the variants were grouped

There are 224 `avgen-project` and 256 `avgen-scene` files under `examples/`. A unique scene is one distinct world (one scene file, or one generator). The following were folded into their parent rather than counted:
- **Projects over the same scene file**: lighting, grade, song or camera variants. Examples: Glowmere Painterly / Matched PBR / Lyrics (one scene), Tree of Life Floating Island / Celestial Night, THE RIFT / THE RIFT LIVE, Astral Forge Live / Trench, Digital Mosh / Live / Trench, the Sonic Garden timbre variants.
- **Camera variants with their own scene copy**: Glowmere Valley 2 Multi-Camera and Song, Quality Lab aliasing dolly.
- **Phase iterations and passes**: Chorus Field p1..p7 and dbg-roll (56 projects) under the p8 Trench show; Liminal passes 1..4 and sub-segments (10 projects) under pass 4, the final; the seven Procedural Space presets under the POC.
- **Recipe ladders**: the four Glowmere Density recipes plus the base `glowmere.recipe.json`.
- **A/B arms, measurement arms, lab and GPU fixtures** are listed separately under [Fixtures](#fixtures). They are not reviewable scenes.

The two biggest variant counts are Chorus Field (114 tracked files) and Liminal (42 tracked files). Both are regenerable from their generators.

## Needs attention

### Generated, missing on a clean checkout

| Scene | Missing file | Command | Prerequisites | Indexed |
|---|---|---|---|---|
| THE RIFT (Bioluminescent Environment) | bioluminescent/rift.scene.json | `python3 examples/bioluminescent/build.py --scene-only (or full build.py)` | python3 + numpy (tools/ci/requirements.txt); build/release/tools/avgen_world_preview built (tests/integration/test_rift.cpp:55); outputs: rift.scene.json gitignored (examples/bioluminescent/.gitignore) | yes: THE RIFT (Bioluminescent Environment), THE RIFT LIVE |
| Biovalley Macro Showcase | biovalley/shots/ (scene files generated beside macro.json / macro_audio.json; names not verifiable without running valley.py) | `python3 examples/biovalley/valley.py (after an engine build)` | Blender hero-asset library: meshes/hero and meshes/env built by examples/biovalley/hero_assets/build.py + colorways.py (Blender); python3 + numpy; engine built; assets/audio/trench.wav | no |

THE RIFT is indexed in both menus while its scene is gitignored. `tests/integration/test_rift.cpp` now generates the scene (or SKIPs with the command). `.github/actions/test-inputs` installs numpy and copies `avgen_world_preview` so that CI can do the same. The Biovalley macro also needs the Blender-built hero library (`examples/biovalley/hero_assets/`), so it cannot be produced from a fresh clone without Blender.

### Reviewable but not in the index

| Scene | Category | Primary file | Why it matters |
|---|---|---|---|
| Chorus Field (Trench god) | Offline | `chorus-field/p8-god-trench.json` | Merged spike (ADR-1180..1182); the only Fiber/Streamline example |
| Liminal Euclidean World: All You Got | Offline | `liminal/all-you-got-pass4.json` | Finished music video (pass 4 final); the only liminal-SDF example |
| Biovalley Macro Showcase | Offline | `biovalley/shots/macro_audio.json` | Merged, owner-approved macro showcase (also generated-missing) |
| The Tide (leviathan) | Offline | `tide/tide.json` | Early prototype, not referenced anywhere |
| Bioluminescent Valley (recipe) | Offline | `recipes/bioluminescent-valley.recipe.json` | Recipe example used by test_asset_library and docs/world-editor.md |
| Open Vista (stress recipe) | Offline | `stress/open-vista.recipe.json` | Second renderer stress profile (docs only) |

Offline projects of indexed live scenes that have no menu entry: `astral-forge/astral-forge-trench.json`, `digital-mosh/digital-mosh.json` (Feline Footwear), `digital-mosh/digital-mosh-trench.json`. Also unindexed: `world/glowmere-valley-2-song.json`, the six extra Procedural Space presets, and the Sonic Garden variants. Proposed entries: [`data/proposed-index-additions.json`](data/proposed-index-additions.json). `examples/index.json` was not edited.

### One scene, two menus

| Scene | Live Projects | Examples |
|---|---|---|
| THE RIFT | `bioluminescent/rift-live.json` (category The Rift, indexed) | `bioluminescent/rift.json` (Showcase, indexed) |
| The Astral Forge | `astral-forge/astral-forge-live.json` (indexed) | `astral-forge/astral-forge-trench.json` (not indexed; must not use the live category "Astral Forge") |
| Digital Mosh | `digital-mosh/digital-mosh-live.json` (indexed) | `digital-mosh.json`, `digital-mosh-trench.json` (not indexed) |

`tests/unit/test_example_menu.cpp:111-118 (CHECK_FALSE at :114)` fails when a category appears in both menus. An offline entry for a live scene therefore needs a non-live category such as Showcase.

## All unique scenes

Verification level is filled in by the coordinator after load and render checks.

| # | Scene | Category | Also | Family | Status | Primary file | Variants | Index names | Key assets | Status docs | Verification level |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | The Astral Forge | Live | Offline | Astral Forge | indexed-reviewable | `astral-forge/astral-forge-live.json` | 1 | Astral Forge Live | assets/audio/trench.wav | astral-forge/README.md, astral-forge/09-production.md | TBD (coordinator) |
| 2 | Digital Mosh | Live | Offline | Digital Mosh | indexed-reviewable | `digital-mosh/digital-mosh-live.json` | 2 | Digital Mosh Live | assets/audio/feline-footwear.wav, assets/audio/trench.wav | digital-mosh/PROGRESS.md, digital-mosh/02-design.md | TBD (coordinator) |
| 3 | Sonic Abstract - 8-Bit Ocean | Live |  | Sonic Abstract | indexed-reviewable | `sonic-abstract/bit-ocean.json` | 0 | Sonic Abstract - 8-Bit Ocean |  | sonic-garden/PROGRESS-abstract.md, sonic-garden/04-brief-abstract-direction.md | TBD (coordinator) |
| 4 | Sonic Abstract - Cel-Shaded Dream World | Live |  | Sonic Abstract | indexed-reviewable | `sonic-abstract/cel-dream.json` | 0 | Sonic Abstract - Cel-Shaded Dream World |  | sonic-garden/PROGRESS-abstract.md, sonic-garden/04-brief-abstract-direction.md | TBD (coordinator) |
| 5 | Sonic Abstract - Chromatic Topography | Live |  | Sonic Abstract | indexed-reviewable | `sonic-abstract/chromatic-topography.json` | 0 | Sonic Abstract - Chromatic Topography |  | sonic-garden/PROGRESS-abstract.md, sonic-garden/04-brief-abstract-direction.md | TBD (coordinator) |
| 6 | Sonic Abstract - Digital Alpine | Live |  | Sonic Abstract | indexed-reviewable | `sonic-abstract/digital-alpine.json` | 0 | Sonic Abstract - Digital Alpine |  | sonic-garden/PROGRESS-abstract.md, sonic-garden/04-brief-abstract-direction.md | TBD (coordinator) |
| 7 | Sonic Abstract - Glitch Signal | Live |  | Sonic Abstract | indexed-reviewable | `sonic-abstract/glitch-signal.json` | 0 | Sonic Abstract - Glitch Signal |  | sonic-garden/PROGRESS-abstract.md, sonic-garden/04-brief-abstract-direction.md | TBD (coordinator) |
| 8 | Sonic Abstract - Infinite Color Geometry | Live |  | Sonic Abstract | indexed-reviewable | `sonic-abstract/color-geometry.json` | 0 | Sonic Abstract - Infinite Color Geometry |  | sonic-garden/PROGRESS-abstract.md, sonic-garden/04-brief-abstract-direction.md | TBD (coordinator) |
| 9 | Sonic Abstract - Neon Vector World | Live |  | Sonic Abstract | indexed-reviewable | `sonic-abstract/neon-vector.json` | 0 | Sonic Abstract - Neon Vector World |  | sonic-garden/PROGRESS-abstract.md, sonic-garden/04-brief-abstract-direction.md | TBD (coordinator) |
| 10 | Sonic Abstract - Organic Digital Garden | Live |  | Sonic Abstract | indexed-reviewable | `sonic-abstract/organic-garden.json` | 0 | Sonic Abstract - Organic Digital Garden |  | sonic-garden/PROGRESS-abstract.md, sonic-garden/04-brief-abstract-direction.md | TBD (coordinator) |
| 11 | Sonic Abstract - Sacred Geometry Flight | Live |  | Sonic Abstract | indexed-reviewable | `sonic-abstract/sacred-flight.json` | 0 | Sonic Abstract - Sacred Geometry Flight |  | sonic-garden/PROGRESS-abstract.md, sonic-garden/04-brief-abstract-direction.md | TBD (coordinator) |
| 12 | Sonic Live | Live |  | Sonic Garden | indexed-reviewable | `sonic-garden/sonic-live.json` | 0 | Sonic Live |  | sonic-garden/LIVE-QUICKSTART.md, sonic-garden/01-brief-live.md | TBD (coordinator) |
| 13 | Sonic VFX - Abyssal Bloom | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/abyssal-bloom.json` | 0 | Sonic VFX - Abyssal Bloom |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 14 | Sonic VFX - Aurora Tundra | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/aurora-tundra.json` | 0 | Sonic VFX - Aurora Tundra |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 15 | Sonic VFX - Cymatic Plate | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/cymatic-plate.json` | 0 | Sonic VFX - Cymatic Plate |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 16 | Sonic VFX - Datascape | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/datascape.json` | 0 | Sonic VFX - Datascape |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 17 | Sonic VFX - Ember Forest | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/ember-forest.json` | 0 | Sonic VFX - Ember Forest |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 18 | Sonic VFX - Event Horizon | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/event-horizon.json` | 0 | Sonic VFX - Event Horizon |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 19 | Sonic VFX - Feedback Mirror | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/feedback-mirror.json` | 0 | Sonic VFX - Feedback Mirror |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 20 | Sonic VFX - Ferrofluid Crown | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/ferrofluid-crown.json` | 0 | Sonic VFX - Ferrofluid Crown |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 21 | Sonic VFX - Lantern Lake | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/lantern-lake.json` | 0 | Sonic VFX - Lantern Lake |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 22 | Sonic VFX - Salt Flat Mirage | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/salt-flat-mirage.json` | 0 | Sonic VFX - Salt Flat Mirage |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 23 | Sonic VFX - Silk Theatre | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/silk-theatre.json` | 0 | Sonic VFX - Silk Theatre |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 24 | Sonic VFX - Stellar Nursery | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/stellar-nursery.json` | 0 | Sonic VFX - Stellar Nursery |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 25 | Sonic VFX - Storm Cell | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/storm-cell.json` | 0 | Sonic VFX - Storm Cell |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 26 | Sonic VFX - Tesla Choir | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/tesla-choir.json` | 0 | Sonic VFX - Tesla Choir |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 27 | Sonic VFX - The Breathing Deep | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/breathing-deep.json` | 0 | Sonic VFX - The Breathing Deep |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 28 | Sonic VFX - The Corrupted Cathedral | Live |  | Sonic VFX | indexed-reviewable | `sonic-vfx/corrupted-cathedral.json` | 0 | Sonic VFX - The Corrupted Cathedral |  | sonic-garden/SCENE-CATALOG.md, sonic-garden/PROGRESS-vfx-art.md | TBD (coordinator) |
| 29 | Procedural Benchmark | Offline |  | Benchmark | indexed-reviewable | `benchmark/benchmark.json` | 0 | Procedural Benchmark |  |  | TBD (coordinator) |
| 30 | Stress Test (1K to 1M) | Offline |  | Benchmark | indexed-reviewable | `stress/stress.json` | 0 | Stress Test (1K to 1M) |  |  | TBD (coordinator) |
| 31 | Biovalley Macro Showcase | Offline |  | Biovalley | generated-missing | `biovalley/shots/macro_audio.json` | 1 |  | assets/audio/trench.wav, examples/biovalley/meshes/hero, meshes/env | bioluminescent-valley/PAUSED.md, bioluminescent-valley/09-macro-showcase.md | TBD (coordinator) |
| 32 | Chorus Field (Trench god) | Offline |  | Chorus Field | not-indexed | `chorus-field/p8-god-trench.json` | 56 |  | assets/audio/trench.wav | chorus-field/README.md | TBD (coordinator) |
| 33 | Alien - Skinned Character | Offline |  | Lab | indexed-reviewable | `characters/alien.json` | 0 | Alien - Skinned Character |  |  | TBD (coordinator) |
| 34 | Alien Squad - Modular Characters | Offline |  | Lab | indexed-reviewable | `characters/alien-squad.json` | 0 | Alien Squad - Modular Characters | assets/aliens |  | TBD (coordinator) |
| 35 | Astronaut Musicians - Prototype | Offline |  | Lab | indexed-reviewable | `musicians/astronaut-musicians.json` | 0 | Astronaut Musicians - Prototype | assets/musicians | astronaut-musicians/PROGRESS.md | TBD (coordinator) |
| 36 | Character Animation Lab | Offline |  | Lab | indexed-reviewable | `lab/character-animation-lab.json` | 0 | Character Animation Lab | assets/aliens, assets/farm | docs/character-animation-lab.md | TBD (coordinator) |
| 37 | Farm Animals - Imported Pack | Offline |  | Lab | indexed-reviewable | `farm/farm-animals.json` | 0 | Farm Animals - Imported Pack | assets/farm |  | TBD (coordinator) |
| 38 | Procedural Geometry Lab | Offline |  | Lab | indexed-reviewable | `lab/lab.json` | 0 | Procedural Geometry Lab |  |  | TBD (coordinator) |
| 39 | Procedural Space (POC) | Offline |  | Lab | indexed-reviewable | `space/space.json` | 7 | Procedural Space (POC) | assets/audio/night-shift.wav | procedural-space/REPORT.md, procedural-space/PROGRESS.md | TBD (coordinator) |
| 40 | Quality Lab: aliasing (+ dolly) | Offline |  | Lab | indexed-reviewable | `quality/aliasing.json` | 1 | Quality Lab: aliasing; Quality Lab: aliasing dolly |  | docs/quality-lab/README.md | TBD (coordinator) |
| 41 | Renderer QA | Offline |  | Lab | indexed-reviewable | `qa/renderer-qa.json` | 0 | Renderer QA |  |  | TBD (coordinator) |
| 42 | Sonic Garden | Offline |  | Lab | indexed-reviewable | `sonic-garden/sonic-garden.json` | 7 | Sonic Garden | assets/audio/sonic-bass.wav, assets/audio/sonic-bell.wav, assets/audio/sonic-context-bell.wav, assets/audio/sonic-context-pad.wav, assets/audio/sonic-morph.wav, assets/audio/sonic-pad.wav, assets/audio/sonic-perc.wav | sonic-garden/PROGRESS.md, sonic-garden/00-brief.md | TBD (coordinator) |
| 43 | Wanderer - Behaviour and Navigation | Offline |  | Lab | indexed-reviewable | `characters/alien-wander.json` | 0 | Wanderer - Behaviour and Navigation | assets/audio/glowmere-valley.wav |  | TBD (coordinator) |
| 44 | Liminal Euclidean World: All You Got | Offline |  | Liminal | not-indexed | `liminal/all-you-got-pass4.json` | 30 |  | EXTERNAL owner-machine file: ~/Desktop/All You Got.wav | liminal-space/PROGRESS-art.md, liminal-space/REPORT.md | TBD (coordinator) |
| 45 | Bioluminescent Valley (recipe) | Offline |  | Look development | not-indexed | `recipes/bioluminescent-valley.recipe.json` | 0 |  |  | docs/world-editor.md | TBD (coordinator) |
| 46 | Bioluminescent World | Offline |  | Look development | indexed-reviewable | `world/world.json` | 0 | Bioluminescent World |  |  | TBD (coordinator) |
| 47 | Fungi | Offline |  | Look development | indexed-reviewable | `organic/fungi.json` | 0 | Fungi |  |  | TBD (coordinator) |
| 48 | Glowmere Valley - Painterly / Matched PBR / Lyrics | Offline |  | Look development | indexed-reviewable | `world/glowmere-stylized.json` | 2 | Glowmere Valley - Painterly; Glowmere Valley - Matched PBR; Glowmere Valley - Lyrics | assets/audio/glowmere-valley.wav | docs/art-direction.md | TBD (coordinator) |
| 49 | Hero Metal | Offline |  | Look development | indexed-reviewable | `hero/hero.json` | 0 | Hero Metal |  | docs/bevels-and-hero-objects.md | TBD (coordinator) |
| 50 | Organic Curves | Offline |  | Look development | indexed-reviewable | `organic/organic.json` | 0 | Organic Curves |  |  | TBD (coordinator) |
| 51 | Plant Grammar | Offline |  | Look development | indexed-reviewable | `organic/plants.json` | 0 | Plant Grammar |  |  | TBD (coordinator) |
| 52 | Glowmere Density ladder (Low/Medium/Dense/Extreme) | Offline |  | Performance | indexed-reviewable | `recipes/glowmere-medium.recipe.json` | 4 | Glowmere Density - Medium; Glowmere Density - Low; Glowmere Density - Dense; Glowmere Density - Extreme |  |  | TBD (coordinator) |
| 53 | Open Vista (stress recipe) | Offline |  | Performance | not-indexed | `stress/open-vista.recipe.json` | 0 |  |  | docs/renderer-upgrade/05-scope-contract.md | TBD (coordinator) |
| 54 | Bioluminescent Grove | Offline |  | Showcase | indexed-reviewable | `world/grove.json` | 0 | Bioluminescent Grove |  |  | TBD (coordinator) |
| 55 | Glowmere Atmospherics | Offline |  | Showcase | indexed-reviewable | `world/glowmere-atmospherics.json` | 0 | Glowmere Atmospherics | EXTERNAL owner-machine file: ~/Desktop/MP3/bass.mp3, assets/aliens, assets/farm |  | TBD (coordinator) |
| 56 | Glowmere Valley | Offline |  | Showcase | indexed-reviewable | `world/terrain.json` | 0 | Glowmere Valley | assets/hdri/kloppenheim_02_puresky_4k.hdr |  | TBD (coordinator) |
| 57 | Glowmere Valley 2 (+ Multi-Camera, Song) | Offline |  | Showcase | indexed-reviewable | `world/glowmere-valley-2.json` | 2 | Glowmere Valley 2; Glowmere Valley 2 - Multi-Camera | EXTERNAL owner-machine file: ~/Desktop/Rebuild.mp3, assets/aliens, assets/farm | docs/glowmere-valley-2 | TBD (coordinator) |
| 58 | Glowmere Valley 3 | Offline |  | Showcase | indexed-reviewable | `world/glowmere-valley-3.json` | 0 | Glowmere Valley 3 | EXTERNAL owner-machine file: ~/Desktop/Rebuild.mp3, assets/aliens, assets/farm, assets/musicians | docs/glowmere-valley-3 | TBD (coordinator) |
| 59 | Hyperspace | Offline |  | Showcase | indexed-reviewable | `hyperspace/hyperspace.json` | 0 | Hyperspace |  |  | TBD (coordinator) |
| 60 | Impossible Chamber | Offline |  | Showcase | indexed-reviewable | `chamber/chamber.json` | 0 | Impossible Chamber |  |  | TBD (coordinator) |
| 61 | Metallic Reassembly | Offline |  | Showcase | indexed-reviewable | `reassembly/reassembly.json` | 0 | Metallic Reassembly |  |  | TBD (coordinator) |
| 62 | Night Shift | Offline |  | Showcase | indexed-reviewable | `city/night-shift.json` | 1 | Night Shift | assets/audio/night-shift.wav | docs/group-d-city-brief.md, docs/sequencer.md | TBD (coordinator) |
| 63 | The Cathedral | Offline |  | Showcase | indexed-reviewable | `cathedral/cathedral.json` | 0 | The Cathedral |  |  | TBD (coordinator) |
| 64 | The Helix | Offline |  | Showcase | indexed-reviewable | `helix/helix.json` | 0 | The Helix |  |  | TBD (coordinator) |
| 65 | The Infinite Temple | Offline |  | Showcase | indexed-reviewable | `infinite/infinite.json` | 0 | The Infinite Temple |  |  | TBD (coordinator) |
| 66 | The Living Constellation | Offline |  | Showcase | indexed-reviewable | `constellation/constellation.json` | 0 | The Living Constellation | assets/audio/glowmere-valley.wav | docs/constellation.md | TBD (coordinator) |
| 67 | The Living Machine | Offline |  | Showcase | indexed-reviewable | `machine/machine.json` | 0 | The Living Machine |  |  | TBD (coordinator) |
| 68 | The Temple | Offline |  | Showcase | indexed-reviewable | `temple/temple.json` | 0 | The Temple |  |  | TBD (coordinator) |
| 69 | The Tide (leviathan) | Offline |  | Showcase | not-indexed | `tide/tide.json` | 0 |  |  |  | TBD (coordinator) |
| 70 | Tree of Life - Floating Island / Celestial Night | Offline |  | Showcase | indexed-reviewable | `treeisland/tree-of-life-floating-island.json` | 1 | Tree of Life - Floating Island; Tree of Life - Celestial Night | EXTERNAL owner-machine file: ~/Desktop/MP3/Hellopines 2.mp3, assets/treeisle |  | TBD (coordinator) |
| 71 | Tree of Life - Ocean World | Offline |  | Showcase | indexed-reviewable | `treeisland/tree-of-life-ocean-world.json` | 0 | Tree of Life - Ocean World | assets/environments/tree_of_life_day.exr, assets/environments/tree_of_life_night.exr, assets/treeisle |  | TBD (coordinator) |
| 72 | Worlds (preset morph) | Offline |  | Showcase | indexed-reviewable | `worlds/worlds.json` | 0 | Worlds (preset morph) |  |  | TBD (coordinator) |
| 73 | THE RIFT (Bioluminescent Environment) | Offline | Live | The Rift | generated-missing | `bioluminescent/rift.json` | 1 | THE RIFT (Bioluminescent Environment); THE RIFT LIVE | assets/audio/trench.wav | bioluminescent/PROGRESS.md, bioluminescent/04-design.md | TBD (coordinator) |
| 74 | Ashfall | Offline |  | Weather | indexed-reviewable | `weather/ashfall.json` | 0 | Ashfall |  | examples/weather/README.md | TBD (coordinator) |
| 75 | Dust motes | Offline |  | Weather | indexed-reviewable | `weather/dust-motes.json` | 0 | Dust motes |  | examples/weather/README.md | TBD (coordinator) |
| 76 | Fireflies | Offline |  | Weather | indexed-reviewable | `weather/fireflies.json` | 0 | Fireflies |  | examples/weather/README.md | TBD (coordinator) |
| 77 | Pollen, spores and seeds | Offline |  | Weather | indexed-reviewable | `weather/pollen.json` | 0 | Pollen, spores and seeds |  | examples/weather/README.md | TBD (coordinator) |
| 78 | Rain | Offline |  | Weather | indexed-reviewable | `weather/rain.json` | 0 | Rain |  | examples/weather/README.md | TBD (coordinator) |
| 79 | Season shift | Offline |  | Weather | indexed-reviewable | `weather/season.json` | 0 | Season shift |  | examples/weather/README.md | TBD (coordinator) |
| 80 | Snow | Offline |  | Weather | indexed-reviewable | `weather/snow.json` | 0 | Snow |  | examples/weather/README.md | TBD (coordinator) |
| 81 | Weather front | Offline |  | Weather | indexed-reviewable | `weather/weather-front.json` | 0 | Weather front |  | examples/weather/README.md | TBD (coordinator) |

Asset notes: `assets/aliens` and `assets/farm` are purchased and must never be committed. `assets/treeisle` is the owner's only copy. `assets/audio/trench.wav` and `feline-footwear.wav` are owner-supplied songs: they are not in `assets/audio/manifest.json` and cannot be regenerated. `glowmere-valley.wav`, `night-shift.wav` and the seven `sonic-*.wav` clips can be regenerated from the manifest. The `sonic-*.wav` clips are missing on the owner checkout as well. `EXTERNAL` means a file on the owner's Desktop: `~/Desktop/Rebuild.mp3`, `~/Desktop/MP3/bass.mp3`, `~/Desktop/MP3/Hellopines 2.mp3` and `~/Desktop/All You Got.wav`.

## Fixtures

These are test, lab, benchmark or A/B files, not reviewable scenes.

| Group | Files | Parent scene | What | Used by |
|---|---|---|---|---|
| assetlod-arms | 32 | tree-of-life-island | ADR-348/355 asset-LOD measurement arms (control + before/after pairs); more are gitignored and regenerated by tools/make_assetlod_arms.py | tools/make_assetlod_arms.py; hidden [.analysis] instruments tests/unit/test_asset_lod_frames.cpp (AVGEN_LOD_FRAMES, skips without) and test_asset_lod_analysis.cpp; ADR-348/355 |
| treeisland-arms | 41 | tree-of-life-island | Tree of Life A/B arms (_ck-*, _ctl-*, _glow-*, _view-*, _ca-vg-*, _compare-original); more gitignored (_fog*, _vx2-*) | tools/make_treeisland_arms.py, make_cosmickey_arms.py, make_fog_arms.py, make_review_arms.py |
| astral-prototype-arms | 7 | astral-forge | Astral Forge prototype comparison arms | tests/rendering/test_astral_port_gpu.cpp:572,585; tests/rendering/test_sdf_anatomy_gpu.cpp:215; tests/unit/test_sdf_anatomy.cpp:355 (compare-t01-face.scene.json); test_particle_latent_gpu.cpp:492 (comment, latent-entity); tempered-metal: docs only |
| imagelook-captures | 16 | - | Image-look capture fixtures (bloom, defocus, exposure, grey ramp, hue wheel, look, wide tier, disabled) | tools/make_imagelook_captures.py (writes/uses them); docs/image-look-*.md |
| gpu-regression | 6 | - | GPU regression scenes (echo-field, endless-meadow, mycelium) | tests/rendering/test_gpu_regression_scenes_gpu.cpp:145-375 |
| renderer-qa-variants | 7 | renderer-qa | Renderer QA isolation variants (character, minimal, transparency, water) and frame-snapshot baselines | tests/rendering/test_composition_gpu.cpp, test_resource_lifetime_gpu.cpp, test_character_forensics_gpu.cpp, test_post_artifact_forensics_gpu.cpp, test_water_depth_forensics_gpu.cpp; tests/support/water_bench.hpp:56 |
| engineering-labs | 50 | - | Engineering Labs fixtures: lab scenes + cases.json per lab (src/labs/case.cpp:52 builds examples/labs/<key>/cases.json); character, footik, motion, motionmatch, tornado, grid-catchup, visibility-culling scenes | src/labs/lab.cpp:39-166 registry; src/labs/case.cpp:52; tests/unit (character lab, foot IK, motion matching), tests/rendering (tornado, grid catch-up, particle, volumetric, hdr) |
| camera-lab | 2 | - | Camera/Framing Lab visual fixture: six shots, one per camera behaviour | src/labs/lab.cpp:80 (Camera Lab fixture); tests/unit/test_camera_behaviors.cpp:444; src/app/ui_script.cpp:646,659,787 (comments) |
| tractor-beam-lab | 2 | glowmere-valley-2 | Abduction/tractor-beam lab and its pre-ADR-262 legacy arm | tests/unit/test_beam_lab.cpp:97,572; tests/unit/test_abduction_sequence.cpp:45; tests/unit/test_farm_locomotion.cpp:922-928; tools/make_tractor_beam_lab.py:56 |
| world-doc-arms | 5 | glowmere-* | Glowmere measurement/assembly arms referenced only from docs | docs only (no test/tool loads them) -- removal candidates |
| city-first-block | 1 | night-shift | First city block (city node kind fixture) | tests/unit/test_city.cpp:645,700 |
| ufo-stack | 1 | glowmere-valley-2 | UFO effect-stack demo over the GV2 multicam scene (generated by a [.generate] test) | tests/integration/test_ufo_stack_demo.cpp:37; tests/rendering/test_shell_gpu.cpp:608; tests/rendering/test_shell2_gpu.cpp:625 |
| infinite-temple-director | 1 | infinite-temple | World-director knob file | tests/unit/test_world_director.cpp:195 |

Support data, also not scenes: lightrigs (21); materials (24); looks (8); entities (2); graphs (3); library (1); organisms (1); ai-scripts (2); weather-readme (1).

## Gitignored and generated paths under examples/

| .gitignore | Patterns | Generator | Kind |
|---|---|---|---|
| `examples/assetlod/.gitignore` | tree-*-r1..r4, dolly-*, floor-*, perf-* (+ .scene.json) | tools/make_assetlod_arms.py | fixture arms |
| `examples/bioluminescent/.gitignore` | rift.scene.json, rift.scene.json.*.tmp, cp1/, cp2/, diag-*, __pycache__/ | examples/bioluminescent/build.py (+ cp1.py/cp2.py) | REVIEWABLE SCENE (the-rift) + checkpoints |
| `examples/biovalley/.gitignore` | meshes/, sky/, shots/, __pycache__/ | examples/biovalley/valley.py (+ Blender hero_assets/build.py, colorways.py) | REVIEWABLE SCENE (biovalley-macro) + frozen shots |
| `examples/world/renders/.gitignore` | * (render output) | - | output |
| `.gitignore (root)` | /examples/world/_bench/, /examples/world/_flicker_*.scene.json, /examples/world/_v3*, /examples/world/.avgen_node_set_probe_*.json, /examples/treeisland/_fog-*, _fogshape-*, _fogheight-*, _fogshadow-*, _fogshadowshot-*, _fogtemporal-*, _vx2-*, /examples/quality/out/, /examples/world/out/ | tools/make_bench_scenes.py, make_fx_bench_scenes.py, make_fog_arms.py, make_fog_primitive_arms.py, make_height_fog_arms.py, make_vortex2_arms.py, make_gv3_state_arms.py | fixture/bench arms and outputs |

The worktree holds tracked files only: `git status --ignored` shows nothing ignored under `examples/`. The generated-missing scenes above are exactly what a clean checkout lacks.

## Branch-only scenes

| Scene | Branch | Tip | Category | Files | Engine diff vs main | Status |
|---|---|---|---|---|---|---|
| The Astral Furnace | `proto/astral-furnace` | 3367f704 (2026-10-08) | Live (also Offline) | astral-furnace/astral-furnace-live.json, astral-furnace/astral-furnace-trench.json, astral-furnace/astral-furnace.scene.json, astral-furnace/astral-furnace-2d-trench.json, astral-furnace/astral-furnace-2d.scene.json | 12 src files, +3116/-37 | PAUSED at cp17 (docs/prototypes/astral-furnace/PAUSED.md on the branch); owner: faces from fire, 2D path intriguing; not merged |
| Astral Forge 2 (moonlit ocean faces) | `proto/astral-forge-2` | 7c78e487 (2026-10-07) | Live (also Offline) | astral-forge-2/astral-forge-2-live.json, astral-forge-2/astral-forge-2-trench.json, astral-forge-2/astral-forge-2.scene.json | 6 src files, +614/-30 | PAUSED; faces never read; owner: repurpose the ocean renderer as a reactive water scene later; suites not run on final commit |
| THE ROOTWORKS (Rift jungle) | `proto/rift-jungle` | 72370e52 (2026-10-08) | Offline (also Live) | bioluminescent/rootworks.json, bioluminescent/rootworks-live.json, bioluminescent/rootworks.py, bioluminescent/jungle_flora.py, bioluminescent/concepts.py | 1 src file, +1/-1 | WIP paused (owner moved the bioluminescent scene to a new direction); its avgen_tests failed test_rift + test_character_lab_slopes (uninvestigated); scene presumably generated by rootworks.py (not verified) |
| Biovalley 2D / Wetland of Veils | `proto/biovalley-2d -> proto/biovalley-original (descendant)` | ed9c5b97 / 48533e44 (2026-10-12) | Offline (also Live) | biovalley-2d/biovalley-2d-live.json, biovalley-2d/biovalley-2d-trench.json, biovalley-2d/biovalley-2d-reference.json, biovalley-2d/biovalley-2d.scene.json, biovalley-2d/build.py, (original only) tools/biovalley2d/original/{wetland,cathedral,concepts,fetch_textures}.py | 12 src files, +1491/-2 | 2D reference-locked audio-reactive renderer; original-environment pass (Wetland of Veils) STOPPED for owner verdict; RESUME.md on branch |
| Biovalley water lab | `proto/bio-valley-water` | 1ee7cceb (2026-10-12) | Offline | biovalley/waterlab.py | 10 src files, +417/-86 | variant of biovalley-macro, not a separate scene; ADR-1294/1295 chosen (variant B), ADR-1296 half-done; ADR-1291 rejected (do not merge) |
| (proto/sonic-garden) | `proto/sonic-garden` | fd61230e (2026-10-03) |  |  | n/a | NO branch-only scenes: every example file on the branch also exists on main (main is newer) |
