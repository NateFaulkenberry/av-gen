# ADR-2021: The imported lake's follow-ups: its identifier, and the frame reflection's tint, blur and edge

- Status: Accepted (2026-11-11), eng/water-followups.
- Request: [docs/requests/2026-11-11-water-followups.md](../requests/2026-11-11-water-followups.md), from av-agent,
  after ADR-2020 went into their alpine scene.
- Code: `WaterRenderer` (`ensureIdPipelines`, `upload`), `WaterSurface::writesIds`, `WaterSettings::ssrTint`,
  `ssrBlur`, `ssrEdgeFade`, `waterEdgeFadeX` and the two reflection mixes in `shaders/water.wgsl`.
- Tests: `[adr2021]` in `tests/rendering/test_gltf_water_gpu.cpp` and `tests/unit/test_gltf_water_materials.cpp`.

## Context

av-agent's alpine lake renders through ADR-2020. Five differences remain against their Blender reference. Each was
checked in the code and on their own scene, rendered here from their files with the same arguments their manifest
records (ground view, 1280x720, offline tier).

1. **The id AOV shows the terrain under the lake.** The water pipeline masks every auxiliary target except colour and
   emission (ADR-035: "normals, velocity and identifiers stay the opaque geometry's"). The shader already computes the
   identifier, but the write mask throws it away. Proven: ground view, rows 600-700, cols 300-900: ids 22806876 (55,461
   px) and 23396708 (4,539 px), exactly their numbers.
2. **The reflection is grey.** `reflectionTint` and `reflection` multiply the environment cube's sample (and the
   styled sky's) and nothing else. The march's hit and the sky mirror's sample are multiplied by `ssrGain` alone.
   av-agent guessed right. This is not a defect: `water_surface.hpp` documents `reflectionTint` as "the sky's colour
   as this water returns it" and `ssrGain` as "the frame's reflection, multiplied (the environment keeps
   `reflection`)". No key coloured the frame's reflection.
