# Talking Heads: engine and implementation research (Part B)

Prep only, 2026-10-24, branch `prep/talking-heads`, base main 250564e9. No engine code was changed. Probes were built
against this tree (`build/release`, Release, M2 Max, AC, LG at 60 Hz). Every GPU run went through `tools/gpu-lock.sh`.

**Headline.**
- **The vertical slice needs no engine change.** Every feature in the spec maps onto an existing system.
- **Mouth and eye beams that ride the head in local space already work.** Use a Light Beam owned by a spot light that
  rides the bust node. Verified in the engine: `probe/engine-beams-follow-bust.jpg`.
- **The full probe stack costs about 4-5 ms GPU at 1080p** against a 29.3 ms budget. The stack is the bust, 16-24
  beams, 2 ghost copies, the glitch tear plus RGB split, bloom and a frame echo.
- **Three small, optional engine changes** would make it cleaner (§5). None is a framework.

## 1. How a Sonic Abstract scene is made (the conventions to follow)

- **Generated, not hand-written.** Each Sonic Abstract scene is a Python module, `tools/sonic_vfx/scenes/<id>.py`, with
  `build() -> kit.Scene`. `tools/sonic_vfx/abstract.py build <module>` writes
  `examples/sonic-abstract/<id>.json` (the project) and `<id>.scene.json` (the world). A module's `INDEX_ENTRY` upserts
  the `examples/index.json` row: category "Sonic Abstract", `"live": true`, and a description that names the MIDI map.
  Live scenes need `sonic.live`, and the index entry is test-enforced (ADR-1340/1341).
- **`tools/sonic_vfx/kit.py`** is the vocabulary:
  - `Scene.proc` / `mesh` / `gltf` / `light` / `entity` / `effect` / `beam` / `route` / `map` / `program` /
    `drift_camera` / `track` / `modwheel`;
  - envelopes `FAST` / `HIT` / `SNAP` / `MEDIUM` / `SLOW`;
  - route helper `R()`, interpret mapping helper `M()`.
  `BASE_PARAMS` sets the live-friendly defaults (bloom on, echo and mosh off). `unshadowed_key()` avoids the default
  key light's shadow pass, which matters in an unlit-background scene.
- **Signals by class** come from `tools/sonic_vfx/signals.py` (`S("snare")` gives `response.snare`). The kick comes
  from `tools/drum_signals.py` (`signal.kick`, `signal.kickEnv`, `signal.drumming`, `signal.bassLevel`,
  `signal.synthLevel`). `kit.Scene.project_doc` installs whichever of those the scene reads (ADR-1620/1630).
- **Generated meshes** are committed under `examples/sonic-abstract/meshes/` (16 MB today) and named by the procedural
  source `{"kind": "mesh", "asset": "meshes/<name>.glb"}`.
- Existing scenes to read first:
  - `glitch_signal.py`: post glitch routes, Sine/Noise deformers, staged ramps;
  - `examples/digital-mosh/` (`arc.py`): states and pads;
  - `examples/rainbow-world/build.py`: control channels, signals, palette.

## 2. Reuse map: every spec feature

### 2.1 Mouth fan and eye rays: Light Beam, owned by a spot light that rides the bust

| Option | Follows head (translate+rotate)? | Points anywhere? | Verdict |
|---|---|---|---|
| **`lightBeam` owned by a Light**, where the light is an authored spot with `"node": "bust"` (composition.hpp `AuthoredLight.node`: "expressed in that node's local frame and rides its world transform") | **Yes**, verified: `probe/engine-beams-follow-bust.jpg` (bust yawed -35 degrees, rolled 12 degrees and moved; the fan stays on the mouth and turns with the head). XFORM offsets (Shake, Float) also carry attached lights (`transform_frame.hpp`) | Yes: the axis is the light's direction | **Use it** |
| `lightBeam` owned by the Entity (`light_beam_effect.cpp` `resolveBeam`) | Position only. The apex is the bottom of the drawn bounds plus a world offset, and the axis is a world yaw/tilt | No: tilt is clamped to ±89 degrees from straight down, so it cannot point up | No (gap G1 would fix it) |
| `lightBeam` owned by the World (kit `beam()`) | No | No (same clamp) | No |
| `aurora` ribbons (ADR-1641/1651/1652), `comet`, `meteors`, `stars` | World or sky only | n/a | No |
| `arc` / `lightning` / `discharge` / `electricField` (ribbon bucket) | Entity, but electric bolts | n/a | Wrong look |
| `trail` (ribbon, entity) | A motion trail behind the owner | n/a | Not an emitter |

