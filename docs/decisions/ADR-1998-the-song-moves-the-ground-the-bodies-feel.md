# ADR-1998: The song moves the ground the bodies feel, and a crest throws at its fastest rise

- Status: Accepted (Musical Geography CP-D r3, the owner's draft4 note on crest throws, 2026-11-10),
  scene/musical-geography. Amends ADR-1964 (the planning surface) and ADR-1997 (the throw's surface).
- Code:
  - `scene::TileMotionTrack` (`src/scene/tile_motion.{hpp,cpp}`);
  - `TileGround::setMotion` (`src/scene/tile_ground.*`);
  - `Composition::tileMotionParameters`, `setTileMotionSeries`, `setTileMotionLive`, `rebuildTileMotion`,
    `ensureFlightTable` (`src/scene/composition.*`);
  - `Engine::refreshTileMotion` and `ReplaySignals::replays` / `sampling` / `musicalBeats` (`src/app/engine.*`);
  - `params::kModOpPriority`, `params::applyTrackMode` (now public).
- Tests (`tests/integration/test_tile_motion.cpp`, `[tiles][hop][geography][audio][adr1998]`):
  - an audio route swells the ground: a cold seek lands every body where a 30 fps play does (1 mm), the drawn swell
    is bit-equal, 60 fps lands where 30 does, and a frame after the seek plays on as the play does;
  - a louder routed swell throws more and higher (no route 0, amount 1 fewer than amount 4, which throws 3 or more);
  - the ground a body is thrown by is the ground the renderer is handed (`environment.tileDynamics`), to 0.1 mm at
    three probes over 20 s;
  - every throw leaves at the fastest rise of that swing under the body (the rise there is at least a step either
    side, and over the threshold);
  - `[.][measure]`: per frame on a real project, the drawn swell, the ground's rise under one body and its launches.
  - Canary (`AVGEN_NO_TILE_MOTION=1`, the ADR-1997 surface): the second and third tests fail (0 throws at every
    loudness; the throw surface 3.0 m off the drawn ground).

## Context

The owner on draft4: "have them react to the changes in the landscape. If a wave of energy goes through the land,
have it toss the alien up". ADR-1997 threw the aliens on crests, but read the crests on the planning surface: the
tile parameters' bases. The routes that make the ground musical -- `response.low` on the swell's amplitude, the rings
`response.onset` drops, `audio.energy` on the breath -- were left out, because a seek did not replay them and a body
that read them would have done one thing on a play and another on a scrub (ADR-1964). So the throws followed the
waves' authored rhythm, and the crests the viewer saw and the ones that threw the aliens disagreed.

What a seek can and cannot reproduce, measured:
- Offline, every input of those routes is a function of time: the analysis of a known file, the beat grid, the
  stems (ADR-870/901/1520). ADR-901's replay already rebuilds them at any instant.
- A route's chain is history (attack, release, spring). A play advances it once per frame, so its value depends on
  the frame rate: on the test rig the drawn amplitude at 1.0 s is 3.0836 at 30 fps and 3.0352 on a 60 Hz grid (1.6%).
  A body reading the play's finals would be thrown differently at 30 and 60 fps, and on a seek.
- The rings are placed 150 m ahead of the camera at the frame their trigger rises; a forward seek never invented
  the rings between (ADR-1953), so the drawn rings themselves were not seek-safe.

## Decision

1. **Record the ground's routes over the song.** Offline, the engine runs the routes onto the ground's parameters
   once over the whole song on a fixed 60 Hz grid -- an ADR-901 signal replay from zero -- and records each route's
   output (amount and gains applied) and depth per sample, and the beat position. It then composes each parameter's
   final value per sample as a play builds it: the base, the timeline's keys (seconds or beats), the routes in the
   modulator's op order with ADR-900's depth, clamped. The parameters are the ones that move the ground's height:
   the amplitude, each train's amplitude, speed, wavelength, direction and `onLand`, the breath's amplitude and period,
   the drift, the rings' amplitude, speed and decay, and what triggers and places a ring.
   - A route this replay cannot rebuild (a control, a scene state, an entity, an envelope or random source, a set
     piece's beat) leaves its parameter on the live path, as before.
   - Recording costs once per change of its inputs: Feline Footwear's 210.5 s, three routes onto 23 parameters,
     36 ms. A base or a key changed only re-composes. The live route state is saved and restored around it.
2. **One ground at every instant.** The composition turns the series into a `TileMotionTrack`: a lane per moved
   scalar (linear between samples, held outside) and the rings, found as the trigger's rising edges through 0.5 on the
   grid and placed 150 m (`rings/ahead`) ahead of the flight's camera at that instant (the flight is a function of
   time; with no flight, rings ahead of the camera stay live). Every surface reads it at the instant it is asked
   about:
   - the drawn ground (`environment.tileDynamics` each frame: the lanes and the rings at the frame's instant);
   - the bodies' stance (`at`), plan (`planned`) and throw (`landscape`), through `TileGround::setMotion`.

   So the crest that throws an alien is the crest drawn under it, the music included; seek equals play; 30 and 60 fps
   agree; a scrub sees the rings a play saw.
3. **Live:** no seek, real-time analysis. The bodies read the live ground (the finals and live rings), and the lanes
   run from the last frame's values to this frame's, so a swell's rise between two frames reaches the crest a body
   feels.
4. **The throw comes at the swing's fastest rise.** The owner on art's close clip (alien09, 41-50 s): "the near alien
   jumps before the wave even reaches him. It should look like the wave is throwing the alien upwards as it passes
   through." Measured on that clip (`cpd-r3-musictoss-timing-plot-*`), the ADR-1997 rule had two faults: it read the
   bases' swell (rest amplitude 0.35, while the drawn swell was 1.2: the drawn rise under the alien peaked at 0.87 m/s
   at 43.4 s, the surface it read at 0.37 m/s at 43.8 s), and it fired once that rise had fallen 10% under its best
   (44.1 s), so the throw was keyed to neither surface's peak; the drawn crest's top then passed under the alien in
   the air. Now, because the landscape is a pure function of time, a body whose rise starts a rising swing over the
   threshold looks ahead along it (until the rise falls under the threshold or a fifth under its best, 4 s at most,
   then again) and is thrown at its fastest instant, at that speed. A cooldown that ends past a swing's peak waits for
   the next swing; a walk onto another tile looks again. Not a time offset: the step and the render clock agree
   (ADR-1997's fixed step; the test samples the same instants).
5. **The planning surface is the drawn one, less the bodies' own landings.** ADR-1964 kept the plan on the bases so a
   seek would land; the record makes the routed ground replayable, so the plan reads it too. A path may now differ
   with the music, deterministically.

## Consequences

- The drawn swell offline is the record's, not the modulator's final: they differ by the chain's frame-grid error
  (1.6% at worst on the rig, while the swell attacks). The Parameters panel still shows the modulator's final.
- On art's project the throws follow the music: Feline 0-60 s, alien05 3 launches before, 12 after; alien02 15 and 18
  (`12-pass-cpd-r3/cpd-r3-musictoss-plot-*`; launches include walking hops). The swell's rise now reaches 0.5-1.2 m/s
  and the rings' fronts 3-5 m/s: at art's `tossGain` 30 most throws reach `tossMax` (11 m/s, 6.2 m).
- A route on `env/tiles/*` that the record cannot carry keeps ADR-1964's behaviour for that parameter: drawn, not
  felt.
- `AVGEN_NO_TILE_MOTION=1` is the A/B arm (ADR-1997's surfaces; the timing rule of 4. stays).
- The landing springs, not the swell, dominate the drawn motion of a tile under a resting alien (±2-6 m/s against the
  swell's 0.3-0.8 m/s, measured on the clip). The throw does not read them (a body's own landing must not throw it
  again), so a spring's bob can look like a cue. At art's values the land's swell is too small to read under a 6 m
  throw; a tuning canary (`onLand` 1, `tossThreshold` 0.5, `tossGain` 8, landing `strength` 0.5) reads as the crest
  band throwing the alien (`cpd-r3-musictoss-timing-close-alien09-40-55s-after-tuning-canary.mp4`). The look is art's.
- Rings ahead of the camera are placed by the flight's camera even when the flight is disabled (a parked review camera
  then sees the film's rings, and they throw).
