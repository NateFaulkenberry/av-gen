# ADR-2010: A looped flight never ends; Musical Geography's live release loops its flight and its arc

- Status: Accepted (Musical Geography, the owner's "add this to the app as a live scene in the world section",
  2026-11-11), scene/musical-geography. Amends ADR-1956 (the flight's `loop`).
- Code:
  - `FlightTable::lap`, `kFlightLoopBlend`, `buildFlightTable` and `flightPoseAt` (`src/scene/flight.{hpp,cpp}`);
  - `examples/musical-geography/build.py --release`: `release`, `loop_tracks`, `LIVE_LOOP`, `asset_path`; the index
    entries "Musical Geography" (Live > Worlds) and "Musical Geography (offline)" (`examples/index.json`).
- Tests:
  - `[flight][adr2010]`:
    - the second lap repeats to the millimetre, eleven laps on;
    - played at 60 fps across the wraps, no step exceeds the flight's own speed;
    - the top-down leg comes round on every lap;
    - seek equals play past the wraps.

## Context

A live project's transport is endless (ADR-1342). Musical Geography's flight was built for the song:
- a table of 210.5 s;
- past that, the last pose holds, over the northern sea.

So a live set longer than the song would freeze the camera. ADR-1956's `loop` closed the route back to its first
waypoint, but the table still ended at `duration`.

The look's arc (day, sunset, night) is keyed in song seconds on 212 timeline tracks. In a live set it would hold
night for ever.

## Decision

1. **A looped table covers three laps.** The lap is the time the flight takes to come round its closed route at its
   waypoints' speeds. `duration` is not read on a loop.
   - The first lap is the opening.
   - The second is the one that repeats.
   - The third keeps the second clear of the table's end, where the smoothing reflects.
   - Every waypoint is passed on every lap, so a top-down leg (`nadir`) comes round each time.
2. **Past the second lap, the second lap again.** `t` maps to `lap + fmod(t - lap, lap)`, so the pose is still a
   function of time (seek equals play).
   - Over the second lap's last `kFlightLoopBlend` (10 s, at most a quarter lap), the pose eases (smoothstep) into
     the pose one lap earlier. That is the same point of the route, the second lap's start. The eye's ground,
     clearance and aim can differ between laps, because the ground moves; the wrap has no step.
3. **The live release** (`build.py --release`) writes the two projects the app lists under Worlds:
   - `musical-geography.json` (offline): Feline Footwear, its stems and feeds, the arc in song time, `sonic.live`
     false;
   - `musical-geography-live.json`: live input (`sonic.live`), no file, no stems. Its scene's flight is `loop: true`.
   - Each arc track holds its value at the song's end (210.5 s), runs linearly back to its first key by
     `LIVE_LOOP` (240 s), and loops there. So night turns to a 30 s dawn and the day comes round again. The palette
     and the flight loop independently; neither is tied to a place.
   - The projects are committed. Their scenes are generated (`scene/`, gitignored) because they reference the aliens
     (commercial, never committed).
   - Every path is relative, so a checkout opens them. The scenes are `scene/release-offline.scene.json` and
     `scene/release-live.scene.json`, apart from the look-development scene `build.py` writes by default.

## Consequences

- Closing the route changes the first leg's spline tangent, so lap 1's opening is not frame-identical to the film's.
  The offline project keeps `loop: false` and is the film.
- The return leg is a straight Catmull-Rom closure from the northern sea back to the bay, across whatever lies
  between: water, shore and hills, terrain-followed and cleared like any other leg.
- A look change on the art branch needs `build.py --release` run again for the committed projects to follow it.
