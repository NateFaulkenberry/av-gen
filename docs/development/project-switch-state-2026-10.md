# Project switch state, 2026-10

Branch `fix/project-switch-state`. ADR-1390 to ADR-1394. Before/after stills are in
`~/Desktop/av-gen-review/56-project-switch/`.

The owner reported three problems:

1. Large pale yellow blobs around every character in the Character Animation Lab.
2. Things carrying over from one project to the next. The clear case: after the Lighting Lab, Astral Forge drew
   red, green, blue and pale-yellow lines.
3. Quitting is slow after loading several projects; a memory leak was suspected.

## 1. The blob

**What it is.** The blob is the Animation Lab's skeleton overlay (`DebugViewOptions::skeletons`), drawn at the wrong
size. It is not a light, a glow, bloom or a halo.

- A debug point's `size` is a world-space diameter in metres (`shaders/debug.wgsl`). The header said "pixel size".
- The joints were drawn at `pointSize * 0.6`. At the default point size that is a **1.8 m disc on each of an alien's
  90 joints**. Together they make one blob, clipped by the floor.

**How it was confirmed.**

- `avgen --example "Character Animation Lab" --debug-draw skeletons,entityOrigins` reproduces the owner's picture
  exactly (`01-blob-before-...png`).
- The same example without overlays has no blob (`00-...reference.png`).

**Which scenes show it.** Any scene with skinned characters while the skeleton overlay is on:

- Engineering Labs > Animation, whose fixture is the Character Animation Lab;
- Engineering Labs > Character;
- before ADR-1390, any later project with characters, because the overlay carried over (see 2).

**Defect or deliberate?** The overlay is a deliberate diagnostic, and the skeleton is the Animation Lab's subject.
The size was the defect (ADR-1391):

- Joints are now 6 cm. Transform-trail samples are 9 cm (they were 1.5 m).
- IK chain ends, targets and the pelvis marker are 10-14 cm (they were 2.7-3.6 m).

The overlay stays on in those two labs and now looks like a skeleton: cyan bones, small joints (`02-...`, `02b-...`).
It is never on anywhere else, so turning it off by default was not needed.

## 2. Carry-overs found

