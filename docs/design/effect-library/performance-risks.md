# Performance and Risks

This page is part of the [Effect Library](README.md). **It asserts no milliseconds.** The engine has
per-pass GPU timestamps (`gpu::FrameTimeline`, which produces marks such as `volume.march`, `scene`
and `shaderlayer`) and CPU stage timers. The renderer-upgrade and forensics memos record what a
number costs to get right: warm measurements, matched luminance, and A/B arms (ADR-117 pass arms).
Every cost claim below is *structural*: what the cost scales with. Each new primitive must add a
FrameTimeline mark and a pass arm, so its real cost can be measured by the rules already in place.

**Classes**

| Class | What it means |
|---|---|
| Very Low | CPU arithmetic or a uniform branch, with no new pass |
| Low | bounded extra draws or ALU in an existing pass |
| Medium | an extra full-resolution pass, or overdraw proportional to screen coverage |
| High | per-pixel loops (taps or steps) or a bandwidth-heavy history |
| Very High | an extra scene render |

**Primary cost codes**

| Code | Cost |
|---|---|
| C | CPU |
| V | vertex |
| F | fragment |
| X | compute |
| B | bandwidth |
| M | memory |
| P | extra pass |

## Per effect

| Effect | Class | Primary cost | Scales with | Main risk and mitigation |
|---|---|---|---|---|
| Space Warp | Medium | F, B, P | proxy screen coverage; the shared DF copy | copy bandwidth on Metal → gate per frame, scissor the resolve to proxy rects |
| Gravitational Lens | Medium | F, B | coverage; env fetch off-screen | none beyond DF |
| Heat Shimmer | Low–Med | F | coverage (Volume); full screen (Haze) | Haze is a full-screen pass → H1 hook, half-res option |
| Shockwave | Medium | F, B (+C light) | concurrent fronts × coverage | cap `maxConcurrent` at 4 |
| Ripple | Low / Med | F | Surface = existing wave loop; Membrane = DF | the 8-wave cap is shared with Ground Pulse |
| Bubble | Medium | F | shell coverage (2 faces) | large bubbles close to the camera → LOD the thin-film taps |
| Time-Warp Distortion | High | B | history taps × coverage | ring allocated at N frames → quality-tier gate |
| Portal | Medium (Remote: Very High) | F (P) | disc coverage; Remote = a second scene render | Remote View is a later wave, reduced resolution, one portal per frame |
| Reality Tear | Medium | F, C | quad coverage; polyline ≤ 32 | – |
| Radial Distortion | Low (Med with zoom blur) | F | taps | cap taps at 16 |
| Glow | Very Low | F (+ a light) | – | spill consumes the light budget |
| Pulse | Very Low | C / F | – | – |
| Flicker | Very Low | C | – | – |
| Bloom Source | Very Low | – | – | no effect when `emissionWeight = 0`; the panel says so |
| Light Beam | Low | F | cone overdraw | – |
| Volumetric Beam | Med–High | F | march steps × volumetric lights | existing march cost; shadowing adds fetches |
| God Rays | High (Screen: Medium) | F, B | steps × shadowed lights | Screen fallback on low tiers |
| Aura | Medium | V, F | owner triangles (redraw) + overdraw | Screen mode for dense or skinned owners |
| Halo | Very Low | F | – | – |
| Light Trail / Trail | Low | C, F | points × overdraw | 64k-vertex arena, LOD |
| Afterimage | Medium | V (Geometry) / B (Image) | copies × owner vertices; taps | cap copies at 8 |
| Motion Smear | Low | V | owner vertices, in every pass that draws it | – |
| Velocity Distortion | Medium | F, B | ribbon coverage + DF copy | – |
| Wind Response / Sway | Very Low | V | existing | – |
| Orbit, Spiral, Float, Shake, Bounce | Very Low | C | – | – |
| Lightning | Low–Med | C (once per strike), F (+ a shadow pass) | glow overdraw; a shadowed flash = cube shadow (6 views) | shadowed flash only under a quality flag |
| Arc | Low | C, F | strands × rate | – |
| Electric Field | Low | F, C | arc count | Worley F2 cost in the fragment |
| Plasma | Medium | F | coverage × steps | 12–24 steps; half-res option |
| Energy Shield | Medium | F | large two-sided shell | – |
| Force Field | Low–Med | F | – | – |
| Charge-Up | Low–Med | X, F | particle capacity | – |
| Discharge | Low–Med | C, F, X | bolts + sparks | – |
| Bioluminescence | Low | F (+ ecology lights) | pattern ALU | shares the ecology light budget |
| Pulsing Veins | Low | F | – | – |
| Growth | Low | F (discard), V | owner | discard loses early-Z for that draw in the prepass |
| Breathing, Organic Pulsation | Very Low | V | – | – |
| Tendrils | Medium | V | strands × segments × passes (incl. shadow) | instancing; LOD by screen size |
| Particle presets | Low (Rain/Snow near layers: Medium) | X, F | capacity; fill-rate | ADR-382: the quality tier scales spawn, never capacity |
| Stars | Very Low | F | – | – |
| Fresnel, Rim Light, Color Cycling | Very Low | F | – | – |
| Dissolve | Low | F | – | early-Z loss, as Growth |
| Hologram | Low–Med | F | internal-face overdraw | – |
| Scanlines, Pixelation, Dithering, CA | Very Low | F | full screen, trivial ALU | – |
| Toon Edges | Low (Hull: Medium) | F (3 targets × 9 taps) / V | – | – |
| Echo / Ghosting | Medium–High | B | taps | ring capacity; stride |
| Temporal Smear | High | B | up to 32 taps per pixel | tier gate, half-res history (the default `temporalHistoryScale` is already 0.5) |
| Freeze-Frame | Very Low (Pose) / Low (Image) | C / B | – | Image warm-up renders one frame after a seek |
| Time Dilation | Low | C | clocked owners; re-evaluating their tracks | – |
| Delayed Motion, Reverse | Very Low | C | HIST lookups | HIST depth = memory in checkpoints |

