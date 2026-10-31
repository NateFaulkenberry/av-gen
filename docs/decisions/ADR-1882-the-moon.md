# ADR-1882: The Moon: a generated lunar world with LRO-calibrated craters, boulders and a lunar-Lambert regolith

- Status: Proposed (Gate B), scene/strange-attractor.
- Code:
  - `src/scene/lunar_world.{hpp,cpp}`: `LunarGen`, `generateLunarWorld`, and the `"lunarWorld"` block.
  - `Composition::setLunarWorld` and `flattenLunar` (`src/scene/composition_portal.cpp`).
  - `examples/strange-attractor/build_moon.py`, which writes `strange-attractor-moon.scene.json`.
  - `make_earth.py`, which builds the Earth from NASA's Blue Marble.
  - `shaders/pbr_shade.wgsl`: no fallback ambient when `environment.fallbackAmbient` is false.
- Tests: `tests/unit/test_lunar_world.cpp` `[lunar-world][adr1882]`:
  - deterministic output;
  - the hero crater's depth below its rim falls between 0.12 and 0.3 D;
  - finite, in-range meshes;
  - a different seed gives a different world;
  - the block round-trips and is refused when malformed.

## Context

The brief asks for a physical Moon: vast terrain, large craters, black sky, a hard low sun, long shadows, rocks for
scale, and the Earth if it helps. The owner approved the LROC NAC DTM of Linné crater for calibration and the Blue
Marble for the Earth. Both are in `assets/space`, gitignored and never committed:
- `NAC_DTM_LINNECRATER`, NASA/GSFC/Arizona State University, 2 m/px;
- `world.topo.bathy.200412.3x5400x2700.jpg`, NASA Visible Earth, public domain.

## Decision

**Craters.** The size-frequency law, oldest first, overprinted by the heroes. The profile is measured on Linné
(D = 2420 m in the DTM):
- depth 0.209 D;
- rim 0.037 D;
- bowl `(1 - r²)^1.9`;
- ejecta falling as `r^-4.3..-5.2` (the generator uses `r^-(3 + 1.5·fresh)`).

Age makes a crater shallower and its rim lower and softer, and takes away its bright ejecta.

**Curvature.** The surface falls away as `y -= d²/2R` (R = 1737.4 km), so the horizon is close.

**Albedo.** It is carried in uv.x: about 0.12, brighter on fresh walls and ejecta.

**The world's extent.**
- A fine tile: 4 km at 2.5 m.
- A skirt: 32 km at 32 m, sunk 400 m under the tile, so no edge is ever in view.

**Boulders.**
- Faceted, displaced icospheres, 0.3–4.5 m on a power law.
- Gathered on fresh rims and ejecta, set into the ground, and merged per chunk.
- About 46,000 of them.

**The regolith** is a material program, with no new shading code:
- McEwen's lunar-Lambert: `1 + L(2/(μ0+μ) − 1)` over Lambert, with L from the phase (Lambert-like at high phase,
  Lommel-Seeliger at low);
- a narrow opposition surge;
- a faintly warm grey.

**Light.**
- A low sun (9°) from the right of the arrival.
- The regolith's bounce as a weak warm fill.
- A black sky that lights nothing.

**The blue cast in the shadows, explained and fixed.** With no environment map and no sky, `pbr_shade.wgsl` lit
surfaces with a constant blue hemisphere (0.10, 0.12, 0.20) that ignored `environment.intensity`.
- A new environment key, `fallbackAmbient` (default true), lets a scene turn that hemisphere off. It is carried in
  the spare z of the bands' frame lane.
- The Moon sets it to false.
- I first tied this to `environment.intensity` 0. Two GPU tests ("water never reaches dry land", "transparent layers
  composite back to front") relied on the hemisphere, so it is now opt-in.

**The Earth.** NASA's Blue Marble on a sphere, 1.9° across, 5° above the horizon to the left of the arrival. It
stands 6 km away at true angular size (radius 100 m), not at its real distance: a body much past about 11 km is cut
back to a small cap nearest the camera, a far-depth limit in the renderer that is not investigated here. The cost is
a few degrees of parallax over the lunar flight.

## Consequences

- No existing picture changes: the fallback is opt-in.

## Revisions at Gate C (the Moon polish)

- **Boulders** were faceted white tetrahedra. They are now subdivided icospheres (one to three levels by size),
  rounded and irregular: broad lobes, one or two split faces, finer lumps; smooth normals; squashed, turned and tilted;
  a sixth to a third of their height buried. Their albedo is the regolith's under them (not brighter). Sizes follow
  cumulative N(>r) ~ r^-1.6 from 0.22 m (many small, few large, up to 5 m); twice the candidates, gathered on fresh
  ejecta. Contact shadows are on and the near cascade is tighter (`shadowSplitLambda` 0.92).
- **Ground detail** the 2.5 m mesh cannot carry is shaded in the regolith program: a directional derivative of noise
  toward the sun over mu0 (grit at 33 cm, lumps at 2 m) and craterlets (Worley bowls on a 2.5 m 3-D lattice, so their
  sections come in every size, faded out by 90 m), clamped to 0.55..1.5 so it never blotches. Fresh craters' ejecta
  carry irregular rays (broad and fine angular streaks, fading as r^-1.8).
- **The sun** is 24 degrees up from the right-behind (azimuth 110): at 9 degrees every crater floor on the route was
  in black shadow, the dive's included.
- **The route**: a fresh 160 m crater close beside the low glide, the rim of an old 900 m basin as the ridge that
  reveals the hero, and the hero (400 m, its floor Gate C's portal) on the basin floor.
- **The Earth** is 3.2 degrees across (life: 1.9; enlarged for the composition) at 40 km, low to the upper left of
  the route. Gate B's "far cut" was not a renderer limit: a node's `scale` parameter is capped at 100, so a unit
  sphere could never be larger than 100 m. `make_earth.py` now writes a 100 m sphere.