| # | What carried | Cause | Fix | Caught by |
|---|---|---|---|---|
| A | A lab's overlays (Lighting Lab: world axes and light arrows, the owner's lines) into every later project, **and into renders started from the editor**, which copy the panel's overlays into the job | `onOpenLab` wrote the profile into `panel_->world.debug`, and nothing ever wrote it back | ADR-1390: one reset point (below) | `test_project_switch_gpu.cpp` control cases; `test_project_switch.cpp` (rule) |
| B | Astral Forge's simulation: Trench opened again in the same editor differed from a fresh render in ~11,500 of 57,600 pixels | `initParticles` cleared the splat grid but not `densTex` ("last frame's density"), which the first steps repel down. This also affected a **seek** inside Astral Forge | ADR-1393: the re-seed resolves the empty grid into `densTex` and the coarse grid | the sweep (failed for every "before" project, now exact) |
| C | Procedural objects: the Character Animation Lab's 46 x 16 m floor records drawn under the alien project's aliens (9,787 pixels) | `ProceduralRenderer` keys per-object GPU state by object **name** plus a per-scene `structureVersion`; both projects have a `floor` and the counters coincided | ADR-1394: drop per-object state when `Scene::identity` changes | the character-lab case and the alien probe |
| D | (hardening, no frame shown to differ) skinning palettes, particle pools and the temporal reset decided "new scene" by the scene's **address**, which a new project's scene can reuse | address-only comparison | ADR-1394: compare `Scene::identity` too | no test; the address is reused only by chance |
| E | Editor selection state into the next project: World panel selection and generator pick, sequence strip, composition and graph-editor selections, the viewport's selected node / pending pick / "look through camera N" (ids are per project), the transform trail, Director stills, Live Performance figures, the optimizer's Undo (it would have written the previous project's live block into this one) | no reset existed; `openAny` reset only the world editor, and an assistant's `project.open` bypassed even that | ADR-1390 `resetProjectViewState` | not visible in a frame; listed for the audit |
| F | The decoded-asset cache: every glTF and image of every project opened, held until quit | `AssetRegistry` was never emptied | ADR-1392: `beginUse`/`releaseUnused` around each scene load | `test_project_switch_resources.cpp` |

**The one reset point (ADR-1390).**

- `Engine::projectGeneration()` moves whenever a project or scene document replaces the open one (`loadProject`,
  `loadComposition`, `newProject`). It does not move on a rollback or a re-flatten.
- `app::ProjectSwitch` is observed once a frame against that counter.
- On a change, `Application::resetProjectViewState` applies the overlays the new project opens with: the session's
  `--debug-draw` set, or a lab's. A lab now *expects* its overlays before opening its fixture instead of writing them
  into the panel.
- The same call resets everything in row E plus the renderer's diagnostic entity and temporal history.
- Every way of opening a project goes through it: the menus, a file drop, a lab, an assistant tool.

**Character Animation Lab -> Astral Forge.** I could not reproduce a blob in Astral Forge. Astral Forge has no
entities, so the skeleton, origin and bounds overlays draw nothing there even when carried.

- Captures 05-08 show the labs' overlays carried into Astral Forge with no visible effect.
- The owner's blob in Astral Forge is not explained by anything I found. Carry-over B, the old simulation's density,
  changes Astral Forge's particles but does not look like a yellow blob.

**The coordinator's flake.** `test_live_switch_carryover.cpp` failed inside a full `cpu-assets` run, on Event Horizon
and Sonic Live. Cause:

- In Live mode each engine opens this machine's live control: MIDI and OSC port 9000.
- A MIDI source was connected here. Alone, the test failed 2 of 2 runs with `lights/heart/intensity 0 vs 2.05`.
- Inside a suite, the first engine binds port 9000 and the second logs `Address already in use`, so the two engines
  do not even see the same inputs.
- The failures are about what the devices send, not what a switch carries.
- Fix: both engines call `setLiveControl(false)`. It passed 3 of 3 runs afterwards.

## 3. The general test

`tests/rendering/test_project_switch_gpu.cpp` (GPU suite, tag `[switch]`):

**Probes.** Astral Forge - Trench (live-scene look), Hero Metal (simple non-live), and Alien - Skinned Character
(characters and a procedural floor). Rendered 320x180, frames 3.0 s + 6 at 30 fps.

**Fresh vs switched.**

- Fresh: a new engine and renderer.
- Switched: one engine and one renderer, the editor's. It runs `app::ProjectSwitch` and builds the overlays as
  `runLive` does. It opens each "before" project, plays 4 frames, then opens the probe.
- The last frame must be **byte-identical**.

**Why exact equality is honest.**

- The first case renders each probe fresh twice and requires identity. That held on this machine for all three.
- It then carries the Lighting Lab's overlays into each probe and requires the comparison to see them.

**"Before" sets.**

- Default: every Engineering Lab, opened as the menu opens it (fixture + overlays); every live scene; the Character
  Animation Lab. That is 46 projects x 3 probes, about 8 minutes.
- `[.sweep]`: every project in `examples/index.json`. I chose not to run it on every suite: measured about 28 minutes
  for two probes in Live mode on a GPU that two agents share.

**A separate case for character overlays.** The Character and Animation labs' overlays have nothing to draw on in the
first two probes. So a separate case opens each lab before the alien project:

- it must be exact through `ProjectSwitch`;
- with the overlays carried, it must differ.

**Knobs.**

- `AVGEN_SWITCH_LIVE=1`: the editor's engine mode.
- `AVGEN_SWITCH_AS_MAIN=1`: the editor before ADR-1390. Pointed at it, the default sweep fails on the Lighting,
  Shadow, Visibility, Camera and Integration labs. For example, Shadow Lab -> Astral Forge differed in 52,721 pixels
  and Lighting Lab -> Hero Metal in 4,746.
- `AVGEN_SWITCH_STILLS=<dir>`: writes the frames of every pair that differs.

**Proof it fails before the fixes.**

- Lighting Lab: the control case, plus the `AS_MAIN` run above.
- Character and Animation labs: the control half of the alien case. It differs when the overlays are carried.
- Astral Forge: the default sweep failed for every "before" project until ADR-1393.
- Alien probe: failed after the Animation Lab (9,787 pixels) until ADR-1394.

**Results with the fixes.**

- Default set: exact for all three probes.
- Live-mode sweep over every example (two probes, before the alien probe existed): exact, 648 assertions.
- Full offline `[.sweep]` with three probes: see §6.

**CPU-side companions.**

- `test_project_switch.cpp`: the overlay rule.
- `test_project_switch_resources.cpp`: asset cache A -> B -> A returns to A's own count; three more round trips do
  not grow the count or the footprint past 64 MB.
- `test_project_switch_pose.cpp`: engine poses are identical after a switch.
- `test_live_switch_carryover.cpp`: parameters, as before.

## 4. State audit

What a project switch does with each piece of state. "Reset" means reset or rebuilt when a project replaces the open
one.

Method:

- read every member of `Engine`, `Application`, `ControlPanel` / `WorldPanel` and the renderers that hold scene-keyed
  state;
- then rely on the image sweep for anything that reaches a frame. The sweep is exact across every example in both
  engine modes.

### Engine

| State | On a switch | Note |
|---|---|---|
| Serialised parameters | reset to default, then the document's (ADR-896) | |
| `post_`, `lens_`, `exposure_`, `focus_`, `temporal_`, `meterSettings_` | reset | e3dc8e9e added `temporal_` |
| Sources, routes, presets, timeline, cues, states, palette, world macros, director, layers, shader layers, effects, sequence, directing plans, song plan, auto-director, parked cut, shot spans, outputs block, render / path-trace settings | replaced from the document | |
| Live block, quality strategy, sonic block | reset; absent means default (ADR-1084/1100/1020) | |
| Audio clips / file | replaced; none in the document means silence | |
| Live audio **input**, MIDI source, OSC, tempo source (MIDI clock vs analysis) | **kept** | device choices for the session, not the project |
| Camera state (`focusState_`, exposure reset) | reset by `installController` | |
| Effect parameters | unregistered and re-registered by `installController` | |
| Field bus, history bank, trigger clock, signal deriver, response controls, transform history automation, entity/staging signals | rebuilt with the scene or reset on structure change | the sweep and `test_live_switch_carryover` agree |
| Decoded-asset cache (`registry_`) | **was kept forever; now trimmed to the new scene's** | ADR-1392 |
| `projectGeneration_` | **new**: bumped by every replacing load | ADR-1390 |

### Application and UI

| State | On a switch | Note |
|---|---|---|
| Debug overlays (`world.debug`) | **was kept; now the session's or the lab's** | ADR-1390, the owner's lines |
| World panel selection, generator pick, route focus | **was kept; now reset** | |
| World editor history and selection, `edits_` history | reset (`openAny` did; now also for opens that bypass it) | |
| Sequence strip, composition and graph-editor selections | **was kept; now reset** | indices and names into the old project |
| Viewport selected node, pending pick, gesture | **was kept; now reset** | |
| Viewport "look through camera N" | **was kept; now back to Editor** | camera ids are per project; Editor/Film choice kept |
| Transform trail subject and history | **was kept; now cleared** | |
| Director stills (pending request, session) | **was kept; now dropped** | |
| Live Performance frames, record, optimizer Undo | **was kept; now cleared** | Undo would write the old live block into the new project |
| Auto-director claim | handled by `refreshDirection` via `sceneGeneration` (ADR-582) | unchanged |
| `carryResponse_` (live scene switch) | deliberately carried, then consumed (ADR-1063) | the performer's response |
| Layout, panel visibility, preview mode, settings, live quality setting, placement tool, camera lock, projection | **kept** | session choices |
| Render queue, running render / trace jobs | kept | they render their own scratch copies |

### Renderer

| State | On a switch | Note |
|---|---|---|
| Meshes, textures, environment / IBL | re-uploaded on `Scene::identity` change | already correct |
| Temporal history, AO, temporal ring, particle pools, simulations | reset by `resetTemporalHistory` (now also called by the editor on a switch; detection now identity + address) | ADR-1394 |
| Astral Forge density texture | **was kept across a re-seed; now cleared** | ADR-1393 |
| Procedural per-object state (instances, LOD, sim grids) | **was kept by name; now dropped on identity change** | ADR-1394 |
| Skinning palettes | was address-keyed; now identity too | ADR-1394 (hardening) |
| Procedural source-mesh cache, static generator-mesh cache (`procedural.cpp`), text-mesh cache, tree cache | kept | content-keyed: correct, and bounded by distinct content opened, not by the number of opens |

## 5. Memory and shutdown

**Instruments added (ADR-1392).**

- `--open-sequence "<a;b;...>" --open-every <n>` opens projects in turn through the menus' own handlers
  (`lab:<key>`, `example:<name>`, or a path).
- Before each open it logs `phys_footprint` and resident memory. On Apple silicon the footprint includes Metal
  allocations.
- `main` logs `shutdown: N ms`, and `~Application` names any step over 50 ms.

**The cycling run.** Glowmere Valley 3 -> Astral Forge Trench -> Character Animation Lab -> Lighting Lab -> THE RIFT
LIVE, four times (20 opens, 60 frames each), one process, 1280x800 window. Before = the same binary with
`AVGEN_KEEP_ASSET_CACHE=1` (the cache as on main), measured before ADR-1394.

| | start | after cycle 1 | cycle 2 | cycle 3 | cycle 4 | mean of opens 6-20 | peak | shutdown |
|---|---|---|---|---|---|---|---|---|
| footprint, before | 699 MB | 1548 | 2446 | 2299 | 2555 | 2589 MB | 3189 MB | 117 ms |
| footprint, after ADR-1392 | 623 MB | 1159 | 1845 | 2091 | 1879 | 2241 MB | 3119 MB | 114 ms |
| resident, before | 399 MB | 1484 | 2370 | 2200 | 2474 | | | |
| resident, after | 332 MB | 1127 | 1831 | 2079 | 1875 | | | |

**What the numbers say.**

- **Nothing grows without bound.** Repeated cycles plateau: about 2.4-2.6 GB before and about 1.9-2.1 GB after. The
  cache trim saves about 350-400 MB of steady state; the peak (Glowmere Valley 3 itself) is unchanged.
- The ~1.4 GB rise from start to plateau is first-use cost: shader pipelines, the largest scenes' buffers,
  allocator high-water. It does not repeat per cycle.
- The resource-count regression check is `test_project_switch_resources.cpp`. A -> B -> A returns the cache to
  exactly A's 13 entries. With the release disabled it fails: 13, then 15 after B, then 15 after A again.

**Shutdown.**

- Teardown took **0.10-0.27 s** in every measured run: four cycling runs, four heavy sequences quit by `SIGTERM` at
  20/33/47/61 s, and six live-scene sequences quit at 45 s.
- **Once in nine `SIGTERM` quits it took 39 s** (`shutdown: 39055 ms`), after a run of live scenes. I could not
  reproduce it, and that run was before the per-step timing existed, so I cannot say what it waited on.
- The asset cache is not the cause of slow quits: 114 vs 117 ms with and without it.
- The next occurrence will log the member that was waited on (`shutdown: <member> took N ms`).

## 6. Validation

All runs are on this branch after merging origin/main 4421568b, on the final source (2e143411; only this document
changed after).

| Run | Result |
|---|---|
| `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/ci/run-suite.sh gpu` | **exit 0**: 661 passed, 0 failed, 1 skipped (NDI runtime), 662/662 ran, 48m 49s; the default switch sweep was the slowest case at 628 s |
| `tools/ci/run-suite.sh cpu-assets` (run under the GPU lock) | **exit 0**: 4255 passed, 0 failed, 19 skipped, 1 expected failure (the documented `[!shouldfail]` slope lean), 4275/4275 ran, 19m 25s |
| `python3 tools/check_project_integrity.py` | **exit 0**, 0 problems |
| `[.sweep]`, every project in the index before each of the 3 probes, offline | **exit 0**, 639 assertions, 1137 s |
| `[switch]` in Live engine mode (`AVGEN_SWITCH_LIVE=1`), sweep included, two probes, before the alien probe and ADR-1394 existed | exit 0, 648 assertions, 1710 s |

## 7. Left open

- **The 39 s quit.** Not reproduced in eight more attempts; now instrumented.
- **The owner's blob in Astral Forge after the Character Animation Lab.** Not reproduced. The labs' overlays draw
  nothing there, and nothing else I found produces a yellow blob in it.
- **`Astral Forge Live` captured in the editor with no audio was entirely black, interface included**
  (`--capture-ui`, frame 330). Not investigated; Trench was used for the stills.
- **Static caches.** The generated-mesh cache in `scene/procedural.cpp` and the text-mesh cache never evict. They are
  keyed by content, so they grow with distinct content opened, not with the number of opens. Not changed.
- **The Live-mode sweep with the alien probe** has not been run. The offline one has (§6).
