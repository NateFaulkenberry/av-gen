# ADR-1961: The sun placed per state and along the timeline, off its arc

- Status: Accepted (Musical Geography, 2026-11-09), scene/musical-geography.
- Code:
  - `DayNightSettings::sunAzimuthOffset` / `sunElevationOffset` (phase-keyed curves) and `sunAzimuthBias` /
    `sunElevationBias` (scalars), in `src/scene/day_night.{hpp,cpp}`;
  - scene JSON `dayNight.sunAzimuthOffsetCurve` / `sunElevationOffsetCurve` and `dayNight.sunAzimuthOffset` /
    `sunElevationOffset`, all in radians, and the parameters `env/dayNight/sun/azimuthOffsetDeg` /
    `elevationOffsetDeg`, in `src/scene/composition.cpp`.

## Context

DayNight puts the sun on one arc: its azimuth sweeps linearly with the phase from `sunAzimuthAtDawn`, and its
elevation is a cosine of the phase. Musical Geography's art agent turned the arc so that day lights the land from the
side. The same arc then put the dawn sun straight ahead of the views, so dawn was back-lit into haze. A palette state
is a phase, and an arc can't give two states independent headings. The flight (Phase 5) needs the same control along
the timeline.

## Decision

Two additions to the arc, both added to the arc's azimuth and elevation:

1. **Per state:** offset curves keyed by phase, interpolated like every other DayNight curve. Keyed at dawn with 0
   at the neighbouring states, dawn turns and day doesn't move.
2. **Along the timeline:** scalars exposed as parameters (in degrees on the panel), so a timeline key or a modulation
   can move the sun.

Empty curves and zero scalars leave the arc exactly as it was. Both kinds go into the settings hash.

## Consequences

- On art revealC (`02-terrain/eng-r10-*`), a dawn key of +90° re-lights dawn from the side. The ground's mean change
  is 6.25 levels. The day frame with the same curve is bit-identical to the day frame without it, with 0 pixels
  different.
- The moon is unaffected, since it follows the night map.
