# Art Pass 2, M3g: realistic underwater lighting (the plan)

The brief is 51- §2c. The owner, on the octopus test (`hero/07-octopus/05-ocean-130s.png`), said it "looks very CG". In
that frame:
- the floor is flat grey and evenly lit, with hard blotches;
- nothing falls off with depth or distance, and there is no colour absorption;
- creatures don't sit on the floor;
- the far walls read as a flat cyan cut-out.

## What real water does (the physics, and how underwater DPs light it)

1. **Absorption by wavelength.** Pure water absorbs about 0.45/m at 650 nm, 0.06/m at 550 nm and 0.02/m at 450 nm
   (Pope and Fry, 1997). Red is gone within a few metres of path, then orange and yellow, and the image tends to
   blue-green. The path counts both ways: from the surface down to the subject, and from the subject to the camera.
   Coastal water (Jerlov's types) shifts the minimum toward green.
2. **Scattering: veiling haze and backscatter.** Suspended particles scatter strongly forward (Henyey-Greenstein g of
   about 0.8-0.9 in sea water; Mobley, *Light and Water*). Contrast falls exponentially with distance: far shapes don't
   go dark, they dissolve into the water's own colour. Lights near the camera light the water between it and the
   subject (backscatter), which is why DPs put lamps wide and behind.
3. **Downwelling light.** The light comes from above: a bright, soft field, Snell's window at about 97°, and dark below.
   Caustics are sharp only in shallow water. They spread and lose contrast with depth and are barely visible past 10-15
   m. God rays appear where the surface is broken and the water is turbid.
4. **Grounding.** The soft top light leaves a dark, diffuse contact shadow under anything on the floor: no hard cast
   shadow, but a darkened footprint and ambient occlusion.
5. **The grade.** Real footage has a lifted, blue-green black and highlights that never clip to white. Saturation falls
   with distance, and the near reds and oranges survive only within a few metres or under a lamp.

## What the engine has

- **Distance fog** (`applyFog`): one grey extinction toward `fogColor`, so it cannot lose red first. That is the gap:
  ADR-1908 (below).
- **Volume march**: a grey scalar transmittance, Henyey-Greenstein phase (`volumeAnisotropy`), local lights in the
  volume (`volumeLocalLights`), and a range (`volumeMaxDistance`). It is right for the near backscatter and the cones of
  the base's lamps, but not for colour loss.
- **AO and contact shadows** (and ADR-1906/1907 for shadows); AgX.
- **Water surface**: none here; the ocean has no surface sheet. The water's local-light gap (48- F7) doesn't apply.
- **Material programs** (the reef): caustics, sand and algae are ours to soften.

## The plan

1. **ADR-1908: spectral distance fog.** `environment.fogSpectral` sets per-channel weights on the surface fog's optical
   depth (0 0 0, the default, is today's grey fog, to the bit). For the ocean, (2.6, 1.0, 0.62): red goes about 2.6×
   faster than green and blue a little slower, toward a deeper blue-green (`WATER_FAR`). Every lit surface (reef,
   base, creatures, subs) gets it, because they all go through `applyFog`.
2. **Near haze and backscatter.** The march keeps only the near 45 m (`volumeMaxDistance`), so the far water belongs to
   the spectral fog. It is forward-scattering (g 0.75), with the local lights in it: the base's floods and the subs'
   lamps make soft cones in the murk. The bay lamp's own scattering stays off (the M3f white-out).
3. **Light from above, dark below.** The fill turns to come from overhead (it pointed up from below). Ambient is a
   little lower, so the undersides go dark.
4. **Grounding.** AO and contact shadows on, so creatures and rocks sit in a soft dark footprint.
5. **Caustics and the floor.** Caustics are soft-edged and dimmer (they were hard blotches), and the sand drifts blend
   over a wider band, so the floor stops reading as patches. Next pass: caustics fading with water depth.
6. **The grade.** AgX in place of ACES, so the highlights roll off and the lamps don't clip to white.
7. **Next (the full pass, after the owner confirms):**
   - caustics fading with depth;
   - god rays where the light comes down past the base's towers;
   - the stills at 128, 130, 135 and 140 s;
   - the ocean clip 121.5-147.5 s;
   - the octopus close-up (in an untracked scene copy).

## Sources

- Pope, R. M. and Fry, E. S. (1997). "Absorption spectrum (380-700 nm) of pure water". *Applied Optics* 36.
- Jerlov, N. G. (1976). *Marine Optics*. Elsevier.
- Mobley, C. D. (1994). *Light and Water: Radiative Transfer in Natural Waters*. Academic Press.
- Underwater cinematography practice: lamps wide and behind the lens against backscatter; white balance lost below
  about 5 m; contrast by distance, not by darkness.

## The wider ocean brief (54- §5): the plan for the build after the owner's OK

- **The base, reworked (owner: modify ours, not a found model).**
  - Sunk into the shelf ledge: only the front face and the launch bay stand proud of the rock. The tiers above become
    rock with a low roofline, not towers.
  - The chimney towers and beacons are removed. A few low, recessed openings remain, and the launch bay with its glow.
  - Weathered materials: concrete and metal panels with seams, rust and biofouling (encrusting growth on the lower
    faces), and sediment on the upper surfaces and ledges.
  - Its illumination is kept: the bay glow, the window bands and the floods.
- **The shelf top.**
  - Its long flat line is broken: undulating terraces, gullies and small canyons cutting the edge, outcrops, and an
    irregular drop-off.
  - Generated by the same pipeline, with more terraces and branching on the shelf, and a few rock masses at the edge.
- **The seafloor geometry.**
  - Rounded, sediment-draped rock instead of spikes: less ridge sharpness, more erosion, and a smoothing pass that
    drapes sediment over the low ground.
  - A few large rock forms where the camera passes.
- **The textures.**
  - Photoscanned rock and sediment (CC0), anti-tiled (ADR-1905).
  - The dark blotches are the rock read against the pale sand by a broad noise mask. The plan is a sediment-dominated
    floor, with rock showing only on steep faces and outcrops, and an encrusted, darker reef texture there.
- **Creatures:** wait for the coordinator's octopus choice. The lighting above grounds them.
