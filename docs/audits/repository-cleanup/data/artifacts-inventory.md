# Generated artifacts and repository hygiene: inventory

Audited 2026-10-14 at `ed9926db`. To check whether outputs are up to date, I ran selected generators in a
**throwaway copy** of HEAD (`git archive HEAD` extracted into the scratchpad, never this worktree) and diffed
their outputs against HEAD. Generators that need the built engine (`build/release/tools/avgen_*`), Blender or
the gitignored assets could not run there; those are marked "not checked". The scene-by-scene dependency
graph belongs to the scene-inventory audit and is not repeated here.

## 1. Generators and their outputs

| Generator (regenerate command) | Source of truth → outputs | Outputs tracked? | Deterministic? | Up to date at HEAD? |
|---|---|---|---|---|
| `python3 tools/make_glowmere_score.py assets/audio/glowmere-valley.wav --seconds 90`, `make_city_score.py`, `make_sonic_material.py` (all run by `tools/ci/generate-audio.sh`) | the scripts plus `assets/audio/manifest.json` (command and sha256) → `assets/audio/*.wav` | no (`/assets/audio/*.wav`) | yes, byte-exact by design. The sonic scores need numpy 2.0.2 (`tools/ci/requirements.txt`). | **yes**: all 9 local wavs match the manifest sha256 |
| `python3 tools/make_test_audio.py <out.wav>` | script → test track | no | yes (documented) | n/a |
| `python3 tools/refresh_scene_fingerprint.py [scene…]` | scene file → `sha256`/`size` stamped into every project that references it | stamps tracked projects | yes | **yes**: `check_project_integrity.py` reports 0 problems (12 projects fingerprint a scene) |
| `python3 tools/check_project_integrity.py` | validator, read-only | n/a | yes | exit 0 |
| `python3 examples/chorus-field/build.py` | `build.py` ("never edit the generated JSON") → `examples/chorus-field/*.json` (57 files) | yes | yes (second run identical) | **NO**: 7 committed `p7-god-*.scene.json` differ. The generator adds a `brow` attractor field and the committed files lack it (147+/154−). Either the generator was edited after the last build or the JSON was hand-edited. |
| `python3 tools/make_foot_ik_lab.py` | derived from a Glowmere scene → `examples/labs/footik/foot-ik-lab{,-off}.scene.json` | yes | yes | **NO**: `volumeDensity 0.006→0.009`, `volumeScattering 0.5→0.6`, `volumeMaxDistance 320→220`. The derived lab is stale against its source scene. |
| `python3 tools/make_glowmere_valley_2.py`, `make_multicam_demo.py`, `make_song_demo.py` | description → `examples/world/glowmere-valley-2{,-multicam,-song}.scene.json` (+ projects) | yes | not established | **NO, but that is expected**: about 30-40k changed lines each. These scenes are later post-processed by `make_abduction_scenario.py` and `splice_farm_animals.py` (which needs `avgen_place_farm_animals` and the purchased farm GLBs), so the first step alone cannot reproduce them. The pipeline is multi-step and not documented in one place. |
| `python3 tools/make_glowmere_valley_3.py && python3 tools/gv3/ufo.py --no-trace` | GV2 multicam + `tools/gv3/*` → `examples/world/glowmere-valley-3{,.scene}.json` | yes | yes. The QA pass found it byte-identical (`docs/qa-pass/clean.md` T1). | not checked (needs `avgen_world_preview` and `avgen_cast_trace`) |
| `python3 tools/make_glowmere_atmospherics.py` | → `examples/world/glowmere-atmospherics{,.scene}.json` | yes | ? | not checked (needs `avgen_atmos_presets`) |
| `python3 examples/bioluminescent/build.py [--scene-only]` | → `examples/bioluminescent/rift.scene.json` (gitignored; `test_rift` generates it) + projects | scene no, projects yes | ? | not checked (needs `avgen_world_preview` and numpy) |
| `python3 examples/digital-mosh/build.py` | → `examples/digital-mosh/*.json` | yes | ? | not checked (needs `avgen_world_preview`) |
| `python3 examples/biovalley/valley.py`; Blender `examples/biovalley/hero_assets/build.py` | → `examples/biovalley/{meshes,sky,shots}/` | **no** (`examples/biovalley/.gitignore`) | ? | cannot run on a clean checkout: needs `meshes/env/*.glb` from Blender. `PAUSED.md:63` cites a `hero_assets/README` that does not exist. |
| `python3 tools/liminal/make_all_you_got_pass4.py --no-validate` (and pass 1-3), `tools/make_liminal_example.py` | → `examples/liminal/*.json` | yes | yes | **yes** (no diff) |
| `python3 tools/make_space_presets.py` | → `examples/space/*` | yes | yes | **yes** |
| `python3 tools/phase_d/make_pasture.py`, `make_autonomy_demo.py` | → `examples/labs/character/*.scene.json` | yes | yes | **yes** |
| `python3 tools/make_tractor_beam_lab.py`, `make_city_scene.py`, `make_city_project.py`, `make_glowmere_fireflies_materials.py`, `gen_match_crowd.py` | → their example JSON | yes | yes | **yes** |
| `tools/sonic_vfx/scenes/*.py` | → `examples/sonic-abstract/*` (scene JSON + `meshes/*.glb`, about 12 MB tracked) | yes | ? | not checked |
| `python3 tools/make_bench_scenes.py` | `examples/world/terrain.scene.json` → `examples/world/_bench/` | no (ignored) | yes (docstring) | n/a |
| Arm generators: `make_cosmickey_arms.py`, `make_treeisland_arms.py`, `make_fog_arms.py`, `make_fog_primitive_arms.py`, `make_height_fog_arms.py`, `make_vortex2_arms.py`, `make_mcperf_arms.py`, `make_review_arms.py`, `make_assetlod_arms.py`, `make_gv3_state_arms.py`, `make_fx_bench_scenes.py`, `make_tornado_showcase.py` | deliverable → `_xxx-*.json` arms beside it | mixed: fog, vortex and bench arms ignored; `_ck-*`, `_arm-*` and similar tracked | yes | not checked. `make_vortex2_arms.py` is known broken against today's deliverable (`.gitignore` comment). |
| `tools/make_aliens.sh`, `make_farm_animals.sh`, `make_astronaut_musicians.py`, `import_tree_isle.py`, `import_hdri.py`, `fetch_polyhaven.py` | owner's `.blend` / BlenderKit / Poly Haven → `assets/{aliens,farm,musicians,treeisle,environments,hdri,nature,terrain}` | **no** (purchased or bulk; the manifests are tracked) | n/a | n/a |
| `cmake/build_info.hpp.in` → `avgen_build_info` target | configure/build-time header | no (build dir) | yes | n/a |
| `tools/experiment.py` | → `docs/experiments/*.json` | yes (3 records) | n/a | records |
| Help content | `docs/help/*.md` + `features.json` are **hand-written**, loaded at runtime (`src/help/database.cpp:78`), enforced by `test_help` and `avgen_help_lint` | yes | n/a | `test_help` decides. No CLI-flag check exists (findings C4). |
| `.avgen-manual/` (handbook staging) | ignored. No generator is committed, and only `.gitignore:115` mentions it. | no | ? | unclear |
| `src/scene/tree_generated.{hpp,cpp}` | **not generated** (hand-written, ADR-175) | n/a | n/a | n/a |
| Shaders | `shaders/*.wgsl` are hand-written and loaded at runtime. **No generated shader files.** | | | |

