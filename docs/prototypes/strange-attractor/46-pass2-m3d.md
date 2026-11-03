# Art Pass 2, M3d: the art checkpoint

The notes are `44-pass2-m3d-owner-notes.md` (with the owner's additions) and the plan is `45-pass2-m3d-plan.md`. M3d
builds on e1c11e47. It is an art checkpoint: per-scene clips and stills, no full sequence, and the suites deferred
until the art is approved.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3d/`.

## What changed

### Portals (Moon portal as the reference)

- **The pyramid and the launch bay** now carry the Moon portal's mesh wire: its scan, its rim, and its shards breaking
  off the lips into the opening. The pyramid had none.
- **The pyramid portal starts much earlier.** Its glyph field reaches 1700 m (M3c: 1100 m) and its tear starts at
  520 m (M3c: 330 m), so the code is building on the face as the fly-over leaves the rover.
- **The one-frame pop (ADR-1903).** Frame by frame at 30 fps, the pyramid, river and launch-bay crossings each had
  exactly one frame showing the world being left: the pyramid's code, the riverbed's code, or the launch bay's
  interior. Three fixes:
  - The composite fills the frame once the camera is through the plane.
  - A refused hand-back now draws the world plainly instead of nothing.
  - The crossing knots sit half a frame off the 30 fps grid (+1/60 s), because the frame landing exactly on the
    crossing instant was degenerate.

  Measured again: no pops at any of the six crossings (`01-portals/pops.txt`).
- **Portal 1:** the scatter is kept 18 m clear of the opening, so the rounded rocks inside it are gone.
- **The launch bay:**
  - The subs' sorties all finish before its code gathers: the last launch is at 136.4 s, before the bay opens from
    about 140 s.
  - The home exit is high in the outer cliff, 86 m up its face. The flight back is never closer than 26 m to the
    ground at any knot, so it cannot clip rock or shrubs.

### The canyon: the Sasquatch area and the night camp

- **Fixed:** the cave mouth (the black tunnel) is removed. The burger rocks are replaced by photoscanned boulders,
  stones and shrubs (Poly Haven, CC0). Each is set on the lowest of five ground samples under its footprint, and only
  where those samples agree within 0.35 m, so nothing perches on a slope or a lip.
- **The night camp, research-led (`45-` plan):**
  - The fire is the key: brighter, shorter reach, hard shadows.
  - The moon is a cool rim from behind, raised; the fill and the sky are lowered.
  - The smoke is lit warm and fades.
  - The embers rise 1-2 m and die.
  - A photoscanned stone fire pit and split logs replace the toy teepee.
  - The seat is a photoscanned boulder.
- **Close-up texture:** the canyon's rock program gains a photoscanned stony-ground texture. It is multiplied into the
  program's own colour near the camera (to 140 m), so wide shots are unchanged.

### The Moon

- The pixel corona animates. Three layers of blocks breathe in scale out of phase (the timeline), so the blocks drift
  out and back. Two layers flicker in on the highs and the low onsets. The beat pulse and the datamosh are kept.

### Mars (ADR-1902)

- **The terrain is the canyon's own pipeline in a new Mars style.** The domain-warped erosion front stands up as mesas
  and buttes in layered sediment, with the canyon's cliffs, talus, strata, terraces and gullies. There is no gorge or
  river. Yardangs and transverse dunes cover the floor. The layout is carried onto a wall with open plain in front of
  it, found by a search of the height field.
- **The fly-over:** out of the wall low, over the rover as it drives, straight on to the pyramids. There is no orbit.
  The rover is nearer the pyramids.
- **Tracks:** treaded ruts with raised berms. They fade and flatten with distance behind the rover.
- **The sand:**
  - The references are the historic photographs of the Sphinx buried to its neck with sand banked against the
    pyramids: Zangaki, *Le Sphynx et les Pyramides*, and Bonfils, *Great Sphinx of Giza*, 1888, both public domain via
    Wikimedia Commons, downloaded to the gitignored assets folder.
  - The drift is highest mid-face, scoured at the corners, and climbs into the courses.
  - It is textured with a photoscanned rippled sand.
- **Pyramid stone:** a photoscanned weathered sandstone on every block, tinted by the block's own colour. The plain
  near the camera gets the rippled sand through the terrain program. The rover's rocks are photoscanned boulders.

### The ocean

- **The seafloor is on the canyon pipeline (ADR-1902)**, as a reef: eroded outcrops, sediment drifts, the floor
  falling away. The box-like basins are gone. The base sits on a pad at a reef wall's foot.
- **The subs** launch on shallow climbing lines, out and up, and return on shallow descents. They are still fast.
- **The camera rocking** was traced to the M3c path: its absolute heights over a dipping seabed nodded the camera in
  pitch by up to ±7°. The new path is one smooth line. Measured, per frame:

  | | Max pitch rate | Pitch range | Max yaw rate | Roll |
  |---|---|---|---|---|
  | Before | 10.6°/s | -6.5° to +8.1° (3 reversals) | 5.1°/s | flat |
  | After | 0.7°/s | -5.8° to -5.0° (no reversals) | 3.6°/s | flat |

- **Wildlife:**
  - Rigged, animated procedural animals (`pass2/build_sealife.py`): 12 fish cruising with a swimming wave down the
    body, 3 rays flapping, and 10 glowing jellyfish pulsing, all keyed in the timeline.
  - Denser kelp, sponges and corals.
  - The particle schools are kept.

## Not done (owner asks still open)

- The schools do not scatter when a sub passes (a particle parameter route was not wired).
- No crabs or other bottom dwellers.
- The night camp's bank behind the group is still a broad slope: no new rock forms were modelled into it.
- The pyramid's mesh-wire shards include drift-sand triangles, which read as long strips for a second after the
  crossing.
- The rover's photoscanned boulders are tinted, not re-textured for Mars.
- The crossing-instant degeneracy is avoided by timing, not fixed in the engine. At 60 fps the shifted instants fall
  on the grid again.
- The canyon-scale POC files (`poc-*.scene.json`, `strange-attractor-poc.json`) were not rebuilt for M3d. The new Mars
  and ocean worlds need new ground samples at that scale, and the sampler is a temporary test.

The suites were not run (an art checkpoint; deferred until the art is approved). The builders reproduce the committed
main scene files byte for byte from `ground-cache.json`, and the `[attractor-world]` and `[lunar-world]` tests pass.