**How one beam is set up.**
- One authored **spot light**:
  - `node: "bust"`, at a local position from the anchors (mouth or eye);
  - local direction from the fan layout;
  - `intensity` 0.01: any value > 0 keeps the beam drawn, and at 0.01 it lights nothing visibly;
  - `range` = beam length, `outerCone` = the beam's half-angle in degrees;
  - `castsShadow: false`, `contactShadow: false`.
- One **`lightBeam` effect** owned by `("light", <id>)`. `kit.Scene.effect` already takes the owner.

The beam's look is a shell (`shaders/shell_fx.wgsl` `fs_beam`): an analytic cone with a core that sharpens
(`coreSharpness`), soft sides (`edgeSoftness`), end falloff, drifting dust and a soft depth contact. It is additive, and
a quarter of it feeds bloom. That is the spec's "bright core with a restrained soft halo" with no transparent particle
overdraw.

**Routable leaves:**
- `fx/<id>/intensity`, `color`, `coreSharpness`, `edgeSoftness`, `falloff`, `endFade`, `dust`, `drift`, `aperture`
  (the source width);
- `lights/<id>/outerCone` (spread of that ray), `range` (length), `azimuth` / `elevation` (aim, in the node's frame,
  elevation ±89; routable wavering), `color` (multiplies the beam colour), `enabled`.

**Budgets:**
- the shell budget is 128 per frame (the 129th is `Dropped` with a reason);
- clustered lighting holds 32 lights per cluster, so 24 beam lights plus key and rim fit (26).

**Probe notes for tuning:**
- At these defaults the beams read as wide, pink, dusty searchlights. Thin rays need `aperture` about 0.002-0.004,
  `outerCone` 0.6-1.5 degrees, `coreSharpness` 4-6, low `dust`, `intensity` 0.6-1.5.
- The hero cone needs 10-14 degrees and `edgeSoftness` about 0.7 (01 §6).
- Fan spread in the probe was ±40 degrees; aim the art pass at ±35.

**Fade in and out, transients, sustained emission.** All are routes onto `fx/<id>/intensity` (sustained from a level,
bursts from an event through `HIT`/`SNAP` envelopes) gated by a master `signal.awake` (§3).
- Effect `timing.fadeIn`/`fadeOut` handle the beam's own life.
- **The residual trail:** the frame echo (`temporal/echo/*`, an FIR over clean frames, 0.16 ms measured). On a dark
  ground it trails only bright things. Use 2-4 frames at strength 0.2-0.4, routed up in overload.

**Mouth glow.** The mouth interior is its own GLB material, so its program can emit a routed colour that blooms. That
makes the beams look like they come out of something lit.

### 2.2 Effect A, spectral vibration: Shake + two blended ghost copies (+ echo)

- **Displacement: the `shake` effect** (XFORM, entity owner, Eiserloh trauma² noise, never per-frame random).
  - Rows: `trauma`, `amplitude`, `rotation`, `frequency`, `decay`, `seed`.
  - Route `signal.bassLevel` and `signal.kickEnv` to `fx/<shake>/trauma` with `FAST`/`HIT` envelopes. Recovery is the
    envelope's decay.
  - XFORM is evaluated **before the scene with this frame's route finals**, so it adds no latency. Children (ghosts)
    and riding lights (beams) follow.
