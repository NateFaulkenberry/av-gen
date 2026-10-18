# RD Pins scene: research (owner decision, 2026-10-17)

The owner chose C3, "pins": copper rings on black, a labyrinth that turns to spots. It becomes a full live scene.
This document is the research and the option debate. The decisions and stage gates are in `C-scene-plan.md`.

## 1. How professionals build reaction-diffusion and Turing-pattern art

**Karl Sims, "Reaction-Diffusion Tutorial" and the Xmorphia map (Pearson 1993; Munafo).**
- The (f, k) plane is a map of named behaviours: spots, mitosis, coral, worms, holes, waves, chaos.
- The art is choosing a *path* through that map over time, not one point on it.
- Sims' installations use a spatially varying f and k (a gradient or a painted map) so one image holds several
  pattern families and their frontiers. That frontier is the most interesting part of any RD image, and it is what
  our regime field does.

**Jonathan McCabe, multi-scale Turing patterns (2010).**
- Several activator/inhibitor pairs at different radii are summed, so large-scale structure carries smaller
  structure inside it.
- The lesson for us is hierarchy. A single-scale RD reads as a texture; two scales (a slow, broad regime map over
  a fine pattern) read as a composition.

**Nervous System, "Reaction" and "Kinematics" products, and Entagma or Houdini RD solvers.**
- Professionals run RD on the *surface* (a mesh or a UV grid), then build geometry from it:
  - extrude by V;
  - remesh at an isocontour;
  - instance onto it.

  The pattern is a mask driving geometry, not a shader.
