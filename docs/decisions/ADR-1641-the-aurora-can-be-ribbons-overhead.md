# ADR-1641: The aurora can be ribbons overhead

- Status: Accepted (eng/aurora-ribbons)
- Extends ADR-230 (an atmospheric effect is a view ray; the aurora as curtains on stepped cylinders), keeps
  ADR-939's audio master over everything the aurora does with the music.
- Asked for by the Living Prism aurora valley. The reference (`~/Desktop/living prism/new-direction.jpg`) has broad
  ribbons arching and swirling across the sky, with curving bright lower edges and vertical rays rising from them.
  The curtains, standing on a straight horizontal base round the horizon, read as columns of light.
- Code:
  - `src/world/atmospherics.{hpp,cpp}`: `AuroraShape::ribbons` and the seven `ribbon*` settings, their validation,
    and `packAurora` (lanes `AuroraGpu::ribbon0/1`, zero while ribbons are off). `AuroraGpu` is 272 bytes, was 240.
  - `src/world/effects/kinds/aurora_effect.cpp`: the rows (Advanced → Ribbons) and their JSON (`shape/ribbon*`).
  - `shaders/common.wgsl` (`AtmosAurora`); `shaders/atmosphere_fx.wgsl` (`atmosAuroraRibbons`,
    `atmosRibbonJitter`; `atmosphereAuroraAt` takes the pixel angle).
  - The size assertions: `scene_renderer.hpp` (`FrameUniforms`, the `skyGround` offset), `effect_conformance.cpp`
    (`AtmosphericFrame` 2596 → 2660).
  - The three shipped projects whose saves carry an aurora (`aurora-tundra`, `glowmere-valley-2-multicam`,
    `glowmere-valley-3`) gain the new keys at their defaults, as ADR-939 did for `glints`. A save writes every field,
    and `test_effects_project` checks the file on disk is what a save writes.
- Tests:
  - `tests/rendering/test_aurora_ribbons_gpu.cpp` (`[adr1641]`):
    - off, any ribbon settings give the same sky (hash);
    - on, a different sky with lit sheets and bare sky between them;
    - a still is deterministic;
    - 1/30 s later less than 2% of pixels move by more than 24/255;
    - hidden `[measure]`: cost.
  - `tests/unit/test_aurora_audio_response.cpp` (`[adr1641]`): packing only when on, validation, JSON round-trip and
    legacy defaults.

## Decision

**A curtain is a vertical sheet standing on a curve in the ground plane** (Lawlor & Genetti, "Interactive Volume
Rendering Aurora on the GPU"). **It is rendered by marching horizontal planes through the layer it occupies** (the
raymarched Shadertoy auroras).
- **The footprint.** The curve is the mid-level line of a domain-warped noise in world (x, z), so it folds and
  swirls. A second, lower-frequency noise cuts gaps between ribbons; the dark sky between them is the silhouette.
- **The march.** The view ray crosses `ribbonSteps` planes from the lowest a lower rim can hang
  (`anchor.y + ribbonAltitude`) to the highest a top can reach. Each sample reads the footprint at its own (x, z).
  The same curve repeated up the planes is the sheet's vertical extent, and the footprint's fine noise, the same on
  every plane, is the rays rising from the rim.
- **The rim.** It rises and falls along the ribbon by `ribbonEdge` curtain heights (the warp noise), so lower edges
  curve in elevation as well as across the sky.
- **Perspective** makes curves overhead arch across the sky and converge on the horizon.

