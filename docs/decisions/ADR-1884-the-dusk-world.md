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
