# DIGITAL MOSH: Correction plan (pass 6, the last pass)

The spec for this pass is `00b-art-direction-correction.md`. Where it conflicts with earlier design, it wins. This
plan replaces the world of passes 1-5:

- the dunes, the riverbed and the mesas are gone;
- the land is gone;
- corruption is off until the world passes §13's test.

Kept:

- ADR-1160 to ADR-1166, including the bounded integrate, the quantize op, the contagion grid, the scaled SDF
  shadows and the banking spline camera;
- the arc and the audio layers;
- the soaring flight;
- the FPS discipline.

## The world: one mirror, and tableaux standing in it

There is no terrain grammar any more. The ground of the whole dream is **a single perfectly still mirror to the
horizon**:

- It is a sea with no shore, at y = 0.
- It reflects the sky, the sun and Dalí's horizon band, and that band is the only horizon.
- It is the connective tissue: the flight skims it between tableaux.

The renderer has no screen-space or planar reflection of objects. So each object's reflection is **built**: a
mirrored copy under the surface, seen through a near-transparent water whose depth colour is the sky's own.

**This turns a limitation into the scene's central impossibility.** A reflection can differ from what it reflects
(§4, "reflections containing objects that aren't present"; Dalí's *Swans Reflecting Elephants*).

| | Tableau | The impossible idea | Iconic object(s) | Palette (from the paintings, `05-palettes.md`) |
|---|---|---|---|---|
| A | **The Mirror** | The flat infinite plain; objects stand in it half-submerged; their reflections are other things | The plain itself; a half-sunk Tanguy form whose reflection is the eye's moon | Pale Dream: cream `#e4d7bf`, ochre `#c1ae5e`, dusty rose, pale turquoise `#94acbe`, lavender shadow |
| B | **The Eye** | A colossal eye lies in the mirror as if it were the plain's own eye, iris to the sky: the plain is watching. Its reflection is a moon | A 60 m eyeball, half-submerged, its painted iris turned toward the passing camera | Iris in the Bee's turquoise and ochre, pupil ink, the white a wax-stone cream |
| C | **The Hanging Land** | An inverted island: a 140 m slab of the dream's earth hangs 70 m over the mirror, its olive growing down from the underside, its roots up into its own sky. Its reflection hangs the right way up, so the flight between them has two horizons and no "down" | The slab; the olive, inverted; the Tanguy object hanging from it | de Chirico's warm orange light on its edge; the underside in Persistence's earths |
| D | **The Door** | A free-standing door in the mirror, through which it is night (Magritte, *The Empire of Light*): a painted night sky with a moon in a doorway under a day sky. A door is also the threshold the flight passes | A tall door frame and a night sky panel | Empire of Light: day `#94c0d9` around it, night `#25292a` and a lamp's yellow inside it |
| E | **The Stair** | Scale collapse: a staircase rising out of the mirror whose every flight is half again larger than the last, so it is miniature where it leaves the water and enormous where it meets the sky. Beside it a flower taller than the stair, impossibly thin | The stair (ADR-1040 stairs, 6 flights at 1.5^k); the colossal flower | de Chirico's arcades: cream stone, long green-black shadows `#223b37` |

**Spatial impossibilities (§4)**

- Reflections that are other things: the eye's reflection is a moon, and the hanging olive's reflection stands
  upright.
- A horizon inside another horizon: the door, and the gap between the slab and its reflection.
- No direction is down: under the slab.
- An object receding but growing: the stair.
- Night inside day: the door.

**Composition (§6).** Every tableau is a painting with enormous empty space: the mirror and the sky are most of every
frame, and each tableau holds one bizarre idea, off centre. The flight frames each tableau the way a painter would
hang it, low over the mirror and with the horizon in the lower third or upper third, never centred.

**Materials (§8)**

- The eye's white is wax-stone: smooth and slightly translucent-looking (subsurface-like through the sun's rim).
- The slab is earth with a painted look.
- The door's night is a painted surface that is lit as one.
- The stair is chalk. The flower is porcelain.

## Corruption back on (only after the world passes)

The corruption attacks the symbols, and each one transforms (§9-10).

- **The eye:** eye → moon → hole → portal → pixels.
  - Infection: the contagion starts in the pupil; its cells fail in the iris.
  - Corruption: the eye rises out of the mirror into the sky and becomes the moon (its reflection had already said
    so), leaving a black hole in the mirror where it lay.
  - Nightmare: the hole opens into a ring of the strain's light, a portal.
  - Collapse: the moon, the hole and the portal come apart into blocks, into pixels.
- **The olive:** tree → hand → roots → veins.
  - Its hanging roots lengthen toward the mirror, and their reflection rises to meet them.
  - The contagion runs down them like veins: the rot climbs the olive, the ink runs along it.
- **The stair:** its flights shift (they re-scale on the kick) and then fall apart into blocks.
- **The door:** its night spreads into the day sky around it: the strain's light reaches the haze. The mirror tears
  along stepped seams (the water's tears).

**Music drives the identity changes (§11)**

| Music | Drives |
|---|---|
| The arc | Which stage, and therefore which form each symbol has |
| Bass | The slab and the eye breathe |
| Kick | Fractures and the stair's re-scale |
| Snare | The contagion's spread and discontinuities: mosh, only from the Corruption on |
| Treble | Spores and fine dissolution |
| Spectral centroid | The strain's hue |
| Intensity | How far the stage has gone |

## The gate (§13)

1. First the world alone, every corruption system off: one still per tableau and two from the flight, in
   `wip/pass6-world-only/`.
2. Judge them honestly against "would I hang this on a wall?" and iterate until the answer is yes.
3. Only then turn corruption back on.
