# Pass 3 final revisions, 67- §4: the Mars sand piles fit the ground (the hero agent)

The owner's note: "mars - try to find a more natural way to make the piles of sand around the pyramids fit with the
textures of the mars surface - right now it looks like two entirely different textures / surfaces" (M3g §3, 54-).
Media: `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/04-mars/final/`.

## Why they were two surfaces
1. **Lighting, not texture.** The drifts and the plain already shared the sand photoscan and tint (M3e/M3g). But
   marsRock's occlusion output is register r0, and its M3d ops write the sand texture times its tint there, so the
   plain's ambient has been scaled by that (about 0.3-0.5) since M3d. The drift program had no occlusion, so its
   ambient was full: hence the pale, flat slab.
2. **Relief.** The plain and dunes carried the canyon program's strata relief (ledges and laminations along the band
   gradient: the "scaly" dunes); the drifts carried the sand's ripple normal map.
3. **Geometry.** The drift is a height field in the pyramid's frame, and the pyramid is sunk 3 m into a pad that is
   not exactly level. Its surface crossed the ground at a slope, so it stood on the plain with a crisp line round it.
4. **Slope switch.** The plain switched to the rock palette above normal.y 0.75 (about 41 degrees), so dune flanks and
   banks went pale rock-pink beside sand.

## What changed
- `build_mars.py` (marsRock, marsDrift, the drift nodes):
  - The drift takes the same occlusion as the plain (r0). This keeps the plain's approved brightness.
  - The plain takes the sand's ripple normals and roughness 0.95 wherever it is sand, exactly as the drift does. The
    rock keeps its own relief.
  - Sand runs up to normal.y 0.2-0.45 (about 63-78 degrees). The line is broken by a 9 m noise, so only cliffs and
    mesa walls take the rock palette.
  - The wall glow's 4 ops are dropped from marsRock to make room. `glow` is 0 on Mars and nothing routes it.
  - The result is 95 of 96 ops and 2 texture ops.
- `hero/build_mars_sand.py`: new drifts, `assets/strange-attractor/hero/mars/pyramid-{a,b,c}-sand-p3.glb`.
  - They use the same plan and profile as build_pyramids.py's `sand()`, laid on the real ground sampled under every
    vertex.
  - At the base each drift is exactly the old one, so the burial line and the sand in the courses are unchanged.
  - Out from the base, the drift's old thickness over the pad goes through a soft maximum with 0. The thin edge
    therefore flattens onto the ground and meets it nearly tangent, about where the old edge was.
  - It then sinks 0.25-0.75 m under the ground along an 18 m noise. That gives a noise-broken toe with no lip.
- `pass3/cameras.json` gains supplementary cameras:
  - `h2-mars-drift-west` and `h2-mars-drift-b`;
  - path beats `j-mars-070.50` (the owner's M3g screenshot time), `j-mars-074.00`, `j-mars-082.00` and
    `j-mars-088.00`.

## Checks
- **Luma guard (Mars leg clip):** 0 flagged half-seconds.
  - The dusk→Mars and Mars→night crossings are unchanged within 0.016.
  - The leg's mean luma goes 0.587 → 0.610.
- **Regenerating the scene:** build_mars.py at 1f88520c reproduces the committed scene exactly. The new scene
  differs only in materialPrograms and the three drift nodes.
- **Not changed:** cameras, timing, portals, engine code. The flythrough scene and build_journey.py are not touched.
