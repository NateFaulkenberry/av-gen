# Water "tearing lines": audit and proposal (read-only; nothing in either worktree was touched)

**Bottom line.** Your hypothesis is right, and I've confirmed it in code.
- **What the tears are:** ripple layers sheared by a flow offset that grows with t, wherever the baked per-vertex flow jumps between neighbouring vertices. The stepping comes from the water mesh's 1.2 m triangle grid.
- **No existing parameter** can place, bound or tune them.
- **Recommendation:**
  - (A) Bound the advection. This removes the accidental seams and the late-film streaking. It changes every frame, by design.
  - (B) Add deliberate tears built on the same mechanism, with a bounded shear, explicit placement and an explicit lattice for the angular steps. B is byte-identical at amount 0.

## 1. Mechanism confirmed
The magnitudes below come from a CPU re-implementation of the bake: `scratchpad/audit/shear_bands.py` and `shear_classes.py`.

- **Flow is baked per vertex.** The normal carries `(dir.x, speed/fastest, dir.z)` (`src/world/terrain.cpp:325-327`), sampled on the ground grid. In GV3 that grid is 48 m / 40 = 1.2 m. It is interpolated per triangle and normalised per pixel (`shaders/water.wgsl:226-233`).
- **The offset has no bound.** Every ripple layer samples at `p − dir·speed·t·k` (`water.wgsl:163, 169, 176`). Sparkle, foam and glow drift the same way (`398, 426, 320`).
- **The baked field has three discontinuities:**
  1. The direction is the tangent of the *nearest segment* of the Chaikin-smoothed centreline (`src/world/water.cpp:155-175, 193`). It is piecewise constant, so it jumps along a ray from each node. GV3 has 104 nodes about 7.3 m apart, with turns up to 5.2° (mean 2.4°).
  2. The flow takes one body per point (`water.cpp:201-212`). At the elder pool it flips from the river's 0.41 m/s along the channel to the pool's 0.066 m/s along (0.7, 0.7).
  3. A still body's speed steps to 0 at its rim (`water.cpp:190`).
- **Why the stripes are dense and follow the band.** Across a triangle that straddles a jump, the offset changes by |Δv|·t over about 1.2 m. That compresses the noise by γ ≈ |Δv|·t / 1.2 m and turns its iso-lines parallel to the band.
- **Why the paths are stepped.** The quad split is fixed (`{a,c,b,b,c,d}`, `terrain.cpp:354`). Each band piece therefore runs along a grid axis or the anti-diagonal, giving zig-zags one or two cells (1.2–2.4 m) wide.
- **Compression γ (median / max):**

| Source | 34 s | 171 s | 215 s |
|---|---|---|---|
| Pool switch | 5.6 / 16 | 28 / 78 | 35 / 97 |
| Node rays | 1.3 / 2.3 | 3.3 / 7.8 | 4.0 / 9.6 |

- **The whole river streaks, not just the bands.** The smooth bank-shear parabola raises the median compression from 1.18 to 2.15 between 34 s and 171 s.
- **Why it aliases late.** The fade uses the nominal frequency (`135-138, 239`). At 171 s, inside the pool band, layer 1's 19 cm wavelength becomes about 7 mm, and nothing fades it.
- **Why GV3's final hides them (inference).** The analytic gradient ignores the Jacobian, so compressed stripes keep an ordinary slope, and the calm 0.05 ripple makes them faint. The faint lines in v0/v1 at 171.25 s match the node rays.
- **Map:** `scratchpad/audit/shear_map_171s.png` (top-down: red is the pool switch, amber the node rays).

## 2. Current model
- **Normal:** three isotropic value-noise layers (`water.wgsl:151-180`).
  - Frequencies are s, 2.7s and 7.4s, with slope weights 1, 0.2 and 0.05.
  - They travel at 1×, 1.6× and 2.4× the local speed; `chop` pushes layers 2 and 3 across the flow.
  - Each layer fades below 1–3 px of an isotropic footprint.
  - Still water gets a speed floor of 0.08 m/s, multiplied by `rippleSpeed` (`233`).
