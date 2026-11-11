# ADR-2004: An edge-on side spans the gap to its neighbour, so a gap's walls do not open into a dotted line

- Status: Accepted (Musical Geography, CPD-R11, the owner's live screenshot, 2026-11-11), scene/musical-geography.
  Amends ADR-1959 (the slab fast path's vertex stage).
- Code: `ProceduralGeometry::slabEdgeOn` (`"slabEdgeOn"`, default 0.1, written only when changed;
  `src/scene/procedural.*`), carried in `windTiming.w` on a slab draw (`src/rendering/procedural_renderer.cpp`), and
  the rule in `slabVertex` (`shaders/procedural.wgsl`).
- Tests: `[gpu][procedural][geography][adr2004]` in `tests/rendering/test_procedural_slabs_gpu.cpp`. A flat tiles ring
  is seen low down a column gap. With the rule off, the gap's walls show isolated black pixels (holes onto the
  background); with the default they close, and nothing else in the frame moves.

## Context

The owner saw a thin dotted vertical line in a live screenshot. Art reproduced it at 58 s on draft8b and found it
plain in an offline render.

**Measured.** Draft8b at 58.0 s, captured live (`--live-profile`, ultra, a 60-frame warm-up) and rendered offline (the
same instant, matched against an offline sequence to the frame):
- **The offline frame has the dots too.** Art's offline crop was of another spot.
- **Not lighting or post.** The dots stay with the sun's and moon's shadows off, AO off, fog off, glossy reflections
  off, grain, dither, bloom and tilt-shift off, and a plain material in place of art's program. They stay with the
  underlay and the coarser rings removed (mgTiles1 alone), and on the general (non-slab) path.
- **Not shadow resolution.** Changing it (1k, 8k) does not move them.
- **The AOVs show what they are.** At every dot, the normal is +z and the depth is 1.0-1.4 m *behind* the wall pixels
  beside it (normal -x). The slab face-debug colours agree.

So the dots are holes. The line is a column gap running away from the camera. Its walls, the sides of consecutive
tiles in one plane, are seen nearly edge-on. Between one tile's side and the next is the 0.34 m row gap. Seen down the
column, each of those gaps opens in the thin wall band as a slit a pixel or two long, through which the faces deeper in
the gap show. In a long row they make a regular lattice: the dotted line. It is geometry rendered as it is, not a pass.

Things that did not fix it:
- **Closing gaps under a pixel on screen.** Measured along the tile's axes, as the far-tile rule does: at 1 px nothing
  changed. Only at 16 px did the dots go, and by then the rows' gaps had closed across the middle distance (a visible
  look change).
- **Extending every side across the gap.** The dots went, but the walls that face the camera grew dark blocks.

## Decision

A side face seen nearly edge-on spans the gap to its neighbour in its own plane:
- **The test.** `|cos|` between the face's normal and the direction to the camera from the tile's centre. The rule is
  full below `slabEdgeOn` and off above twice it.
- **What moves.** The face's in-plane horizontal width eases out to the lattice pitch, the same ease the far-tile rule
  uses for a closed gap.
- **Why it is invisible.** An edge-on face has almost no area on screen, so the overlap shows nothing of its own. It
  only stops the slits from opening.
- **Where it applies.** The camera's views only: the prepass and the lit pass compute it from the same expression and
  agree to the bit. A shadow view keeps its gaps.
- **Cost.** One dot product per side vertex.
- **Scope.** Only "tiles" generators, whose pitch is wider than the box, are affected.

## Consequences

Measured on draft8b at 58.0 s, captured live with a 60-frame warm-up. The count is isolated dark pixels in the
120 x 240 px box on the owner's wall:

| | before | after | rest of the frame changes by |
|---|---|---|---|
| live high | 64 | 6 | mean 0.03 levels |
| live ultra | 80 | 6 | mean 0.02 levels |

**Cost: none measurable.** Draft9 at 85 s, live-profile headless at ultra, rule off vs on, two runs each:

| | GPU p50 | GPU p99 |
|---|---|---|
| rule off | 33.32 / 33.36 ms | 45.72 / 45.95 ms |
| rule on | 33.42 / 33.07 ms | 46.02 / 45.27 ms |

The differences are within run-to-run noise.

Media: `18-pass-cpd-draft9/cpd-r11-*`.

**Not this rule.** Art's crop also shows faint vertical streaks on tile tops. They come from the live ambient occlusion,
which runs at half resolution (`aoResolutionScale` 0.5 at every live tier; offline runs it at 1.0):
- With AO run at full resolution live, the streaks are gone.
- Scaling GTAO's occluder bias to the AO texel did not remove them (7.1 -> 6.6 levels against the full-resolution
  frame), so it was not kept.
- Full resolution costs frame time this scene does not have. It is left as a decision for art and the coordinator
  (02-requests, CPD-R11).
