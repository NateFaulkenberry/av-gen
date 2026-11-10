# ADR-1993: Raised land meets the sea softly, and the coast follows it

- Status: Accepted (Musical Geography CP-D r2, CPD-R3b, 2026-11-10), scene/musical-geography. Amends ADR-1992's land
  lift.
- Code: `landPart` (`src/scene/tile_terrain.cpp`) and its twin in `tileSample` (`shaders/generator.wgsl`).

## Context

ADR-1992 made raised land into land where its profile passed about a half (`smoothstep(0.35, 0.65, w)`). Art's land3
scene shows the result (`cpd-r2-land3-030s.png`, `-035s.png`, and the owner's screenshot: "some of these lumps look
odd"):
- Every island and every headland's seaward edge stood on a vertical cliff of half its height. The worst coastal step
  between neighbouring 4 m cells was 15.7 m.
- The cliff was drawn as a ragged band of separate tall pillars. A land tile's skirt (12 m, scaled by `1 - water`)
  was shorter than the step, so gaps opened between the pillars. The sea slabs beside them are thin, so the dark cell
  sides and the tiles behind showed through. That is the "dark red cells mixed in". It is the land/sea step itself,
  not the rings or the AO: it goes once the step is a slope.
- Nothing keyed to the coast saw the raised land. The shore channel (the beach colour and the flight's
  `snap: "shore"`) still read the continent field alone, so islands had no beach and no shallows.

## Decision

**One coast coordinate.** The land part works out a coast coordinate `u` in the continent's own units:
- the continent: `u = (c - landThreshold) / shoreBand`, so the land ramps over `u` in -1..1 and the shore band is
  `|u| < 3`;
- each ridge or rise: `u = (w - 0.08) / 0.07`, where `w` is its profile. Its land is then `smoothstep(0.01, 0.15, w)`,
  low on the profile, so raised land eases down to a shore foot and the sea floor as the continent's coast does. Art
  suggested 0.05..0.3; the lower ramp brought the islands' steepest step down from 8.0 to 5.5 m, their profiles' own
  slope.

**Which coast wins.** The land's coast is the largest `u`. Where a raised feature's `u` leads, the land and the shore
are taken from it. Elsewhere they are the continent's, computed exactly as before, so a scene without raised land is
unchanged bit for bit.

**The hills stay off.** Inland stays the continent's. Feeding the raised coast into inland put the continent's full
hills (66 m at art's `heightScale` 2.2) on every island and doubled the steps. ADR-1994's `noise` adds relief to raised
land instead, at a strength the art agent controls.

Everything downstream reads the one rule: the shore channel, and with it the shore colour and the flight's shore snap.

## Consequences

Canaried on art's land3 scene (3c9c4018), from its tiles spec. The worst step between neighbouring 4 m cells inside
each feature's footprint:

| Feature | R3 | R3b |
|---|---|---|
| bayIsle | 15.4 m | 5.5 m |
| stack | 14.8 m | 5.3 m |
| southIsle | 15.4 m | 5.4 m |
| pairIsle | 13.9 m | 5.5 m |
| westIsle | 15.7 m | 4.8 m |
| inletIsle | 7.1 m | 2.7 m |
| eastHead | 14.3 m | 5.5 m |
| northShore | 15.1 m | 5.5 m |
| baySpit | 15.4 m | 6.3 m |
| tarnRidge | 10.9 m | 5.6 m |
| capeEast | 12.0 m | 4.9 m |

- Each is now its profile's own slope (`1.5 × height / edge` per metre, about 5 m per cell on art's islands): a hill,
  not a wall. Art can widen an edge for gentler land.
- `westHead`'s and `capeWest`'s worst steps (8.3 and 4.6 m) are the continent's own coast nearby, and are unchanged.
- The auto water levels are unchanged to the millimetre: the 3 rivers' sources and the 6 ponds, including the stream
  sourced on a ridge and the tarn behind one. They read the land height, which this does not change.
- An island gets a ring of shore around its foot; a headland gets a shore down both flanks.
- Tests:
  - `test_tile_terrain` [adr1993]: an island's shore sits at its land ramp, none at its heart; its steepest metre is
    its profile's slope; the flight's shore snap finds its coast; a headland has a shore down its flank.
  - `test_tile_terrain_gpu` [adr1992]: the shore channel is compared CPU against GPU.