- **Foam:** a band by vertical depth, broken up at 7s (`417-432`).
- **Sparkle:** thresholded glints at 26s, band-passed to 3–70 px (`140-149, 393-405`).
- **Glow:** thresholded, drifting patches, gated by depth and midstream position (`319-334`).
- **Reflection:** the IBL cube only; the mip is roughness × 2.2 (`347-355`). There is no screen-space reflection.
- **Moon glint:** GGX with shadows (`363-383`). Refraction offsets the depth read (`257-261`).
- **Routable** (`src/scene/composition.cpp:5299-5339`): glow, sparkle, ripple, flowSpeed, swell, foam, glowColor, clarity, maxOpacity, fresnel, reflection, roughness, refraction, rippleScale, shallowDepth, shallowColor, deepColor.
- **Not routable:** chop, specular, foamWidth, edgeFade, the glow shape, and the other colours.
- **Latent hazard:** routing `flowSpeed` shifts the pattern by Δspeed·t, and routing `rippleScale` rescales it about the world origin. Either makes the ripples jump. GV3 routes only `audio.bass → ripple` (+0.015).

**Q2, can existing parameters make streaks?** No. Every layer is isotropic, and `chop` changes where layers travel, not their shape. The only elongation is the accidental shear in §1.

## 3. Proposal

### A. Bounded advection (do this first, as its own ADR and re-baseline; no legacy switch, per ADR-441)
- **Change:** in the ripple, sparkle, foam and glow code, replace the unbounded offset with a two-phase offset.
  - Phases: φ₀ = fract(t/P + j(p)) and φ₁ = fract(φ₀ + ½).
  - Weights: 1 − |2φ − 1|, blended variance-preserving (÷ √(w₀² + w₁²)).
  - j(p) is low-frequency noise that hides the pulse. P is about 8 s.
- **Measured bound at any t:**
  - Pool: median 1.6, max 4.2.
  - Node rays: 1.07 / 1.28.
  - Smooth shear: 1.04 / 1.11.
- **Cost (inference):** 3–7 extra noise evaluations per pixel; measure it.
- **Should the accidental seams go? Yes.** They depend on film time, sit wherever implementation details put them (path nodes, body boundaries), and alias. B brings the look back where you want it.
- **Optional polish, deferred:** make `flowAt` continuous at the source (interpolated tangents, bodies blended by weight). Its blast radius is larger, because floaters and entities read it.

### B. Deliberate tears (new; `if (tears > 0)`, all in `water.wgsl`)
- **Direction frame:** `td` is the frame wind (`frame.windDir.xy`) or an authored angle, uniform across the frame.
- **Seam network (rigid, so nothing grows with t):** `q = (dot(p,td) − drift·t, dot(p,⊥td)) / tearCell`.
- **Stepped band:** S = Σ over the 3 corners of the lattice triangle holding q (same split as the mesh) of bary_k × step(0, N(corner_k)).
  - N is value noise stretched along `td`, with seams about `tearSpacing` apart.
  - Band mask: `band = 1 − |2S − 1|`.
- **Which seams show:** `live = smoothstep(1−cov, 1−cov+0.15, M(q))`.
- **Wind energy:** `e = mix(1, clamp(w.strength·(1+w.gust)/0.74, 0, 2), tearWind)`, where `w = windSampleAt(pos, t)`.
- **Shear:** `Δ = tearShear·live·e` (metres, bounded). Each layer samples at `p − o_k − dir·Δ·S`.
- **Amplitude:** `gradient × (ripple + tears·band·live·bandFade)`. The band needs its own slope, or GV3's calm ripple hides the stripes.
- **Layer fade:** `rippleLayerFade(f_k, footprint·r + Δ·|∂S/∂px|·r)`, with r = targetSize.y / 1080. The fade then counts reference pixels, so previews and 1080p/4K finals match (§7.10).
- **Band fade:** `bandFade = smoothstep(1, 3, 1/(|∂S/∂px|·r))`. A band narrower than a pixel fades out instead of turning jaggy.
- **Alternative placement:** "current" mode sets `side = step(threshold, speedFraction)` per vertex in `vs_water`. The mesh then steps the band exactly as the accident did, but along the fast/slow-water boundaries.
- **Later:** authored seam segments in a uniform array.

**Parameters** (WaterSettings, JSON, `nodes/<n>/water/…`):