**The look reuses the aurora's own controls.** These all apply unchanged:
- the three height-graded colours, intensity, opacity, edge brightness and filaments;
- the flow speeds (pattern drift, fold churn, the folds' climb);
- the spectrum shaping the tops by bearing, and the five audio depths under the ADR-939 master;
- the colour cycle and bloom weight.

The cylinders' own settings (radius, layer spacing, base height, waves, curtain count) do nothing in ribbon mode.
Off is the default, and while off the ribbon lanes pack to zero and the curtain code is the code it was.

**Making a few planes look continuous:**
- **Jitter.** The plane offsets are jittered per pixel by an integer hash of the view direction quantised by the
  pixel's angle. The noise is locked to the sky (it turns with the camera, not the screen), and a still is a
  function of its time.
- **Widening.** Where the samples along a ray are further apart in the footprint than a sheet is thick (low rays,
  towards the horizon), the sheet is widened to about three samples across, and dimmed by the same factor.
- **Rim onset.** The rim's onset is never sharper than about 1.5 plane spacings.
- **Weighting.** Each sample stands for `dy / rd.y` metres of path, divided by the sheet's thickness, so a sheet
  crossed square-on sums to about 1 whatever the plane count.
- **Compositing.** Samples are composited front to back with "Curtain opacity", so the many sheets a low ray crosses
  read as layers, not as one haze.

## Evidence (M2 Max, AC power, Release; media `~/Desktop/av-gen-review/89-aurora-ribbons/`)

**Unchanged by default.** The aurora valley (the scene branch's project, unchanged; offline tier, 2x supersample,
1080p, t = 40 s) renders pixel-identical to main 2d211c8e. All aurora and atmospherics GPU tests pass, including
ADR-939's audio-response identities.

**The look.** The same project with both auroras switched to ribbons (settings in `demo-aurora-shapes.json`):
- **t = 100 s:** broad green and pink arcs sweep overhead with bright lower rims and rays.
- **t = 40 s:** ribbons with rays rising from curving rims.
- Files: `aurora-t40-curtains-vs-ribbons.jpg`, `aurora-t100-curtains-vs-ribbons.jpg`, `reference-new-direction.jpg`.

**Sampling.** `sampling-grain-banding-soft-shipped.png` shows the same crop at t = 100 s four ways:

| version | result |
|---|---|
| sheets widened by a factor of 2 | grain |
| a fixed jitter | horizontal banding: each plane drew its own copy of the sheet |
| widened by a factor of 8 | no grain, every sheet blurred |
| **shipped: a factor of 3** | crisp, with a fine grain the supersample averages |

- 128 planes instead of 96 did not change the grain: it was a sheet-width question, not a plane-count one.
- The width units were the defect: the sheet's width is in the footprint line's units, which climb about five per
  unit of footprint.

**Cost** (hidden `[measure]`: 1080p, a mostly-sky frame, the GPU frame's median over 4 interleaved reps of 100
frames). A Blender session was using the GPU, so the numbers scatter:

| arm | GPU frame (ms) |
|---|---|
| no aurora | 1.9-2.9 |
| curtains (3 shells) | 4.1-7.2 |
| ribbons, 24 planes | 9.2-13.3 |
| ribbons, 48 planes | 12.0-14.7 |

- Ribbons cost about +5 ms (24 planes) to +7 ms (48 planes) over the curtains on a frame that is mostly sky.
- On the Living Prism's offline frame (about 0.3 s) that is negligible. Live, 16-24 planes ("Quality").

## Consequences

- The scene agent can switch an aurora to ribbons and shape them by altitude, fold size, width, swirl, rim variation
  and fade distance. The palette routes onto the colours keep working.
- A ribbon aurora at 1080p without supersampling shows a fine grain on the sheets. Offline supersampling averages it.
- The ribbons stand above the anchor. A camera-anchored aurora keeps its altitude above the camera but its
  footprint is in world space, so the ribbons have parallax as the camera travels.
- `FrameUniforms` grew 64 bytes.

## Rejected

- **A curtain base following a noise path in azimuth and elevation, on the existing cylinders.** It curves the
  base, but the curtains stay on a ring round the viewer: no ribbon can cross overhead or recede into perspective,
  which is the reference's whole shape.
- **Ribbons along authored 3D splines.** The look wants many folded sheets, not a few authored lines. A spline
  footprint is a later option for a hero ribbon (the same march, a distance to the spline instead of a noise level).
- **Distance-field (sphere-traced) stepping to the footprint.** Fewer samples in the gaps, but the noise footprint has
  no cheap exact distance. Plane marching with an early skip in the gaps was simpler and bounded.
- **Temporal accumulation** to remove the grain: the engine has no TAA.

## Revisit triggers

- A live scene wants ribbons at full frame rate: lower the plane count, or march at half resolution with the
  upscaler.
- A hero ribbon must follow a path: give the footprint a spline-distance mode.
- The grain matters at native resolution: blue-noise jitter by screen position, or accumulate when a TAA arrives.
