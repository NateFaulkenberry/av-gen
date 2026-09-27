# ADR-916: Deliberate water tears

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the owner's revision brief for Glowmere Valley 3, section 1 ("visible surface tearing",
with a reference image), and the GV3 revision's water audit
**Follows:** ADR-914 (the accident's mechanism, bounded), ADR-915 (reference pixels), ADR-099 (the
water's modulation surface), ADR-387 (the panels group parameters by path), ADR-902 (the liveness
registry)
**Implemented by:** the `---- tears (ADR-916) begin/end ----` blocks in `shaders/water.wgsl` (`tearAt`);
the tear fields of `WaterSettings` (`src/scene/water_surface.*`); `WaterUniforms::tears`, `tearShape` and
`tearFrame`, `waterSourceWithoutTears` and the two pipelines in `WaterRenderer`
(`src/rendering/water_renderer.*`); the per-item pipeline bind in `SceneRenderer::render`;
`CompositionNode::WaterTearParameters` and their registration, unregistration and per-frame copy
(`src/scene/composition.*`); the `tear-setting-unread` rule (`src/params/liveness.cpp`) and its facts
and six phase-rate rows (`src/scene/route_liveness.*`)
**Tests:** `tests/rendering/test_water_tears_gpu.cpp` (8 cases, each with its control) and
`tests/unit/test_water_tears.cpp` (5 cases); `test_water_depth_forensics_gpu.cpp`'s uniform poison list

## Context

The owner's brief asks for visible "tearing" lines across the water, with a screenshot as the
reference: thin, angular seams in which the ripples are packed into dense parallel stripes. They should
read clearly in selected shots, belong to the Glowmere look rather than read as noisy normal-map motion,
vary subtly over time, respond to distance, survive the final render, relate to the wind or the music,
and not make the whole surface busy.

