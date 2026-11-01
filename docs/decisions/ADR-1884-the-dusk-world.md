# ADR-1884: The dusk world: the same canyon, a dusk light that does not crush the gorge

- Status: Proposed (Gate C), scene/strange-attractor.
- Code: `examples/strange-attractor/build_dusk.py` writes `strange-attractor-dusk.scene.json`.

## Decision

The dusk canyon is the flythrough scene's canyon, the same `attractorWorld` block (the generation is shared by hash:
identical terrain, no second bake), with the corruption, the scan and the portal taken out.

- **The sun** is 2.8 degrees up, ahead of the river run, so the water mirrors the bright western sky and only the
  high strata catch warm light.
- **The sky** is violet-indigo, not pink: a deep indigo zenith (0.022, 0.020, 0.115), a violet horizon
  (0.36, 0.22, 0.46), only a narrow warm band at the sun, cirrus lit coral and shaded indigo.
- **The gorge does not crush** (the plan's risk 8, the Nocturne lesson). A cool sky fill from above (0.7), a violet
  bounce across the gorge (0.45) and a weak floor bounce hold the shadowed walls up as violet stone; the environment
  term is doubled (0.16).

The camera arrives from the Moon's floor portal 150 m above the river, still diving, pulls out over the water and
joins the canyon's own river run (ADR-1883).

## Consequences

The walls stay readable in the dive and the river run (`portals-c/`); the look is judged on the render, as the plan
asked, not measured.

## Revision at Gate C 2

The coordinator found the dusk canyon moody but flat: muddy maroon walls, with warm light only on distant rims.

- **The sun is low and rakes the walls.** It is 8 degrees up, behind-left of the exit, so it rakes across the west
  wall and the far walls down-canyon. Lit sandstone now stands against violet shadow, and the shadows are long.
- **The shadows are cooler.** The sky fill is bluer and lower, and the bounce is violet, so shadowed stone reads as
  violet rather than mud. The gorge still does not crush.
- **The exit is in the east wall** (site B of four candidates; `portals-c2/00-exit-candidates/`). It is 43 m up a
  56-degree face, facing down-canyon over 200 m of open water, with the lit far walls ahead. The camera flies out low
  over the water, drifting right with the river and rising slowly, to 60 s.
- **No tear on the dusk side.** The wall carries a weak corruption field around the exit, but no tear and no code. The
  camera looks out through the wall and never sees a tear from the front. The tear's throat stretched the wall's foot
  into dark slivers in front of the camera, and code on the foot filled the first frames.

## Revision at Art Pass 2, Milestone 1

The dusk canyon is no longer a scene. `build_dusk.py` and `strange-attractor-dusk.scene.json` are removed: the Moon's
portal returns to the root canyon (ADR-1885) and the dusk light is the `dusk` look from crossing 1 (ADR-1886), written
by `build_journey.py`. The sun is now -130 degrees azimuth, 8 degrees up; the exit is unchanged.