**What the convention should take from this:** a generator is up to date only if someone re-ran it. Two
derived example sets (chorus-field p7, foot-IK lab) have drifted with nothing noticing. The cheap guard is a
"regenerate and `git diff --exit-code`" check for the generators that need no engine. The ones verified clean
above are good candidates for that list.

## 2. Large tracked binaries (>200 KB)

183 binaries are tracked, 100.6 MB in total (png 75.8 MB, glb 19.7 MB, jpg 3.9 MB, bin 1.1 MB).

| Size | Path | Purpose | Referenced by |
|---|---|---|---|
| 6.2 MB | `assets/imported/camera_01.glb` | CC0 camera prop (Poly Haven) | **only** `assets/imported/ATTRIBUTION.md`. No scene, test or tool uses it. |
| 4.4 / 3.9 / 2.5 / 1.2 MB, 0.5 MB `.bin` | `assets/quaternius/glTF/{Bark_NormalTree_Normal,Bark_NormalTree,Rocks_Diffuse,Mushrooms}.png`, `CommonTree_1.bin` | CC0 test fixtures (owner decision 2026-09-28) | their `.gltf`, the GPU/CPU test cases |
| 3.0 / 2.4 MB (+3 smaller) | `assets/imported/Textures/Green/*.png` | `alien.gltf` textures | `alien.gltf` |
| 0.4 MB | `assets/imported/alien.bin` | CC0 alien | `alien.gltf` |
| **3.7 MB** | `05-static-reference-vs-f027-full.png` (repo root) | biovalley-2d comparison still. Owner commit `7b83fd58` ("ref"). | **nothing** |
| 25.9 MB (10 files, 0.4-3.8 MB each) | `examples/treeisland/renders/cosmicocean/*.png` | ADR-393/450 evidence for the Cosmic Ocean (removed by ADR-441) | `01-crop-the-square-lattice.png` (ADR-393:78) and `14-quarter-vs-full-diff-x20.png` (ADR-450:117). The other **8 are unreferenced**. |
| 27.2 MB (19 files, 0.9-2.1 MB) | `examples/treeisland/renders/cosmickey/{before,after}/*/frame_000000.png` | ADR-358/372 lighting before/after | about 11 named by ADR-358/372, `make_cosmickey_arms.py` or `_ck-*.json`. `before/_ck-before-{orbit,shadow,wide,under}` and `after/_ck-drift-{t0,t45}` are **unreferenced**. `_ck-drift-t0` is **byte-identical** to `_ck-view-wide`. |
| 1.9 MB, 1.1 MB ×6, 0.2-0.4 MB ×6 | `examples/sonic-abstract/meshes/*.glb` | generated meshes for the sonic-abstract scenes | their `.scene.json` + `tools/sonic_vfx/scenes/*.py` |
| 0.3-0.4 MB ×4 | `examples/bioluminescent/meshes/{crinoid_beads,crinoid_arms,fan_polyps,crinoid_chains}.glb` | THE RIFT organisms | `examples/bioluminescent/build.py` (by stem) |
| 0.76 MB, 0.27 MB | `docs/prototypes/astral-forge/img/{stills-contact,approaches-A-E}.jpg` | prototype record | `05-tests-and-assessment.md`, `04-architecture.md` |
| 0.2-0.4 MB ×3 | `docs/research/gpu-world/*.jpg` | spike record | `gpu-world-architecture-spike.md` |
| 0.26 MB | `docs/pathtrace/phase4-denoise-before-8spp.png` (+9 smaller pngs) | path tracer record | ADR-353, `pathtrace/overview.md` |

