# ADR-2009: A ball's impact tints the tile's outline instead of adding to it; its sparks keep a minimum width

- Status: Accepted (Musical Geography, CPD-R15, 2026-11-11), scene/musical-geography. Amends ADR-2008.
- Code:
  - `TileBallSpec::tint` and `TileBallSpec::sparkMinPixels` (`src/scene/tile_dynamics.hpp`);
  - JSON `environment.tileDynamics.balls.tint` / `.sparkMinPixels` (`src/scene/tile_terrain.cpp`; tint 0-1,
    sparkMinPixels 0-16, else refused);
  - the frame's `ballGlow.w` (tint) and `ballSpark.w` (spark min pixels) (`shaders/common.wgsl`, `scene_renderer.cpp`);
  - `ballImpactLight` now returns the two strengths, and `procShade` lays them on (`shaders/procedural.wgsl`).
- Tests:
  - `[balls][adr2009]`: the defaults, the round-trip and the range checks (CPU);
  - `[gpu][balls][adr2009]`: on a bright cyan tile field, an impact's outline reads lime with tint 1 and stays near
    cyan when added (tint 0).

## Context

ADR-2008 added the impact's light on top of the tile's lit colour. On the dark night tiles that reads as the impact's
colour. On the bright day tiles the sum is a pale cyan-white: the tile already fills most of the range and the tone
map carries a bright sum toward white. No single gain kept the night in hue and the day readable (CPD-R15: ball03 at
59 s, ball10 at 69 s).

Two fixes were weighed:
- **Gains and colours keyed on the song's look timeline.** That needs new timeline plumbing and per-state tuning, and
  a lower day gain only makes a pale mark fainter.
- **A tint blend.** Take the tile's own colour out under the light, then add the light. It costs one multiply-add per
  lit fragment, needs no tuning per state, and leaves the night alone, where the tile under the light is near black.

The tint blend was chosen.

## Decision

- **Tint.** With `k` the impact's glow and spark strengths on this fragment, the tile's colour (and its emission)
  becomes `colour * (1 - cover) + light`, where `cover = tint * (1 - exp(-4 (k_glow + k_spark)))`.
  - The cover saturates early: a strength of 0.25 already takes out 63%. So the outer glow and a faint spark also
    carry the hue.
  - `tint` 0 is ADR-2008's add; 1 (the default) is the full tint.
- **Spark width.** The sparks' lit band is at least `sparkMinPixels` pixels wide on screen (default 3), so a spark
  at 70 m reads as a line, not a broken pixel. The glow keeps the authored `edgeBand` (at least one pixel).

## Consequences

- At night, frames are within a few levels of ADR-2008's. By day the lime glow and the magenta sparks hold their
  hue.
- Cost and media: 02-requests (CPD-R15).
