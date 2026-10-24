# ADR-1643: A river can draw its flow as streaks

- Status: Accepted (eng/water-flow-streaks)
- Extends ADR-1286 (the cinematic water path) and reuses ADR-914 (bounded advection) and ADR-099 (the flow and the
  channel position the water's vertices carry). It follows ADR-916's and ADR-1642's rule: optional code is compiled
  out of every pipeline that does not draw it.
- Asked for by the Living Prism aurora valley. The reference (`~/Desktop/living prism/new-direction.jpg`) has dense,
  fine, directional streak lines in pink, teal, lavender and white that curve with the river. The water's tears run in
  one fixed direction and read as dashes; the glow patches drift, but they are blobs.
- Code:
  - `src/scene/water_surface.{hpp,cpp}`: `WaterSettings::flowStreaks` and the streak settings, their validation;
    `cinematic()` includes `flowStreaks > 0`.
  - `src/rendering/water_renderer.{hpp,cpp}`:
    - `WaterUniforms::streakA/B/Color1..3` (400 bytes, was 320, within the 512-byte stride);
    - a fourth pipeline, `water-cinematic-streaks`, built from the full source. The three existing pipelines now
      compile the streak blocks out.
    - Per-slot selection, `streaks_[i]`.
  - `src/scene/composition.cpp`, `composition.hpp`:
    - the `water` keys, written only when a streak setting is authored;
    - the parameters `water/streaks/amount` and `water/streaks/color1..3`, registered, applied and removed.
  - `shaders/water.wgsl`, in `flow streaks (ADR-1643)` blocks:
    - the uniforms;
    - `waterFlowStreaks`;
    - the lane derivative and side, taken in uniform control flow;
    - the colour and emission terms.
- Tests:
  - `tests/rendering/test_water_streaks_gpu.cpp` (`[adr1643]`):
    - at 0 the water is byte-identical to the shader with the streak blocks removed, on the plain and on the
      cinematic path, with every other streak setting off its default (and the control: on, it is not);
    - on, the streaks light 5-90% of the river's pixels;
    - 1/30 s later less than 2% of them moved by more than 0.25;
    - hidden `[measure]`: cost.
  - `tests/unit/test_water_cinematic.cpp`: defaults, cinematic switch, round-trip, written only when authored, ranges.
  - `tests/unit/test_water_parameters.cpp`: the four parameters reach the surface. The existing sweep checks they leave
    with the node.

## Decision

**A streak runs along the river's own flow line.** The water's vertices already carry where across the channel they
are (`uv.y`, 1 at the centreline, 0 at the bank, from the same query as the flow). A line of constant channel position
is a flow line, so the streaks are lanes of it:
- `streakLines` lanes from bank to bank;
- they follow every bend exactly, and converge where the river narrows, as a long exposure's streaks do;
- a low-frequency world noise meanders them by up to half a lane.

**Lit in stretches carried downstream.** Each lane is lit where a noise, at that lane's own offset and carried along
the flow at `streakSpeed` times the water's speed, is above the threshold `streakDensity` sets. The carrying uses
ADR-914's two-sample cycle, so no offset grows with time.

**Colour and highlights:**
- each lane's colour runs through the three palette colours by its hash, drifted across the river by a coarse world
  noise so the two banks differ;
- the brightest lanes take white cores;
- lane brightness varies by hash.

**The side of the centreline.** `uv.y` is 1 - |across|, unsigned, so lanes counted from it mirror at the centreline. The
shader recovers the side from the sign of the channel coordinate's world gradient against the flow's left (screen
derivatives of `uv.y` and of the position, solved in uniform control flow), so the lane coordinate is continuous
through the centre.

**A narrow calm band down the centreline.** The vertex value 1 - |across|, interpolated over a triangle that straddles
the centreline, is a sawtooth about a quad wide. At 320 lanes that is about ten lanes of zigzag. Changing the vertex to
a signed value would perturb every existing water surface's flow and glow, so the streaks fade out over about 6% of the
channel at the centre instead.

**Anti-aliasing.** A lane narrower than about a reference pixel (ADR-915; lanes per pixel from `fwidth(uv.y)`) fades to
its mean, so a distant reach reads as a soft tint, not crawling lines. Still water (speed fraction under ~0.03) has no
streaks, and they stay off the bank's very edge.

**A fourth pipeline.** The streak code sits in its own marked blocks. The plain, torn and cinematic pipelines strip
them, so each is exactly the shader it was. The full source is a fourth pipeline that draws only a surface with
streaks. `flowStreaks > 0` makes a surface cinematic, since the streaks need no frame copy but live with the cinematic
code.

## Evidence (M2 Max, AC power, Release; media `~/Desktop/av-gen-review/90-water-flow-streaks/`)

**Unchanged by default.**
- The aurora valley (the scene branch's project, unchanged: cinematic water, no streaks; offline tier, 2x supersample,
  1080p, t = 40 s) renders pixel-identical to main 18cad001.
- The identity test holds plain and cinematic water to the shader with the streak blocks removed, hash for hash.

**The look.** The same project with streaks on (`demo-streak-settings.json`: amount 2.5, 320 lanes, 25 m, density 0.55,
highlight 1.2, pink, teal, lavender), t = 40 s and 100 s:
- dense, fine lines curve with every bend of the river up the valley, over the aurora's reflection;
- the far reach softens to a tint;
- a calm line runs down the middle;
- files: `river-t40-before-after.jpg`, `river-t100-before-after.jpg`, `river-t40-streaks-crop.png`.

**What did not work:**

| attempt | result |
|---|---|
| a line integral of fine noise along the local flow (10 taps over 14 m) | dashes, not lines: the taps were further apart than the noise's cell, so the average had no line in it. A true LIC needs ~35+ taps. |
| noise in coordinates rotated to the flow | rejected unbuilt: a degree of turn swings the pattern metres at a kilometre out. |
| the unsigned channel coordinate | a V of mirrored lanes at the centreline, then a zigzag from the interpolation sawtooth: hence the side and the calm band. |

**Cost** (hidden `[measure]`: the QA river at 1080p, cinematic water either way, the scene timing's median over 6
interleaved reps of 60 frames):

| | scene timing (ms) |
|---|---|
| streaks off | 14.83-15.06 |
| streaks on | 14.93-15.13 |

About +0.06-0.1 ms, within the run-to-run spread.

**Temporal stability.** Between frames 1/30 s apart, under 2% of the river's pixels change by more than 0.25 (the GPU
test). The streaks drift; they do not flicker.

## Consequences

- The scene agent can turn on streaks on the river, set their count, length, density, speed and highlight, and drive
  their brightness and three colours from the palette or the music.
- Flowing bodies only. A pond, or a body with no derived flow, draws none.
- The calm centre band is visible as a darker line down the middle, readable as the river's deepest line. A signed
  channel coordinate in the vertex would remove it at the cost of perturbing every existing surface.
- A narrow river with many lanes resolves fewer of them at distance. The fade keeps that a tint.

## Rejected

- **Extending the tears** (ADR-916): they are seams in a fixed direction, made to compress ripples, not light lines.
- **A flow map texture**: the water has no texture-space flow and would need a bake. The channel coordinate is
  already the stream function.
- **A full line-integral convolution** of noise along the flow: 35+ taps per pixel for the same look at these
  lengths, and only straight-line taps without a flow texture.

## Revisit triggers

- The centre band matters in a shot: carry a signed `across` (a new vertex lane, or the side packed somewhere the flow
  does not read) and remove the band.
- Streaks wanted on still water: drive the lanes from the wind direction instead of the channel.