3. **The far treeline is a light band.** The guess (the 15 m step, and the mirror's trust distance) is not the
   cause. The cause is the march's blur. A hit is averaged over seven taps at radius
   `(roughness * 0.9 + spread * 0.5) * hitS / depth`, capped at 3% of the frame's height. Roughness is taken
   linearly as a cone angle, so at roughness 0.04, a hit 1 km down the ray from a lake pixel 200 m away gets the full
   3%: about 22 px at 720p, wider than a tree. Proven by changing one term each, on their scene:

   | ground view, rows 405-430, cols 0-640 | mean RGB | column std |
   |---|---|---|
   | as rendered | 153, 165, 174 | 11.7 |
   | far fade off (`far = 1`) | 153, 165, 174 | 11.7 |
   | blur radius 0 | 143, 153, 158 | 22.8 |
   | the march's mix removed (mirror and cube only) | 167, 183, 198 | 8.1 |

   The trees come back crisp with the blur at 0 (`out/trees-strip.png` in the hand-back). The step size and the
   far fade change nothing. The mirror does not trust the trees: at 1-2 km their copy depth is under 1000 m from where
   it is read, so they belong to the march.
4. **The waterfall is not reflected.** Confirmed. `Fall` and `Spray` are `alphaMode: BLEND` in their GLB. Blended
   entities are drawn after the water (`drawItems(blended, true)`, after the water draws in `SceneRenderer::render`),
   and the frame copy the water reads is taken before the water. The fall is never in the copy. Not built: see the
   options below.
5. **Pale bands at the left and right edges.** The march's hit and the sky mirror both fade to the environment cube
   over the outer 6% of the frame's width (77 px at 1280). That fade is there for a lookup a ripple throws off the
   frame. For a mirror-still lake the lookup point at the edge is on screen and correct, and the fade only replaces
   it with the cube's bright horizon. That replacement is the band.

## Decision

- **Identifiers.** `WaterSurface::writesIds`. A gltf water material (ADR-2020) sets it, and its pixels then carry
  the entity's identifier in the ids target, so the id AOV shows the lake. The renderer draws such a surface with an
  identifier-writing twin of its pipeline: the same module, with the ids target's write mask on. The five twins are
  built the first time a surface asks, so a scene without one builds nothing. A terrain's water keeps the bed's
  identifier as ADR-035 decided. Changing that would move every terrain-water scene that uses object edges,
  `sharpenId` or the temporal slot gate. The test holds a terrain river's ids target identical with and without the
  water.
- **`ssrTint`** (rgb, 0..4, default 1,1,1) multiplies the march's and the sky mirror's reflection, beside `ssrGain`.
  `reflectionTint` stays the environment's. Two keys for two sources, because the request's own evidence shows
  them wanting different colours (the sky's reflection is already close, the cliff's is not).
- **`ssrBlur`** (0..4, default 1) scales roughness's share of the march's blur radius. At 0 a hit is sharp, apart
  from the ripples' own spread (ADR-1868).
- **`ssrEdgeFade`** (0.001..0.25, default 0.06) is the share of the frame's width over which the march's hits and
  the mirror fade at the left and right edges. The top and bottom keep 6%. This departs from the request's own
  suggestion (fade toward the last valid colour, or a darker cube), because the lookups in that band are valid: the
  fix is to stop throwing them away, not to replace them with a better guess. A rippled surface that narrows it will
  show the edge texel smeared in the last few pixels, which is what the 6% was for.
- **Byte identity.** At their defaults the three are the old shader exactly. `ssrTint` and `ssrBlur` multiply by 1,
  which is exact. The edge fade selects the old literal expression whenever the width is 0.06. A GPU test rebuilds
  the pre-ADR-2021 expressions from the live source and compares hashes on the QA river, with the cinematic path and
  the mirror both on and off, plus a control.

The three keys are WaterSettings keys, so a terrain's `water` takes them too, written back only when off their
defaults.

## Measured

On their ground view (1280x720, offline), ADR-2020 as rendered against `ssrTint [0.64, 0.79, 0.88]`, `ssrBlur 0.05`
and `ssrEdgeFade 0.004`:

| | before | after | Blender reference (their numbers) |
|---|---|---|---|
| lake ids, rows 600-700, cols 300-900 | 22806876 x 55,461 + 23396708 x 4,539 | 25035132 x 60,000 (the PBR lake's id) | |
| treeline reflection, rows 405-430 | 153, 165, 174 | 123, 141, 149 | 93, 115, 126 |
| left edge, cols 0-30 vs 40-80 (rows 450-700) | 148, 170, 190 vs 141, 157, 169 | 123, 147, 162 vs 123, 147, 163 | |
| right edge, cols 1250-1280 vs 1200-1240 | 153, 173, 192 vs 156, 168, 178 | 134, 154, 168 vs 138, 158, 171 | |

The colour PNG is byte-identical with and without the identifier write.

Cost: the identifier write adds one 32-bit target to the water's fragments, the tint is a multiply, and the edge fade
is a select. `ssrBlur` does not change the tap count. Live frame on their ground view (`--live-frame --size 1920x1080
--ab nowaterssr --ab-blocks 8`), run back to back:

| scene | baseline GPU | `nowaterssr` arm | water SSR |
|---|---|---|---|
| ADR-2020 keys | 39.00 ms | 31.21 ms | +8.0 ms (all 8 pairs +7.0 to +8.5) |
| with the three new keys | 39.08 ms | 32.00 ms | +7.5 ms (7 of 8 pairs +6.4 to +8.2) |

The frame costs the same with and without the new keys (+0.08 ms, inside the noise). An earlier run of the first scene,
at a faster GPU clock, measured 25.6 ms baseline and +3.1 ms of water SSR, so the absolute numbers move with the
machine. In both runs, at 1080p live, this scene is over a 60 fps budget (16.7 ms) before the water: shadows alone are
8-14 ms. Of the water, the 128-step march is the part worth trimming, through the live ladder (`waterssrhalf` at Low,
off at Emergency) or a lower `ssrSteps`.

## The waterfall: options, not built

| option | what it does | cost | why not now |
|---|---|---|---|
| A. Export the fall as `MASK` (av-agent side) | a cut-out fall is opaque: in the copy and the depth, so the march reflects it | none in the engine | loses the fall's soft edge; their call |
| B. Draw the blended entities before the copy, for a surface that asks | the fall is in the copy for free | zero draws | the water (opaque at `maxOpacity` 1) then draws over any blended thing in front of it: spray at the fall's foot, glass, leaf cards; wrong in general |
| C. Draw the blended entities a second time into the copy, after it is taken | correct: the copy gains the fall, the frame is unchanged | one more draw of every blended mesh, plus scratch auxiliary targets (about 48 MB at 1080p) or write-masked twins of every blended pipeline | not small: a second variant of the whole blended path |

C is the right engine change if A is not enough. Its draw cost is the blended meshes once more, small for a fall and
its spray.

## Revisit triggers

- Another water kind that is an object in its own right (a pool in a procedural) should set `writesIds`.
- A rippled surface with a narrowed `ssrEdgeFade` showing edge smear.
- Option C, when a scene needs reflected glass or particles as well as a waterfall.
