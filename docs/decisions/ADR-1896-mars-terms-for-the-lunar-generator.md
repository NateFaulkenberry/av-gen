# ADR-1896: Mars terms for the lunar generator: mesas, yardangs and pads

- Status: Proposed (Art Pass 2, M2), scene/strange-attractor.
- Code: `LunarMesa`, `LunarPad`, `LunarGen::mesas`, `pads`, `yardangs` and `wind`, and `marsRelief`
  (`src/scene/lunar_world.*`).
- Tests: `[lunar-world][pass2][adr1896]`.

## Context

The brief asks for Cydonia: dusty plains, mesas and wind-shaped features, with three pyramids standing on it. The
lunar generator already makes a curved, cratered, rock-strewn ground with an albedo channel and a skirt to the horizon.
It lacked the Martian landforms.

## Decision

Three terms, each absent on the Moon (the Moon's hash is unchanged):
- **Mesas.** A flat-topped stamp with a rounded-rectangle outline (rx, rz, angle) eroded by a noise. A steep wall cut
  into terraces drops from the plateau, with a talus apron at the foot (sand brightens the albedo there).
- **Yardangs.** Ridges and troughs from a noise stretched long along `wind` and narrow across it, `yardangs` metres
  deep; dust darkens the troughs.
- **Pads.** The ground eased to a level within a radius, blending over the last fifth, before the curvature: a footing
  for a structure placed on the ground (the pyramids' sand plains).

## Consequences

- Mars is a lunar world with a warmer program, a sky and a haze (`build_mars.py`): no new terrain system.
- A pad's level is measured before the curvature: a node standing on it subtracts d^2 / 2R (the builder does).