## Cross-cutting risks

1. **Bandwidth of the scene-colour copy (DF).** A full-resolution RGBA16F copy plus a resolve is the
   largest fixed cost this library adds. On Apple GPUs, WebGPU exposes neither tile memory nor
   programmable blending [R23], so the copy is a real round trip through memory.
   - Mitigations, all structural:
     - one copy per frame shared by all producers;
     - skip entirely when there are no producers;
     - scissor to the producers' union.
   - The option to measure first is a half-resolution offset target, because offsets are smooth.
2. **Overdraw from large shells** (Shield, Bubble, Beam, Plasma) close to the camera. SHELL has one
   pipeline per kind (ADR-118), and each kind should cull its back faces where the look allows.
3. **Temporal taps.** Every `temporalFilter` tap is a texture-array fetch at history resolution.
   Kernels declare their tap count, and the quality tier caps it.
4. **Volume march shadows** add a fetch per step per shadowed light. That is multiplicative with the
   existing march cost, which already dominates volumetric scenes (see the renderer-upgrade memo).
5. **REDRAW with skinned owners** re-runs skinning in the vertex stage per copy. Afterimage × 8 of a
   skinned hero is 8 skinned draws. Image mode exists for this reason.
6. **Checkpoint memory (HIST, clocks).** Checkpoints are capped at 256 MB, and the interval doubles
   when thinned (`entity.hpp:767-779`). A HIST ring is about 9.6 KB per subscribed owner per 4 s, so
   100 subscribed owners × 60 checkpoints is 58 MB. Subscriptions are per owner with a history
   consumer, never global.
7. **The light budget** is zero-sum with ecology lights (Glowmere). The 16-light reservation must be
   reviewed visually.
8. **Early-Z loss** from FXL clip. Discard is enabled only on draws whose `fxFlags` include Clip. It
   is a pipeline variant, not a per-fragment branch, because early-Z is a pipeline property.
9. **Particles after a seek.** This is a *quality* risk, not a performance one. Ambient presets
   (Snow, Rain) warm up, which is capped at 240 frames (ADR-395). The warm-up cost is paid once per
   seek.
10. **Determinism regressions** are the highest non-performance risk. Every type declares
    `seekExactness`, and every `Exact` type joins a parameterised play-vs-scrub test (see roadmap
    1.3/1.10). The failure mode ADR-700 fixed at 150 s must not return through an effect.