- Houdini artists (Entagma's "Reaction-Diffusion" series, SideFX Labs) typically:
  - **Grow over time from seeds,** never by stamping a finished state. The growth itself is the animation.
  - **Advect or warp the inputs** (feed and kill maps, seeds) with a slow noise, so the pattern never settles.
  - **Clip the field at a contour** for the final geometry. The interesting edge is the B ≈ 0.2-0.3 isoline.
  - **Light it as a material, not as glow.** Coral reads through raking light, cavity darkening and subsurface
    tint; emission is reserved for an accent.

**Pin-art and kinetic-array installations.** For example, ART+COM "Kinetic Sculpture" (BMW Museum), MIT Tangible
Media "inFORM", and Daniel Rozin's mechanical mirrors.
- These read because of three things:
  1. **Many identical elements at a regular pitch,** small enough to read as a surface at a distance and as objects
     up close.
  2. **One continuous field drives them,** so neighbours agree and shapes emerge.
  3. **Specular light on the element tops** (a polished head catching a key light), with the bodies falling into
     shadow.

  The arrays are rarely self-luminous. The shape is drawn by highlight and occlusion.
- **Our C3 frame's weakness is exactly the opposite.** Only the boundary pins emit, and everything else is
  unlit black. That is why the owner saw a void.

**Shading references for dark scenes with designed darks: Kuksi, Fabergé enamel, and the "copper and verdigris"
look in game environment art.**
- Dark areas keep information: low-value colour (indigo or oxblood, not black), cavity tint, and a faint
  environment reflection on metal.
- Professional graders keep the darks above black, around 2-5% luminance, and colour them. They never let the
  majority of the frame sit at 0.

**Live visuals practice (TouchDesigner and Notch RD scenes).**
- They run RD at 256-1024² in a feedback buffer.
- Audio usually adds to a seed or feed mask (localised), never the global feed. A global feed change makes the
  whole frame flash or collapse.
- Performers like a "clear" or "erase" gesture and a "seed here" gesture. This matches our MIDI notes and CC 5.

## 2. What the C3 prototype gets wrong, measured against those references

1. **The void.** In the reviewed frame, a black hole in the middle. A kick ring injected so much B, at width 0.9 m,
   that the centre went solid, and a solid region has no boundary. Since only boundary pins were lit, it rendered
   black.
   - **Fix:** never let a solid or empty region render as black. Every pin body carries the pattern's own colour.
   - **Fix:** kicks must seed rings of new growth that stay below saturation.
2. **Black between the rings.** The floor is lacquer black, unlit, and so is every pin not on a boundary.
3. **A flat plane to a hard horizon,** with fog as the only depth cue.
4. **Lighting.** A single key with shadows (9 ms) and no designed rim or environment light on the metal.

## 3. The option debate, per axis

### 3.1 World and composition

| option | for | against |
|---|---|---|
| A. Flat field, camera low and oblique, the field running to a dark coloured horizon | Simplest; the pin rows' perspective is strong | It is the flat plane the owner rules out; the horizon is a world edge |
| **B. A shallow bowl or amphitheatre of pins** (the pins rise on the bowl's walls; the camera sits on the rim looking across and down) | The field fills every edge of the frame (the far wall rises above the eye line, so there is no horizon); depth comes from the bowl's curvature and the pins' scale gradient; it reuses the proven Phase Membrane bowl | The far wall's pins are seen side-on (fine: their heads still catch light); it needs a positional offset on the grid distribution (the bed-height grid, done in B-flow) |
| C. Pins on a large sphere seen from inside or outside | Strong silhouette | Outside: a ball in a void again. Inside: disorienting, and an RD plane does not map to a sphere without seams |
| D. A valley (C1's terrain) | Proven to fill the frame | A valley has one dominant direction; the bowl is more enclosing and lets the camera orbit slowly |

**Pick B, the bowl.** The pattern domain is the bowl's plan (an RD plane wrapped over it). The far wall rises above
the camera so the frame is all pins. The camera makes a slow 90 s orbit arc on the rim, never following the music.

### 3.2 Pin and ring geometry and material

| option | for | against |
|---|---|---|
| A. Square boxes (C3) | Cheapest (12 triangles); crisp | Squares alias into a grid at 1080p; the rings look stepped |
| **B. Round pins: a low-poly cylinder with a domed or bevelled head** (8-12 sides) | Round heads catch a point-like highlight from every direction, which is the pin-art look; rings read as rings | About 4-8 times the triangles of a box. At 40k instances × about 80 triangles = 3.2M triangles, which needs measuring |
| C. Point billboards (sprites) | Cheapest at scale | No real geometry, no occlusion between pins: it reads as particles (the brief's "particle soup") |

**Pick B if the cost proof holds (≤ about 4 ms more than boxes); otherwise boxes with a bevel.**

**Material:** polished copper heads and darker oxidised shafts.
- Albedo and metal come from the palette roles.
- The pattern sets each pin's height *and* its body colour:
  - pins on the pattern (V high) are bright metal;
  - pins off it are dark oxidised metal;
  - pins on the boundary (the B ≈ 0.2 isoline) carry a thin emissive band at the head (the "ring").
- So a solid region reads as a raised plateau of bright heads, an empty one as a low dark field with a faint sheen.
  Neither is a void.

### 3.3 Environment and lighting (no dead space)

| option | for | against |
|---|---|---|
| A. Black floor and black sky (C3) | | The void |
| **B. The bowl closes the view; between the pins is a floor that reads the pattern dimly** (a low-value tint of the pattern colour, plus cavity darkening); a warm key grazing across the bowl; a cool rim from behind; environment light from a dim coloured sky for the metal to reflect | Every pixel carries colour and pattern; the darks are designed (oxblood or indigo, not 0); grazing light gives the heads highlights and the shafts shade | Floor emission must stay low, or the image goes flat |
| C. Volumetric haze | Depth | Cost 7 ms measured; not needed inside a bowl |

**Pick B. Drop the shadows** (9 ms): the pins' own darkening by height (lower pins darker, a fake occlusion from
the pattern) gives the depth cue for free.

### 3.4 Audio and MIDI

| option | for | against |
|---|---|---|
| A. C3's mapping (kick ring of B, snare ring, sustain regime, bass height, hat light) | Proven, has memory | The kick ring saturated and made the void; bass on height pumped the whole field |
| **B. Music edits the *regime map*, the pattern grows from that:** a kick sends a ring travelling outward that flips the regime it passes (labyrinth to spots), so the spots spread in rings; held sound widens the spot pool; bass lifts the pins in a slow swell travelling from the centre (a spectrum field with travel); hats light the boundary bands | The pattern's own growth does the animation, and the owner's "labyrinth turning to spots" is the musical gesture itself. No saturation is possible: the regime only selects the family | The response is slower (the pattern needs about 1-2 s to change family). Kicks also need an immediate cue: a faint ring of light on the pins as the ring passes |
| C. Resonance-driven regime: the resonance field's sand channel (ADR-1405) as the regime map | The cymatic figure written into a surface that remembers it; a direct link to the Resonator family | The resonance is tuned for a plane figure of its own scale; it adds the resonator's tuning surface to this scene; and the figure moves the regime everywhere at once, not as travelling events |

**Pick B, with C as a stage-gated option** (proof render): a resonance figure *blended in* as a second, slower
layer of the regime, if it reads.

**MIDI (concise, the Resonator scenes' pattern):**
- CC 1-6 are the performer's handles:
  1. pattern (labyrinth to spots);
  2. growth speed;
  3. pin height;
  4. audio influence;
  5. erase / starve;
  6. boundary light.
- CC 9 selects the palette and CC 10 locks it.
- Notes 36-39 hold seeds (velocity is the rate) at four places on the bowl.

### 3.5 Palettes (a role-based set; ADR-1420 selector, ADR-1421 change fallback)

**Roles:**
- environment (sky and floor in the darks);
- shadow (oxidised shafts);
- base (low pins);
- pattern (raised heads);
- ring (the emissive boundary);
- key (light);
- rim (light).

**Six states, from the brief's §4.6 and the owner's chosen look:**

| state | roles, in short |
|---|---|
| **Copper Night** (the C3 look) | Copper heads, oxblood darks, amber rings |
| Verdigris | Oxidised green-blue shafts, bright copper heads, teal rings |
| Garnet and Indigo | Red heads over an indigo field |
| Violet and Gold | Violet field, gold rings, restrained |
| Bone and Basalt | Ivory heads, basalt darks, no ring emission (non-luminous) |
| Ember | Black metal, only the rings glow |

## 4. Open questions to settle with proof renders

| # | question | proof |
|---|---|---|
| Q1 | Does the bowl fill the frame with pins from the rim camera? | Still from the rim at t = 40 s |
| Q2 | Round pins: what do they cost and does the ring read better? | Close still and a GPU timing, round against box |
| Q3 | Do the darks hold colour without flattening the image (floor tint, body colour)? | The same still, luminance histogram (5% floor) |
| Q4 | Kick-as-regime-ring against kick-as-B-ring: is there still a void? | 20 s Feline Footwear clip |
| Q5 | Does the resonance sand read as a regime layer? | Still with the resonance-blended regime |