The largest tracked text files are generated scene and project JSON: `examples/liminal/all-you-got-pass{2,3,4}*.json`
(0.4-2.3 MB each, about 9 MB in total for four passes), `examples/world/glowmere-valley-3.json` (1.3 MB),
`examples/effects/ufo-stack.json` (0.8 MB). One large source file: `src/scene/composition.cpp` (733 KB).
Whether the superseded liminal passes 1-3 still need to be tracked is a question for the scene audit.

History: the pack is 376 MB. History-only blobs over 3 MB: `Stylized_Alien.zip` (11.2 MB) and `D-contact-sheet.png` (4.3 MB).
The purchased farm GLBs are in history from `4bdc42ff`. They are small, but the licence matters, and this is
documented in `docs/development/gpu-ci-private-assets.md:211`.

## 3. Hygiene table

| Item | Tracked? | Referenced? | Origin | Recommendation |
|---|---|---|---|---|
| `indtune.cpp` (0 B, root) | yes | only by `engineering-labs.md:514`, which flags it | `db2034ca` | **SAFE** `git rm` |
| `temporal-{plain,echo-3,echo-10,echo-cold,ring-full,ring-settling}.png` (98 KB, root) | yes | no. They are outputs of the `[.capture]` case in `test_temporal_gpu.cpp`, which writes to the cwd. | `e841d1e0` | **SAFE** `git rm`. Also guard with `/temporal-*.png`, or a temp-dir default. |
| `05-static-reference-vs-f027-full.png` | yes | no | owner, `7b83fd58` | NEEDS OWNER |
| `.agent/*.md` (5) | yes | no | `6afcd9e0`, frozen 2026-09-19 | **SAFE** `git rm -r` |
| `renders/output-preview/*`, `renders/treeisland/*` (7 small json/txt) | yes, force-added under the ignored `/renders/` | ADR-246, ADR-338, ADR-339 | intended (the queue is committed, the pixels are not) | keep |
| `examples/treeisland/renders/**` (28 PNG, 53 MB) | yes, against the `renders/` rule | partly (see §2) | committed before the rule | NEEDS OWNER |
| `examples/world/renders/.gitignore` | yes | n/a | holds `*` | keep |
| `docs/audits/` | untracked | this audit | | commit with the audit |
| Untracked but not ignored | only `docs/audits/` | | | `.gitignore` coverage is otherwise complete. `git status --porcelain --ignored` shows only the asset packs, `build/` and `assets/.DS_Store` as ignored. |
| Hash-identical tracked files | 8 groups, 1.54 MB wasted. The material one is `_ck-drift-t0` = `_ck-view-wide` (1.49 MB). The rest are small: identical `examples/assetlod/_arm-*` scenes, 9 identical `examples/liminal/*.rig.json`, 3 identical `examples/sonic-vfx/test/*-drums.mid`, `examples/treeisland/_ck-ctl-noglow` = `_ck-view-wide` scene. | | | Leave the small ones; generators write them deliberately. |
| `prototypes/` (root, 40 files: `gpu-world`, `astral-forge`) | yes | `CMakeLists.txt` options, off by default | spike code | belongs to the C++ audit. Note: ADRs 1140-1153 cite `prototypes/astral-forge/shaders/`, which no longer exists. |
| `docs/measurements/` (cosmic-ocean-cost.md + 3 json) | yes | ADR-450 | removed feature | keep as a record |
| Purchased assets (`assets/aliens`, `assets/farm`, `assets/musicians`) and songs | **not tracked**, ignored per file pattern | | | correct. `tools/ci/test-assets.list` copies aliens and farm into the private repository (NEEDS OWNER, findings §5). |