- **Spectral copies: two `gltf` child nodes of the bust**, `parent: "bust"`, drawing `th-bust-ghost.glb` (an alpha
  BLEND material) through a program (ADR-1572, `"program"` on a gltf node). The program writes emission (magenta for
  one ghost, electric blue for the other) and a low opacity, with a fresnel term so the copy reads as an edge glow
  rather than a second solid.
  - **Separation:** route `signal.bassLevel` (and `signal.kickEnv` spikes) to `nodes/ghostA/position` +x and
    `nodes/ghostB/position` -x. Optionally add a small opposite y for directional separation. Use FAST envelopes so
    the copies snap back. Opacity is routed with them, so at rest the copies vanish inside the bust.
  - The ghosts must be **gltf** nodes: a procedural node draws blended materials opaque (ADR-1572's own finding). In
    the probe, opaque ghosts showed as hard doubled outlines, which is exactly what blending and fresnel must soften.
  - **Cost:** 2 extra draws of 30 k triangles each. Measured inside noise (§4).
- **Enrichment: the frame echo** (above) makes the vibrating silhouette leave short spectral smears without more draws.
- Not suitable:
  - **Motion Smear.** It reads velocity from HIST, which records the pre-XFORM path, so it never sees Shake.
  - **A Noise deformer at large amplitude.** That is the "vertex explosion" the spec forbids. A small amplitude (≤ 3 mm)
    on the bust is fine as surface "signal shimmer".

### 2.3 Effect B, chromatic possession: the bust's material program + entity FX lanes

- **The bust's program** (porcelain plus wash; 01 §6). The wash is an `oklabRamp` of the palette roles over a slow
  scrolling `noise` in `localPosition`. It is mixed over porcelain by a routed amount
  (`material/<prog>/op/<n>/mix/value` or a constant feeding `mixBy`), and concentrated round the mouth and eyes by
  smooth object-space masks. ADR-1470 (OKLab ramp) and ADR-1162 (quantize) are available.
- **Entity FX lanes on the bust** (`entity_fx.hpp`). These are per entity, so they don't touch other users of the
  material:
  - **`glow`:** `tint` (own-emission recolour), `glow` (self-glow), `rim` / `rimColor` / `rimPower`. Mids drive the
    wash brightness and transients flash it;
  - **`pulse` in Travelling mode:** a colour band crossing the head on an axis, triggered on snares (`rate`, `peak`,
    `bandWidth`, `sharpness`);
  - **`colorCycling`:** a restrained `range` (≤ 0.15 turn) as a hue swing on transients. Never a free-running rainbow;
  - **`rimLight` / `fresnel`:** the cool silhouette separation.
- **Palette transitions:** ADR-1420's palette selector (blends directly, advances on the music) or a slow route on the
  ramp's position. Overload swaps in the acid-red and white accents.
- **Recovery** to porcelain is the route's release (SLOW 2.4 s, or 3-5 s) plus `signal.awake` falling.

### 2.4 Effect C, localized signal tearing: what the engine can and cannot do

| Route | Local to the bust? | Band offsets + chroma? | Engine change | Verdict |
|---|---|---|---|---|
| **T1** `post/glitch/tear` + `tearShift` + `post/split/amount` (ADR-1065: full-screen, a pure function of time, 0.05 ms) | **Visually, mostly.** On the near-black ether, torn rows of background are invisible, so the tear shows on the bust and the beams. Probe still: `engine-cost-probe-full-stack-A4.png`. Haze and particles tear faintly | Yes | None | **Slice default.** Route snare and strong onsets with `SNAP`/`HIT` envelopes, kept brief |
| **T2** Material bands: `quantize` (ADR-1162) on `localPosition.y` x a time epoch hashes band ids, which flash, hue-shift or offset the wash per band | Strictly | Colour only, no displacement | None | Layer it with T1 for the corrupted-signal surface |
| **T3** Sine deformer on the bust: axis Y, displacement X, high frequency, phase jumped by an integrating route (as `glitch_signal.py` does) | Strictly (vertex stage, bust only) | A smooth shear wobble, not hard bands | None | Fine-tear on the highs |
| **T4 (G2)** A screen-space ellipse mask on the `post/glitch` and `post/split` passes | Yes, a screen region round the head | Yes | Small (4 parameters + a mask term in the shader) | First engine change if the owner wants the beams and haze untouched |
| **T5 (G3)** A `bands` (quantise) option on the Sine deformer: per-band rigid x offsets | Strictly, geometric | Hard bands; chroma from T1 split or T2 | Small (CPU + WGSL deformer, parity test) | Best "the object itself tears" look |
| T6 A new distortion-framework producer (`distortion_frame.hpp`: one kinds file + one row; a banded field on an ellipsoid proxy round the owner with exclusion radius 0) | Yes, follows the entity, depth-aware | Yes (three chroma taps exist) | Medium (new `DistortionField` + shader branch) | Defer; more than the slice needs |

Digital Mosh (ADR-1160..1168) has no masked or local glitch. Its glitch is the same full-screen post vocabulary, plus
`temporal/mosh`, which is temporal and full-screen and is deferred by the spec. **Conclusion:** the renderer cannot
band-displace *only* the bust pixels today. The lightest honest alternative is T1 + T2 + T3: full-screen tear and
split, short and transient-gated, on a background dark enough that they read as local; strictly local material bands;
and a strictly local geometric shear. G2 or G3 add true locality later.

### 2.5 Procedural life

- **`float` effect** on the bust (XFORM): `height` (±0.8 cm), `period` (7-11 s), `tilt` (1-2 degrees), `yaw` drift,
  `sway`, `seed`. Incommensurate sines, never periodic, a pure function of time.
- **Irregular orientation:** two `noise` or `lfo` sources (`sources/<n>/rate` about 0.02-0.05 Hz) routed to
  `nodes/bust/transform/rotation` components (2-4 degrees of yaw, 1-2 of pitch).
- **Sound accents:** `response.onset` with depth `response.onsetLow` (ADR-1604's register) gives a 1-2 degree nod
  through a spring chain (`springHz` 3-4, `springDamping` 0.5), so the head settles rather than snaps.
- **Shifting eye-region highlight:** move the program's socket-mask centre by a slow LFO (a constant route). No vertex
  deformation of the face.
- **Camera:** `kit.Scene.drift_camera` (60-90 s period, small amplitude). Never routed to the music.

### 2.6 States: dormant, communicating, overload

**Recommendation: continuous signal blends for the behaviour, plus the state machine only for performer overrides.**

- **`signal.awake`** (an ADR-1602 conditioned signal): `response.level` through a slow attack and release (1.2 s / 4 s)
  and a soft threshold. Every emission and FX route takes `depthSource: signal.awake`. Dormancy is then the entity
  falling silent as the signal decays, never a cut.
- **`signal.overload`:** `response.intensity` (12 s follower) above about 0.7, times `response.transient`, through a
  peak-hold and a 2-3 s fall. It deepens separation, saturation and tear depth, and caps itself because the follower
  falls.
- **States** (ADR-031/1164, the Digital Mosh pattern: `states` with presets and `signal` triggers that `hold`):
  - pads 36, 37 and 38 force Dormant, Communicating and Overload;
  - an `elapsed` trigger returns Overload to Communicating after N seconds;
  - presets move only base values (beam base intensity, wash base, tear base).
  A slice without states is complete. States are task 9 of the plan.

## 3. Audio: the signal map, latency and control

**The spec's signal set mapped to the bus.** Every source works on a mix and on stems:

| Spec signal | Bus signal | On stems | On a mix |
|---|---|---|---|
| Low-frequency energy | `signal.bassLevel` (drum_signals.py) | `role.bass.level` | `response.bass` (30-150 Hz body) |
| Mid-frequency energy, "the voice" | **`signal.voice`** (new, scene-authored; a `signal` source in `firstActive`, built like drum_signals.py) | `role.vocal.level`, else `role.synth.level` | `audio.mid` (auto-gained mid band) |
| High-frequency energy | `audio.treble` (level) and `signal.hat` = firstActive[`role.drums.hat`, `response.hat`] (events) | drums-role classifier | the mix's hat detector (kept by ADR-1620, 0.85 precise on Feline) |
| Broadband | `response.level` (read against the music's own loudness, ADR-1510) | | |
| Transient intensity | `response.transient` (level), `response.onset` (event) + `response.onsetLow/Mid/High` register (ADR-1604) | | |
| Sustained energy | `response.sustain` | (or `signal.synthLevel`) | |
| Bass transients, "kick" | `signal.kick` / `signal.kickEnv` (ADR-1620: a mix has no kick; on a mix it is `beat.pulse` x 0.7 while `signal.drumming`) | `role.drums.kick` | beat stand-in |
| Snare hits | `signal.snare` = firstActive[`role.drums.snare`, `response.snare`] (new, scene-authored) | | |
| Macro dynamics | `response.intensity` (12 s) | | |

**Rules that keep it from being tuned to one track:**
- read `response.*` levels (they are relative to the track's own loudness);
- prefer `role.*` through `firstActive` signals, so a stem set improves precision without changing the look;
- keep every route's amount inside a clamp;
- never tune against the absolute loudness of one file.

Validate on at least three sources:
- Feline Footwear (the reference mix, `assets/audio/feline-footwear.wav`, with its stems);
- Trench (`assets/audio/trench.wav`);
- one quiet or ambient track or `make_test_material.py`'s `pads` class.

**Same-frame propagation (what adds latency):**

| Hop | Latency |
|---|---|
| Analysis frame to `audio.*`, `response.*`, `stem.*`, `role.*` | Same frame (`signals` rack, first pass) |
| `signal.*` (ADR-1602), `interpret` (`visual.*`), envelope sources | **Same frame** (second pass, list order: list a signal after any it reads) |
| Routes (with their chains) to parameter finals | Same frame |
| XFORM (Shake, Float) | Same frame (evaluated BeforeScene with this frame's finals) |
| Effect signal triggers | Same frame (ADR-1601) |
| A material program's `audio` input | Same frame, raw (rms, bass, mid, treble) |
| **`macro.*` (a route into a macro read back)** | **One frame late** (33 ms at 30 fps): **do not use** |
| Route attack (`attackMs`) | Adds its own rise: transients use 0-15 ms, levels 40-120 ms |
| Live impact to photon, measured (ADR-1605/1609/1610) | p50 about 48-60 ms (Event Horizon 47.5, Rainbow World 59.7). The scene adds nothing if it avoids macros |

**The control layer (ADR-1606/1612).**
- Declare `control.channels` with `rest` and `pickup: true`, and bind CCs to *signals* (`control.<ch>`), not parameters.
- Use each channel as a route **depth** (`depthSource: control.<ch>`, amount doubled so the rest value of 0.5 is the
  designed look) or through `polarity: "bipolar"`.
- Seeks replay the control layer (ADR-1168/1573).

The full binding table, parameters and MIDI map are in `03-implementation-plan.md` §3-4.

## 4. Cost: measured, throwaway probe

**Arms.** `probe/cost_arms.py`, eight cumulative arms. Each is a kit.Scene in scratch:
- the probe GLB at 0.6 m as a gltf node, unshadowed key and rim, a dark sky, faint fog;
- 1920x1080, `--headless --range 1.0: --frames 360 --bench-json`, default tier;
- 3 interleaved repetitions, medians.

| Arm | Contents | GPU p50 ms (reps) | median | GPU p95 | CPU frame p50 |
|---|---|---|---|---|---|
| A0 | Empty ether + bloom | 1.23 / 1.25 / 1.27 | 1.25 | 1.39 | 2.1 |
| A1 | + bust (29.8 k tri) | 5.26 / 4.43 / 3.35 | 4.43 | 6.0 | 6.8 |
| A2 | + 16 beams (12 mouth, 4 eye; light-owned, riding) | 3.56 / 4.17 / 3.81 | 3.81 | 9.3 | 5.2 |
| A3 | + 2 ghost copies (opaque in the probe) | 3.95 / 4.04 / 4.05 | 4.04 | 10.8 | 5.4 |
| A4 | + glitch tear + RGB split | 3.95 / 4.26 / 3.99 | 3.99 | 11.0 | 5.3 |
| A5 | + frame echo (4 frames) | 4.26 / 4.26 / 4.16 | 4.26 | 11.8 | 5.6 |
| A6 | A4 with 24 beams | 4.66 / 4.70 / 5.05 | **4.70** | 7.2 | 6.1 |
| B2 | A1 + 16 *world*-owned beams | 3.98 / 3.79 / 4.97 | 3.98 | 9.3 | 5.4 |

**Reading.**
- **The whole stack is 4-5 ms of GPU** (A6 is the worst case: bust, 24 beams, ghosts, tear, split, bloom) against
  29.3 ms. That is about **6x headroom**, before any live-ladder lever.
- Pass medians in the top arms:
  - `scene` 3.5-4.2 ms (bust, beams and sky together; at this load the GPU sits in low clocks, which inflates it);
  - `post/bloom` 0.14-0.33, `ao` 0.14-0.31, `temporal/echo` 0.16, `post/glitch` 0.05.
- **Marginal costs are mostly below run-to-run noise.** The one clear step is beams: +8 beams cost about +0.6 ms
  (A6 vs A4). These beams fill a large part of the frame (camera 2 m away), so screen coverage is the cost driver. The
  hero cone will cost the most per beam.
- **Light-owned vs world-owned beams:** no measurable difference (A2 vs B2), so the 16-24 dummy spot lights are not a
  cost problem.
- **p95 spreads (7-12 ms) are clock ramping** in an unpaced headless run at a tiny load. They are not frame spikes.
  The CPU frame is 5-6 ms.
- Draws: 9 (bust 2 parts, 2 ghosts x 2 parts, sky, post). Submitted triangles 89 k.

**Caveats.**
- The ghosts were opaque. Blended ghosts add at most two bust-sized layers of overdraw, which is small.
- The probe had no particles (20-60 motes cost nothing measurable) and no material programs. The bust program is about
  30-40 ops, which compiles to straight-line WGSL live (ADR-1493).
- This is not the live, paced, UI-on measurement. The implementation must run:
  - `tools/live_scene_profile.py examples/sonic-abstract/talking-heads.json --mode live --target-fps 30`;
  - the live baseline check (`tools/live_perf_baseline.py`, ADR-1608) after adding the scene to its list.

**Budget for the slice** (GPU, 1080p, live Ultra/High):

| Item | Budget |
|---|---|
| Bust with porcelain program | ≤ 1.5 ms |
| Beams: hero cone + ≤ 8 mouth rays + 4 eye rays | ≤ 2.0 ms |
| Ghosts | ≤ 0.5 ms |
| Tear and split | ≤ 0.2 ms |
| Echo | ≤ 0.2 ms |
| Bloom and the rest of post | ≤ 1.0 ms |
| Sky, haze, motes | ≤ 1.0 ms |
| **Total** | **≤ 7 ms p50, ≤ 10 ms p95** |

CPU: ≤ 8 ms per frame. No per-frame CPU mesh work: no SDF, no re-mesh, no routed node parameters that rebuild
geometry. The deformers run on the GPU.

## 5. Engine gaps: none blocking. Each is the smallest change, with its test (ADR range 1670-1689)

| # | Gap | Smallest change | Test | Needed for |
|---|---|---|---|---|
| G1 (ADR-1670) | An entity-owned Light Beam cannot follow the owner's rotation or point upward | A `space` row (`world` default, `local`) on `lightBeam`. In `local`, `resolveBeam` takes the offset in the node's frame (`NodeView.world`) and the axis from local yaw/pitch through the node's rotation, over the full sphere | `test_light_beam_local` (CPU): with a fake `EffectSceneQuery`, apex and axis track a rotated and translated node; `world` stays byte-identical | Optional. Drops the 16-24 dummy spot lights and makes the fan a self-contained effect, which the spec's "build this as a reusable effect" favours |
| G2 (ADR-1671) | `post/glitch` and `post/split` are full-screen only | `post/glitch/{maskX, maskY, maskRadius, maskFeather}`, an ellipse in uv (0 radius = off, byte-identical). One mask term in the glitch shader, scaled by aspect. The scene authors the centre from `kit.Scene.project` of the head (camera drift is small) or routes it | GPU: masked frame equals unmasked inside the core and equals no-glitch outside the feather (pixel diff). Defaults unchanged (hash) | Optional. True screen locality for T1 |
| G3 (ADR-1672) | The Sine deformer is smooth; no hard band offsets | `bands` (cycles per metre; 0 = off) on Sine: phase input `floor(dot(p - c, axis) * bands) / bands`, so each band is a rigid offset. CPU and WGSL in parity | CPU/GPU parity (the deformer suite's pattern); a banded mesh's vertices within one band share one offset; `bands: 0` is byte-identical | Optional. Geometric tearing local to the bust |

Not gaps (checked):
- the audio set (all signals exist; `signal.voice` / `signal.snare` are scene-authored `signal` sources);
- material ops (oklabRamp, quantize, fresnel, noise, mixBy);
- entity FX lanes on a procedural node (`entity_fx.cpp` maps procedural ranges);
- gltf node programs (ADR-1572);
- state triggers with hold and elapsed (ADR-1164);
- control channels with rest and pickup (ADR-1612);
- frame echo.

## 6. Risks found while probing

1. **A procedural mesh node as the bust parent of gltf ghost nodes** is untested. The probe parented gltf to gltf.
   Verify in task 2. Fallback: the bust as a gltf node too, which drops the deformers, so tearing becomes T1 + T2 only.
2. **Spot-light azimuth and elevation are clamped to ±89 degrees elevation** in the node frame. A ray straight up from
   the crown is not possible, which is fine for this layout.
3. **Light-owned beams need intensity > 0.** A route that drives `lights/<id>/intensity` to 0 kills the beam. Drive
   `fx/<id>/intensity` and leave the light at 0.01.
4. **Glitch tear on haze and particles.** Keep haze ≤ 10% of frame luminance and motes sparse, or take G2.
5. **The `+Z`-facing GLB vs engine features that assume `-Z` is forward** (`FxAnchor.forward`). Not used by the slice.
   G1 must take explicit local directions, not "forward".
6. **The hero cone close to the camera fills the frame and costs the most.** Watch coverage in the composition.
