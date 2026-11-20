# ADR-2102: Searchlight beams and a sky halo in the cloud march; a base colour per material part

- Status: Accepted (2026-11-19), avagent/neon-dreams (Neon Dreams, iteration 4)
- Extends: ADR-2100 (Cloud Environment v2), ADR-2101 (relief sea, `partMaterials`).
- Code: `src/scene/clouds.{hpp,cpp}` (`CloudBeam`, `CloudHalo`, `Clouds::beams|beamAir|beamCloud|halo`,
  `cloudBeamDirection`, the knobs), `src/rendering/cloud_renderer.cpp` (uniforms `beams`, `beamInfo`, `halo`,
  `haloColor`), `shaders/clouds.wgsl` (`beamFade`, `beamOnCloud`, `beamAir`, `haloLight`), `shaders/clouds_resolve.wgsl`
  (the uniform block; the composite no longer discards light with no cover), `src/scene/procedural.{hpp,cpp}` and
  `src/scene/composition.cpp` (`partMaterials.<name>.baseColor`).
- Tests: `tests/unit/test_clouds.cpp` and `tests/unit/test_procedural.cpp` (`[adr2102]`);
  `tests/rendering/test_clouds_v2_gpu.cpp` (`[adr2102]`: the beam lights the air and the sea, pointed away it changes
  nothing, it turns with the timeline second, the halo brightens the sky around its body).

## Context

Neon Dreams has three lighthouses whose beams the owner wants sweeping across the cloud sea. Iteration 3 built them as
the kit's unit cone with `alphaMode: blend` and a program opacity, and they drew opaque: the procedural renderer has
opaque, mask and refraction pipelines and no blended one, so blend is silently opaque there (ADR-2101 recorded it). A
blended pipeline would still be wrong for a beam: a sorted transparent cone is not hidden by the cloud in front of it
(the cloud composites after the opaque scene), does not light the cloud it enters, and shows its own polygon edges.

The moon was a hard-edged disc: a sphere node at 60 km whose edge met black sky with nothing between.

## Decision

1. **Beams are marched with the cloud.** `clouds.beams[]`: `origin`, `count` arms spread evenly in yaw (2 = opposed),
   `yaw` at t = 0 (0 = +x, 90 = -z), `elevation`, `rpm`, `length`, `halfAngle`, `radius` at the lamp, `color`,
   `intensity`; at most 8 arms in all. Each frame the renderer turns every arm by `rpm` at the timeline second
   (`cloudBeamDirection`, a pure function of the second: seek lands where play would).
   - **On cloud:** every lit march sample adds each arm's soft profile (`exp(-2 r^2 / w^2)`, w growing from `radius` by
     the half angle, faded along the length) times `beamCloud`, as a glow does, so a beam lights the crowns it crosses.
   - **In the air:** each arm's scatter along the view ray is integrated in closed form at the ray's closest approach to
     the arm's axis (the profile there, faded, over the sine of the angle between ray and beam, capped), times
     `beamAir`. It waits at its distance along the ray: the march adds it, times the transmittance so far, when it
     passes that distance, so cloud in front hides it and cloud behind does not; the opaque scene cuts it.
2. **A halo** (`clouds.halo`: `direction`, `color`, `radius` in degrees, `intensity`): an inner glow and one four times
   wider around a direction, added where the ray reaches the sky (beyond `maxDistance`, so behind the far moon sphere
   too), times the cloud's transmittance.
3. The composite used to discard every texel whose alpha was under 1/1024; beams and the halo in clear air are light
   with no cover (alpha 0), so it now discards only texels that are also black.
4. Knobs: `clouds/beam/<i>/intensity`, `clouds/beamAir`, `clouds/beamCloud`, `clouds/halo/intensity`, registered after
   the glows' (routes can flash a lighthouse on a kick).
5. **`partMaterials.<name>.baseColor`** `[r, g, b]` replaces that part's base colour factor, so one asset can be
   grouped into nodes by cloth colour (Neon Dreams' awnings) without a per-instance colour column.

## Consequences

- A lighthouse beam is a few dozen ALU per sample and per pixel per arm; Neon Dreams' six arms did not move the cloud
  pass's median measurably at 1080p (the march's cost is the lobes).
- The beam does not cast into the cloud's interior (no march toward the lamp): a beam lights a cloud's face and its
  inside equally, which reads as light entering a thin crown. A beam behind a building is cut by depth, not shadowed.
- Still true: a procedural mesh with `alphaMode: blend` draws opaque. Nothing in the repository needs blended procedural
  meshes after this; if something does, it needs its own pipeline and sort, not this path.
