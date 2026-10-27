# ADR-1853: The canyon alone, and its flythrough

- Status: Accepted, scene/strange-attractor (Gate 2b, 2026-10-27). Supersedes the formation parts of ADR-1852.
- Code: `src/scene/attractor_world.{hpp,cpp}` (`buildFlight`, `attractorCamera`, `AttractorCameraMode`),
  `shaders/attractor/world.wgsl`, `examples/strange-attractor/strange-attractor-flythrough.scene.json`.
- Tests: `[adr1853]` in `tests/unit/test_attractor_world.cpp`.

## Context

After Gate 2 the owner asked to "remove all that stuff and just go back to the canyon - fix that up and make it look
good and render a fly through of the canyon".

## Decision

**Removed outright (ADR-441: no shims, no dormant code).**
- The Rössler or Thomas ribbon, the butte tower and the nested arches.
- `scene/attractor_formations.{hpp,cpp}`, `shaders/attractor/formations.wgsl` and the tube volume and its bindings.
- The landmark uniforms; the parameters `recursiveDepth`, `formationScale`, `attractorInfluence`, `attractorSystem`
  and `landmarks`; the camera drift.
- Their tests.
- **Kept from ADR-1852:**
  - the side canyons (seen from inside the gorge, they are the canyon's own side branches);
  - the band cut;
  - the knife-ridge smoothing;
  - background regeneration (ADR-1851).

**The camera.** `attractorWorld/camera` (and the block's `"camera"`) has three modes: `scene`, `composed` (the
searched vista, still) and `flight`.

**The flythrough.**
- **Built with the world**, sampled at 30 Hz, so a frame's pose is a pure function of its second.
- **Five movements over `flightSeconds` (54 s by default):**
  - the composed vista, pushing forward;
  - a 13-second dive off the lip;
  - low along the gorge's centreline, 25-45 m above the floor;
  - a climb past the cliffs to above the plateau;
  - a slow, level reveal over the canyon country.
- **Clear of the rock.**
  - The path is Gaussian-smoothed, then lifted clear: everything within 22 m, plus 14 m.
  - It is lifted by a cone envelope (`y_i = max_j (c_j - k |i - j|)`, k = 28 m/s), so it stays above every
    clearance point and its climb and descent are bounded.
  - A light smoothing may settle at most 5 m into the margin.
- **The aim** looks 2.5 s ahead, with the pitch clamped to -28..+8 degrees.
- **The bank** follows the turn rate (at most 11.5 degrees) on the river only.
- **The world fills every frame.** The path stays inside 75% of the square, where the upper tier and the rim tiers
  close every horizon. This is tested, and the clip was checked second by second.

**Look fixes. Each cause was found by A/B on the same frame.**
- **The black dashes on lit cliffs.**
  - Cause: the shader's band cut lowers treads up to a band height below the baked surface, but the shadow ray was
    tested against the baked surface, so it started inside rock.
  - Fix: the shadow march tests the cut surface (with the detail) for its first 120 m. It starts 1.2 m plus 0.2% of
    the distance off the surface.
- **The wood-grain moiré.** Cause: the band ledges were cut along bilinear contours, which turn at every texel edge.
  The surface near the camera (under 4 km, blended out by 5.5 km) is now the C2 cubic B-spline of the field.
- **Hatching on mesas.** Cause: ledges thinner than a pixel. The band cut fades from 2.5 to 5 km, and its risers
  widen with distance.
- **The faint vertical streaks on the caprock.**
  - The cliff flutes, vertical by construction, are removed.
  - Gullies are cut in metres (90 m and up), not in texels, so 4096² no longer cuts finer, streakier grooves than
    2048². Their amplitude drops from 6.5 to 4.5 m.
  - What remains is soft shading of real 90 m+ gully geometry.
- **Near rock (the lumpy blob).**
  - A height displacement can only vary along a vertical face, which carved vertical grooves. Face relief is now a
    pattern in (along the face, the strata coordinate), so it is two-dimensional on the face:
    - beds of uneven thickness, recessed at their partings;
    - sparse broad joints;
    - a weathering skin.
  - Its height grows with the slope, so its sideways depth holds at about 1.5 m. It fades out by 1.1 km, beyond which
    it would only alias.
  - Flats carry soft-edged plates. Sharp plate edges self-shadowed into specks.
  - A four-tap AO near the camera darkens ledges and the feet of risers.
- **The river.**
  - A water plane at -0.8 m fills the gorge's channels (its floor is at -2 m), traced in the same pass.
  - It reflects the canyon (a second march, shaded without a shadow march) and the sky, with Schlick's Fresnel and
    fine ripples, over a dark body.
- **Nocturne.** Its haze and ambient are lifted (`0x55588e`, `0x5a6cc8`).
- **Exposure.** The sun is 4.6 (from 5); the bone caprock is `0xd9bf9a`.

## Consequences

- **Measured offline cost** (M2 Max, Release, 1080p, 4096² field):
  - the 54 s flythrough rendered 1,620 frames in 47.2 s, 34 fps end to end with H.264 encoding;
  - a low-river stretch rendered at 21.5 fps.
  - That is about 29-46 ms per frame, not a live measurement. The B-spline near surface (16 taps per march step) and
    the reflection march are the new costs. The live tier (Stage 6) will want a cheaper near surface.
- Generation at 4096² takes about 4.3 s, including the flight.