The screenshot is the accident ADR-914 removed. The ripples were sampled at `p - v * t`, and wherever
the baked flow jumped between two vertices of the water mesh the offset changed by `|dv| * t` across one
triangle, which packed the noise into stripes parallel to the jump; the mesh's fixed quad split stepped
the band into zig-zags one or two 1.2 m cells wide. It grew with film time, sat wherever path nodes and
body boundaries put it, and aliased late in a film. No setting could place, bound or tune it: every
ripple layer is isotropic, and `chop` moves layers, not their shape (the audit's Q2).

## Decision

**Tears are the accident's mechanism, with every quantity that made it an accident replaced by one
somebody chose.** All of it is in `tearAt`, per pixel, a pure function of the position and the
timeline second:

- **Where.** A rigid lattice of `tearCell` metres aligned with the seam direction -- the scene wind's
  steady direction, or an authored angle -- drifting along it at `tearDrift`. Each lattice corner lies
  on one side or the other of the zero line of a smooth field whose seams are `tearSpacing` apart and
  `tearStretch` times longer than wide. `S` interpolates the corners' sides over the lattice triangles
  with the water mesh's own split, so where the side changes `S` ramps 0 to 1 across one triangle: a
  stepped band one cell wide, along the lattice's axes and its anti-diagonal.
- **How much.** Across a band the ripple layers' sample point moves out and back again -- a tent in
  `S`, peaking at half the shear plus half the cell -- so each half of the band packs half the shear
  into half the cell. The compression is `tearShear / tearCell` and the band holds about
  `tearShear * rippleScale` stripes, parallel to it. Outside every band the shift is exactly zero, so
  nothing past a seam moves, and nothing grows with time.
- **Which seams show.** `tearCoverage`, a threshold on a second slow field, so a lower coverage removes
  seams rather than fading all of them.
- **The wind.** With `tearWind` above 0, the local wind's strength and gust envelope (`windSampleAt`,
  on the water's clock) scale the shear: a gust front crossing the water tightens the seams it crosses,
  in step with the grass on the bank. A scene without wind keeps its seams as authored.
- **The slope.** The band adds `tears` of ripple amplitude to the surface's. The analytic gradient does
  not carry the shift's Jacobian, so compressed stripes would otherwise keep the calm surface's slope
  and not read. The shear comes in over the first quarter of `tears`, so a route lifting it off 0 grows
  the seams instead of snapping the ripples beside them sideways in one frame.
- **Distance.** The band's width and the compressed layers are counted in reference pixels (ADR-915): a
  compressed layer fades where an uncompressed one at its frequency would, and a band narrower than
  about two reference pixels fades out whole. So a seam near the lens is packed stripes, one further out
  is a smooth slick line through the rippled water, and a preview and a final agree.

**At `tears` 0 the surface is byte-identical to one without the code, by a shader permutation.**
`WaterRenderer` builds two pipelines from `water.wgsl`: one as written, and one from
`waterSourceWithoutTears`, which removes every marked block. A material whose `tears` is 0 is drawn by
the second, so it runs exactly the shader ADR-914 and ADR-915 left, and `SceneRenderer` binds the
pipeline per water item (once per frame when a scene's waters agree).

**Every setting is a control, under one heading, labelled for what it does (UI reach).** Ten
parameters under `nodes/<terrain>/water/tears/`: the Parameters panel heads them `<terrain>/water/tears`
in the **nodes** group, and the World panel's Inspector shows them as `tears/<label>` rows under
**water** with the terrain selected.

| JSON key | parameter | label | route? |
|---|---|---|---|
| `tears` | `amount` | amount | yes |
| `tearShear` | `shear` | shear (m) | yes, slow chains |
| `tearCoverage` | `coverage` | coverage | yes, slow chains |
| `tearCell` | `cell` | step size (m) | no |
| `tearSpacing` | `spacing` | spacing (m) | no |
| `tearStretch` | `stretch` | stretch | no |
| `tearDirection` | `followWind`, `direction` | follow the wind; direction (radians, if not following the wind) | no |
| `tearDrift` | `drift` | drift (m per second) | no |
| `tearWind` | `wind` | wind (gusts tighten the seams) | no |

The seven that refuse routes are `modulatable = false`. The step size, spacing and stretch rescale the
lattice about the world origin and the direction turns it there -- 350 m out, a 1% change moves a seam
3.5 m -- and the drift multiplies the timeline second, so a route would make every seam jump. The wind
coupling is a choice, not a signal: to make the music move the seams through the wind, route the wind
(`scene/windSpeed`, `scene/wind/gustAmount`), which moves the grass with them. `tearDirection` is
`"wind"` or an angle in radians about +Y (0 = +X), held to one turn either way, the control's range.

**The liveness registry knows them (ADR-902).** A new rule, `tear-setting-unread` (dead): a tear
setting of a water whose amount is 0 and which no route or track lifts -- that water is drawn with the
tear code compiled out -- or the fixed direction of seams that follow the wind. `LivenessInputs` now
carries the modulator and the timeline for it; without them the rule gives no verdict rather than guess.
The phase-rate table gains `tears/drift`; `tears/cell`, `spacing`, `stretch` and `direction` while the
lattice drifts; and `scene/windDirection` while a water's drifting seams follow the wind. A route to
any of the seven is refused at bind anyway; these rows are what a timeline track that keys one meets.

Rejected:

- **A shift along the seam.** That is a shear, not a compression: it tilts the ripples into a twisted
  rope at `atan(cell / shear)` to the band instead of packing them.
- **A step in `S`, relaxed back to zero over the surrounding metres.** Value noise sits near zero, so
  the relaxation reached most of the surface and restretched the ripples everywhere: the "whole
  surface busy" the brief rules out. The tent reaches nowhere past the band.
- **One shader with the tear code gated on `tears > 0`.** Not byte-identical: with the tear written in
  `fs_water` and read in `rippleGradient`, the compiler built the shared ripple path differently and
  about 130 glint channels of the QA scene moved, by up to 0.125.
- **Placing seams along the fast/slow-water boundaries** (the audit's "current" mode, a per-vertex side
  from the flow speed). Not built: it would tie where seams are to how the river was baked, which is
  what made the accident an accident. The lattice gives one network a director places with coverage
  and a key on the amount.
- **Making the static settings routable with slow chains.** However slow the chain, a change of 1%
  moves the far seams metres.

## Consequences

- **No existing scene changes.** No scene or project sets a tear key, `tears` defaults to 0, and a
  material at 0 is drawn by the tearless pipeline: its pixels are the bytes ADR-915 left (the identity
  test, 6 of 6 frames, with every other tear setting pushed off its default).
- **One more pipeline compile** at start-up and on a shader reload. `WaterUniforms` grows from 192 to
  240 bytes, inside the 256-byte stride.
- **The reactive catalogue and the default proposal follow the controls (ADR-925, ADR-927).** They were
  written against this work's first spellings (`water/tears`, `water/tearShear`, `water/tearCoverage`),
  which never shipped. The catalogue now offers `water/tears/{amount, shear, coverage}` -- labelled
  water tears, tear shear, tear coverage -- and excludes all three while the amount is 0. The seven
  static controls are never offered: they refuse routes. The proposal lifts the amount with the section
  (key `water-tears`) and leaves the shear and coverage alone. `test_reactive_catalog.cpp` and
  `test_reactivity_proposer.cpp` test the real controls, with the tears-off control.
- **The route audit changes shape, not verdicts.** 26 rules with ADR-926's two; the tear rows are new,
  and no tracked project routes or keys a tear setting, so no project's findings change. `nodes/*/water/flowSpeed`'s
  evidence now says ADR-914's bound: a change jumps each field by at most 8 s of travel, not by the
  whole elapsed time. It is still a phase-rate hazard.
- **Measured** (test_water_tears_gpu.cpp):
  - A top-down synthetic sheet: the seams cover 18.6% of the water in long pieces (length over mean
    width 32). The image's dominant gradient inside them lines up with the band, 0.60 against 0.15 for
    the same pixels untorn. Turned 60 degrees, their mean axis turns 58.9.
  - Outside the seams, 0.46% of the water differs from the untorn render by more than 0.1% of its
    mean; with the tent replaced by a one-sided step, 23.1%.
  - At 2160 against 1080 rows, the far half's band pixels are x0.86 and its fine structure (relative to
    the near half) x0.91. Counting the frame's own pixels they are x1.95 and x0.55.
  - The audit also proposed "structure-tensor coherence above 0.6" inside the bands. It does not
    hold, and is not asserted: the compressed ripples are thin streaks with ends and junctions, 0.58
    in a 5x5 window against 0.67 for the ripples they came from, while strongly aligned with the band.
- **Cost.** `tearAt` adds five value-noise evaluations and one wind sample per pixel, and only to the
  materials the tear pipeline draws. Sixty GV3 frames at s08, 960x540 at 2x, took 4.8 s with the
  recommended tears and 4.7 s without, GPU-bound in both (2.7 s of each spent waiting on the GPU):
  inside the noise. A per-pass A/B was not run.
- **GV3** (stills in the revision's `water-tears/` folder, from scratch copies of its project; its files
  are untouched). On top of ADR-915's advice (ripple 0.1 at rippleScale 2.6, the bass route at 0.03), the
  valley's water block wants `"tears": 0.35, "tearShear": 6.4, "tearCoverage": 0.8, "tearCell": 3.2,
  "tearSpacing": 20, "tearStretch": 4, "tearDirection": "wind", "tearDrift": 0.15, "tearWind": 0.6`.
  - GV3's framings are far from the water, and at the reference's 1.2 m step the compressed ripples
    fall below the fade limit, leaving only slick lines. A 3.2 m step at a compression of 2 keeps them.
  - The clearest read is s08: a seam crossing the moon's glint by the boat, bright packed streaks near
    and a dark slick further out, changing 3.98% of the frame (3.93% in the 1080p final). s12 shows a
    combed band and a light slick line. The wides s14 and s39 show faint slick lines. s26 shows almost
    nothing with this lattice: no seam passes near its camera at 171 s.
  - Over a second of s08 the frame-to-frame change inside the seams is 0.0103, against 0.0122 for the
    ripples they replace. The rest of the water is identical.
  - An optional bass accent: `audio.bass -> nodes/valley/water/tears/amount`, add 0.15, attack 120 ms,
    decay 900 ms. At its peak (0.5) it changes 1.3% of s08, inside the seams only.
  - The same settings serve the 960x540 previews and the 1080p and 4K finals.