| Parameter | Default | Routable? |
|---|---|---|
| `tears` | 0 = off | yes |
| `tearShear` | 3 m, cap 8 | yes, with slow chains |
| `tearCoverage` | 0.35 | yes |
| `tearCell` | 1.2 m (what the owner saw) | no |
| `tearSpacing` | 40 m | no |
| `tearStretch` | 3 | no |
| `tearDirection` | wind or angle | no |
| `tearDrift` | 0.15 m/s | no |
| `tearWind` | 0.6 | no |

The static ones either multiply t or rescale about the origin: at |p| ≈ 350 m, a 1% change moves a seam 3.5 m.

**Files:**
- `shaders/water.wgsl`
- `src/scene/water_surface.hpp/.cpp`
- `src/rendering/water_renderer.hpp/.cpp`: append 3 vec4s after `params` (192 → 240 B, within the 256 B stride). Update the static_assert and the NaN list (`water_renderer.cpp:57-61`).
- `src/scene/composition.cpp`: register (5299-5339), unregister (5389-5402), update (8629-8657), JSON (9768-9798, 11561-11576); plus `composition.hpp`.
- The GV3 generator, and an ADR.

### Tests
- **Identity at amount 0.** Build a second ShaderLibrary whose first search directory holds a frozen copy of `water.wgsl` from after A.
  - Includes still resolve live via first-match `locate()` (`src/gpu/shader_library.cpp:61-75, 95`).
  - Render the QA water scene (the `Shore` harness in `test_water_depth_forensics_gpu.cpp`) and a synthetic quad, at 3 seconds × 2 poses, with `tears = 0` and every other knob off its default. Require equal `gpu::hashImage`.
  - Controls: `tears = 0.5` must differ, and both arms must draw at least 500 water pixels.
  - Precedent: `test_distortion_gpu.cpp:370`.
- **The tears exist and align.** Top-down synthetic quad with post and AO off; D = |tears 0.6 − tears 0|.
  - 2–25% of water pixels change.
  - The changed pixels form thin components with a PCA aspect above 6.
  - Inside the bands, the structure tensor is coherent (above 0.6) along each band piece.
  - Control: rotating `tearAngle` by 60° must move the component axes with it.
- **A is bounded.** At t = 10 s and t = 200 s, high-frequency water energy stays within ±15%. Today's shader fails this, which is the control.
- **Seek exactness:** the pattern at forensics `:788`.
- **Resolution:** 1080 vs 2160 internal, within ±15%.
- **CPU checks:** JSON, validation, the unregister list, and the NaN poison list (`:1071-1100`, which is hand-kept).
- **GV3 renders:** stills of s08, s12, s14, s26 and s39; `shimmer.py` on s26 against 0.0029/frame.

## 4. Wind and audio
- **The wind field is already reachable from water.** `water.wgsl:24` includes `common.wgsl`, which includes `wind.wgsl` (`common.wgsl:272`).
  - `windSampleAt(p,t)` gives direction, regional strength and travelling gusts (`wind_field.wgsl:54-88`).
  - It runs on the same clock as the water: both use `time.renderTime` (`scene_renderer.cpp:2772, 3549`).
  - No new binding is needed.
- **GV3's wind is on:** 0.62 rad, speed 0.74, gust fronts every 38 m at 4 m/s. A front would tighten the seams in step with the grass.
- **What to route:** `scene/windSpeed` or `gustAmount` (`composition.cpp:4514-4553`). Never route `gustSpeed` (it multiplies t, `wind_field.wgsl:70`) or `windDirection`.
- **Audio:** use parameter routes, e.g. `audio.bass → water/tears`, add 0.15, attack 120 ms, decay 900 ms.
- **A second wind:** still water drifts along `WaterFlowSettings::windDirection` (`water.hpp:65`, 45°). That is about 10° off the scene wind.

## 5. Side findings
- `composition.cpp:5389-5402` never unregisters the ten ADR-350 water parameters.
- The GV3 scene sets `"shoreFade": 1.6`, which no parser reads (`composition.cpp:11561-11570`). It is silently ignored; `edgeFade` was probably meant.
- Inference: sparkle runs at 135 cycles/m, so its sin-hash (`water.wgsl:98-101`) sees arguments near 1e7 far from the origin. B should use an integer hash.
