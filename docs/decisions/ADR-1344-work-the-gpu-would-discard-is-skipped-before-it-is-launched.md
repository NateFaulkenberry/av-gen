# ADR-1344: Work the GPU would discard is skipped before it is launched; THE RIFT's live body limits are live-only

- Status: Accepted (2026-10-15), the owner's request ("see if it can be optimized to hit 30 fps target for live playback")
- Report: `docs/prototypes/bioluminescent/live-30fps-report.md`
- Code:
  - `src/rendering/environment_renderer.hpp`: `EnvironmentFrame::haveCamera`, `viewProj`, `cameraPos`, `cameraForward`;
  - `src/rendering/ecosystem_renderer.{hpp,cpp}`, `shaders/ecosystem.wgsl`: the host cull and the host list;
  - `src/rendering/scene_renderer.{hpp,cpp}`: the camera on the frame, pass arm `ecohostcull`;
  - `shaders/volume.wgsl`: `frameLightMask`;
  - `examples/bioluminescent/build.py` (`bodies`, `LIVE_LOD`), `examples/bioluminescent/rift-live.json`.
- Tests: `tests/rendering/test_ecosystem_gpu.cpp` ("the host cull dispatches only hosts that can be seen, and changes no
  pixel"), `tests/rendering/test_volume_gpu.cpp` ("A ranged light lights exactly the rays that enter its range, to the
  end of the march"), `tests/integration/test_rift.cpp` ("the bodies' live limits are live-only").

## Context

THE RIFT LIVE could not hold a 30 fps live target at 1080p on an M2 Max. The controller fell to Emergency (half
resolution), and the frame still took 37-40 ms (median), with 29-44% of frames missing their vsync. Headless at High,
the GPU took 50 ms. Measured attribution (report §2):

- **The sea pens' bodies:** 19 ms of a 51 ms frame at High. They are 25,640 dark, double-sided fronds drawn to 80 m. From
  30 to 80 m they are a few pixels tall, and their light comes from the seapenPolyps emitter layer, which keeps its own
  300 m. Removing the bodies past 30 m changes nothing visible in matched stills.
- **The bodies with no LOD:** fans, whips and crinoids had a single rung. `build.py` avoided rungs because v1's grid
  clustering made feathery organisms blocky. Mesh rungs now use meshoptimizer (ADR-078).
- **The ecosystem's emit dispatch:** it launched one thread per (host, template point), 57 M a frame. The shader then
  discarded every point of a host that was too far, behind the camera or off screen.
- **The volume march's frame lights:** at every step of every pixel, it evaluated the eight frame lights. In THE RIFT
  those are the moon and seven crown lights with ranges of 30-40 m at the canyon's head, so for most of the flight seven
  of the eight added exactly zero.

## Decision

1. **The ecosystem culls hosts on the CPU.** Each frame, `EcosystemRenderer::cullHosts` applies cs_emit's own
   early-outs to whole hosts: too far, behind the camera, and the |ndc| <= 1.05 frame test as a sphere against the four
   side planes. Each test is looser than the shader's (rounding slack, the bob's unscaled drift, the largest axis scale,
   1.06 for 1.05), so a dropped host is one whose every point the shader would have dropped. The dispatch reads a list
   of the survivors' original indices, and every hash keyed on the host index stays the same. The picture is the same
   bytes (GPU test). `EnvironmentFrame` carries the frame's camera for this. Without it (`haveCamera` false), nothing
   is culled. Pass arm `ecohostcull` switches the cull off for an A/B.
2. **The volume march skips frame lights a pixel's ray never reaches.** `frameLightMask` tests once per pixel whether
   the ray segment [0, maxDistance + one step] enters each ranged light's sphere (range x 1.001 + 1 cm). A light outside
   its range adds exactly zero (the window is clamped to 0), so skipping it changes no value. Directional lights and
   lights with no range are always kept. The GPU test fails with a mask that stops 15% short of the march's end.
3. **THE RIFT's live body limits are live-only by construction.**
   - **The sea pens stop at 30 m.** `maxDistance` is a distance cull, which an offline render lifts (ADR-186), so this
     limit applies only live.
   - **The fans, whips and crinoids have three LOD rungs.** An offline render keeps LOD rungs (ADR-191), so the scene
     authors their thresholds as 0, a ladder that ends at LOD0. `rift-live.json` sets the thresholds as parameters
     (`procedural/<body>/lod/distance1|2`, from `build.py`'s `LIVE_LOD`).
   - **Checks:** `rift.json` renders the same bytes (max 1/255, the run-to-run floor). The test reads both projects as
     loaded.

Nothing here changes the live quality ladder, its levels or the controller. The target for this work is the 30 fps
choice of ADR-1341, which the Live panel sets (ADR-1360). The project does not set it.

## Consequences

- **The live result** (1080p, transport playing, M2 Max; report §4): THE RIFT LIVE now holds 30 fps.
  - **Frame time:** median 33.3 ms, P95 35.6-36.4 ms, 0-0.1% deadline misses.
  - **Level:** it settles at Low or Medium. Before, it settled at Emergency and missed 29-44% of its vsyncs.
  - **High:** headless GPU at High fell from 50 to 25 ms. Pinned live, High still misses 2.6% of frames.
- **The look:** matched realtime stills differ only in emitter twinkle and slightly less occlusion of the sea-pen glow
  at 30-80 m (0.6-3.7% of pixels by more than 8/255). The offline project is unchanged.
- **Any ecosystem gains the host cull.** Any volume with ranged frame lights gains the mask. Both are identity
  transforms, so no other scene's picture moves.
- **Not addressed: GPU clock scaling.** With the frame cap at 30 fps, the GPU idles part of each frame and Apple
  silicon lowers its clock. The measured GPU time then inflates: at Low, 21.5 ms capped against 16.4 ms uncapped and
  17.4 ms headless. The controller reads the inflated number and climbs back slowly, so it sits a level lower than the
  hardware could hold. This is a controller question for every scene at a 30 fps target, so it is left for a separate
  decision (report §6).
