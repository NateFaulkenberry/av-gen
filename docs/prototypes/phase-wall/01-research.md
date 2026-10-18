# Phase Wall: research

2026-10-17, branch `proto/phase-wall` (from `proto/generative-rd` d0e62cf9). This covers how professionals build
kinetic walls, flap and flip-disc installations, and generative work driven by synchronisation, plus the rooms and
light around them. Each section ends with what this scene borrows. The debate and the choices are in `02-plan.md`.

## 1. What we start from

- **The R&D wall (A3 flaps, ADR-1450).** It has 9,600 metal flaps on a flat 150 × 64 m panel. Each flap turns about
  its vertical axis by 0.9 sin θ of its own oscillator. The coupling breathes 1.5 → 14 → 1.5 over 96 s: disorder,
  then islands, then unison. Strain (the activity channel) lights the fronts and defects cyan. Live, it runs at Ultra
  with 0 misses; the 300 × 128 network costs 0.2 ms (`../generative-rd/performance.md`).
- **The review found** (stills `63-generative-rd/A-oscillators/compare/A3-flaps-*`):
  - a flat rectangle seen at an angle, with hard edges;
  - the right 35-40% of every frame is a black void;
  - the far end of the panel shows a dotted moiré: flaps seen nearly edge-on at the grazing angle are smaller than a
    pixel;
  - the colour is a painted phase map (bronze crests, teal-black troughs) rather than light on metal.

## 2. Kinetic walls and facades

**Ned Kahn: wind veils and kinetic facades.** These include *Wind Veil* (Charlotte, 2000, about 80,000 hinged aluminium
panels), the Brisbane Airport car park, *Wind Arbor* (Marina Bay Sands, 260,000 flappers on cables) and the Phoenix
Footprint Center (130,000 flaps). The flaps are small, identical, hinged on one edge and free. The wall carries no
image of its own. What you see is **light**: each flap reflects a different piece of the sky as it swings, so a gust
crosses the facade as a travelling band of glitter. Kahn calls the Brisbane wall "waves in a field of metallic grass".
Physicists have used the facades to study turbulent pressure fields (APS DFD 2023), which shows the wall really
renders an invisible field.
- **Borrow:**
  - the field is shown by **reflection, not paint**: the metal catches environment light, and turning the flap moves
    the reflection;
  - small identical units in the thousands, each with a little individual imperfection (hinge play);
  - the flicker of a front crossing the wall.

**Daniel Rozin: the Mechanical Mirrors** (*Wooden Mirror*, 1999: 830 wooden tiles in an octagon of 35 × 29, each
tilted ±30° by a servo; *Rust Mirror*, *Penguins*, *Pom Pom*). The tiles carry no colour. A few spotlights above make
a tile brighter as it tilts up and darker as it tilts down, so **the angle is the grey level**. The frame is part of
the work (the octagon), and the sound of hundreds of motors is part of it too.
- **Borrow:**
  - a fixed raking light makes angle legible as value;
  - a designed frame around the field (the work's edge is architecture, not a crop);
  - the hush and clatter of many actuators as an audio idea (not built: there is no audio output).

**BREAKFAST: flip-discs and Brixels** (*Brixel Mirror* 2018, *Oceans* at Fontainebleau Las Vegas, 45 ft;
*Multiply 1*, flip-discs black on one side and high-gloss gold on the other). A Brixel spins continuously through
360°, and a flip-disc is bistable. The look depends on **two faces with very different value**: black and gold, or
matte and mirror. That is what turns a small rotation into a large change in the image.
- **Borrow:** the value contrast between what a flap shows face-on and what shows edge-on. Here the edge-on view
  reveals the plane behind the louvres, so that plane must carry colour.

**Architectural kinetic facades.** Examples are the Institut du Monde Arabe (Nouvel, 27,000 light-sensitive
diaphragms), the Al Bahar Towers (Aedas, 2,098 actuated mashrabiya units) and the dECOi *Hyposurface*. A facade
module is a louvre or a diaphragm that **opens and closes onto a lit interior**. The pattern is legible because what
is behind the screen differs in value from the screen.
- **Borrow:**
  - louvres in front of a luminous backplane: as a flap turns edge-on, colour pours through;
  - a structural grid (mullions, bays, piers) that frames the field and gives the eye a scale.

**ART+COM, *Kinetic Sculpture*** (BMW Museum, 2008: 714 metal spheres on stepper-driven wires, a seven-minute loop).
The choreography runs from chaos, through competing forms, to a resolved shape, then breaks down again. The
narrative is the **order parameter**: the audience watches a system find coherence.
- **Borrow:** the 96 s coupling breath is that arc (disorder, islands, unison, release), and the camera should be at
  its widest when the field is most coherent.

**Split-flap boards** (Solari; LAb[au]'s *SignalToNoise*, 512 split-flaps in a circle, treated as a generative
system). The flap is a "pixel" with a sound and a cadence. LAb[au] put the flaps in a ring around the viewer.
- **Borrow:** enclose the viewer; the field surrounds you rather than hanging in front of you.

**Zimoun** (hundreds of identical prepared DC motors, "architecture of sound"). Identical elements produce endless
small differences, and the mass behaves like an organism.
- **Borrow:** per-flap variation (a hashed rotation offset, a value jitter) so that even perfect unison has grain.

## 3. Synchronisation as a visual subject

**Kuramoto in art.** David Rueter's *The Kuramoto Model (1000 Fireflies)* (Northern Spark 2012) gave 1,000 cyclists
lights that synchronised by radio. One light looks ordinary; the work exists only in the crowd. The Weimar *Portrait
of Two Lovers at a Distance* switched to a Kuramoto-Daido model because few agents read poorly.
- **Takeaways:**
  - synchrony needs many units and a wide view;
  - the dramatic moment is the transition, not the synchronised state.

**The physics we can show** (ADR-1450 regimes; chimera and spiral-wave literature: Kundu et al., Phys. Rev. E 2018;
Abrams, Mirollo, Strogatz and Wiley 2008; Liu and Uchida 2025).
- Locally coupled lattices freeze **topological defects** (phase vortices). These are the bright seams between
  islands.
- A Sakaguchi lag α turns rings into spirals, and near π/2 it gives turbulence.
- Coupled metronomes on a moving board are the iconic demonstration. **A flap that swings ±sin θ is a metronome**, so
  the wall reads as thousands of metronomes falling into step.
- **Borrow:**
  - swing, not spin (sin θ drives the angle);
  - the defect light is the signature (keep strain as the emission);
  - unison is only interesting if it moves. A periodic frequency tilt makes full synchrony a slow wave rolling round
    the room, and a coherent mirror-flash rides that wave.

## 4. The room: environment and light around such works

**UVA, *Momentum*** (Barbican Curve, 2014: twelve pendulums of light in a 90 m curving gallery with 6 m walls). The
room is the instrument. The light the pendulums throw across the walls and the curved floor is the work, and a
visitor's path through the room changes it.
- **Borrow:**
  - the installation is the whole room, and the walls continue past the frame;
  - the floor catches light;
  - the camera is a visitor walking.

**Olafur Eliasson, *The Weather Project*** (Tate Turbine Hall, 2003). A mirrored ceiling doubled the hall, and a thin
haze made the light into a volume and dissolved the walls. Mono-frequency light took the colour out of everything
but one hue.
- **Borrow:**
  - haze to give the room depth and colour in the darks (the far wall sinks into a tinted atmosphere, not into black);
  - one dominant hue per palette, with the accent reserved for the defects.

**Asif Khan, the Hyundai Pavilion** (PyeongChang 2018: Vantablack walls studded with points of light, "a window into
the depths of space"). A black that is designed: absolute darkness used as a material, with small lights giving
scale.
- **Takeaway (and a warning):** dark is a choice only when it has structure in it. A black void with nothing in it is
  the R&D frame's failure.

**James Turrell, the Ganzfeld rooms.** The room is filled with a field of coloured light, so the eye cannot find the
walls.
- **Borrow:** the darks carry hue (the backplane and the haze sit in the palette's environment colour, never neutral
  black).

**Data walls and motion design.** In LED media facades and data walls (Refik Anadol's rooms, airport flight boards),
a uniform grid read at a grazing angle is the classic moiré trap. Studios fight it with:
- a pitch large on screen;
- per-element variation;
- haze or depth of field on the far field;
- avoiding thin high-contrast gaps.
- **Borrow:**
  - keep each flap at 8 px or more at 1080p wherever the camera goes;
  - fill the gaps between flaps with the lit backplane, not black;
  - let haze lower the contrast of the far walls.

## 5. Rendering techniques (and what AV Gen already has)

| need | technique | in AV Gen |
|---|---|---|
| Metal that shows the field by reflection | Environment light seen only in reflections: strips and rings in direction space | **ADR-1151 reflection bands.** A flap turning about the vertical axis sweeps its reflected direction horizontally, so it flashes as it crosses a **vertical** strip. A horizontal ring never flashes: rotation about y keeps the reflected direction's height. |
| Angle as value | A raking key from above casts the flaps' shadows on their neighbours and the backplane | A directional key with shadows |
| Colour in the darks | A luminous backplane; tinted height haze | An emissive box behind each wall; environment fog colour |
| A wall that continues | Several walls reading **one** network; the network wraps | Grid readers with their own frames (ADR-1450 channels); `wrap` grids; **16 GPU field slots per scene** (`kMaxGpuFields`), so 2 per wall |
| Per-flap glow without per-pixel fields | The effector writes the instance's emissive, and the program reads `instanceEmissive` | Emission effector (Replace) + material `input` op |
| Specular aliasing | Kaplanyan-style normal-variance roughness filtering (Kaplanyan et al. HPG 2016; Tokuyoshi and Kaplanyan 2019, 2021) | Material op `roughnessFilter`; FXAA at output; **no TAA** |
| Imperfection | A hashed per-instance rotation and value | `variation.randomRotation`, `materialVariation.valueRandom` |

**What AV Gen does not have:**
- **planar or screen-space reflections** (`docs/water.md`): a mirror floor cannot show the walls, so the floor's sheen
  comes from the bands and the key;
- **a field-space warp:** a grid reader's frame is affine, so a truly curved wall cannot read a planar network. Flat
  walls joined at corners can, one reader frame per wall.

## Sources

- Ned Kahn's facades: [Frame, Brisbane Airport facade](https://frameweb.com/tags/brisbane-airport-kinetic-parking-garage-facade-by-ned-kahn-and-uap);
  [nedkahn.com](https://nedkahn.com/?p=1232); [dataphys.org, Ned Kahn's wind-visualising facades](https://dataphys.org/list/ned-kahns-wind-visualizing-facades/);
  [APS DFD 2023, turbulent pressure fluctuations revealed by Ned Kahn's artwork](https://archive.aps.org/dfd/2023/t45/6);
  [Wind Arbor](https://psmedia.asia/publicartsg/wind-arbor); [Kovach, Footprint Center facade](https://www.kovach.net/project/project-footprint-center-kinetic-facade/);
  [Construction Specifier, kinetic facade systems](https://www.constructionspecifier.com/kinetic-facade-systems-adding-a-dynamic-element-to-building-structures/3/).
- Daniel Rozin: [Wikipedia](https://en.wikipedia.org/wiki/Danny_Rozin); [SIGGRAPH history, Wooden Mirror](https://history.siggraph.org/?p=15570);
  [UBC wiki, Wooden Mirror](https://wiki.ubc.ca/Wooden_Mirror).
- BREAKFAST: [Brixels](https://theartistbreakfast.com/brixels); [Dezeen, Brixels](https://www.dezeen.com/?p=1277973);
  [BREAKFAST studio](https://theartistbreakfast.com/).
- Facades: [ArchDaily, mashrabiyas to dynamic facades](https://www.archdaily.com/510226);
  [e-architect, Al Bahar Towers](https://www.e-architect.com/dubai/al-bahar-towers-abu-dhabi);
  [Architizer, dynamic facades actually built](https://architizer.com/blog/inspiration/collections/8-impossibly-dynamic-facades-that-were-actually-built/).
- ART+COM: [Kinetic Sculpture](https://artcom.de/?p=528); [Light Now](https://www.lightnowblog.com/2009/10/kinetic-sculpture-at-bmw-museum).
- Split-flap: [Wikipedia, split-flap display](https://en.wikipedia.org/wiki/Split-flap_display);
  [LAb[au], SignalToNoise](https://meta.lab-au.com/projects/signaltonoise/texts/presentation).
- Zimoun: [Wikipedia](https://en.wikipedia.org/wiki/Zimoun); [KIKK 2025](https://2025.kikk.be/exhibitions/zimoun/).
- Kuramoto in art: [David Rueter, The Kuramoto Model (1000 Fireflies)](https://northern.lights.mn/projects/david-rueter/);
  [The Line, 2012](https://www.thelinemedia.com/devnews/kuramoto060612.aspx);
  [Bauhaus-Universität Weimar wiki](https://www.uni-weimar.de/kunst-und-gestaltung/wiki/Special:MobileDiff/125742).
- Chimera and defects: [Kundu et al., chimera states in 2D locally coupled networks](https://arxiv.org/pdf/1707.08443);
  [Liu and Uchida, spiral-wave chimeras](https://arxiv.org/html/2511.21058v2).
- Rooms and light: [UVA, Momentum](https://uva.co.uk/features/momentum-barbican-curve-gallery);
  [Olafur Eliasson, The Weather Project](https://olafureliasson.net/artwork/the-weather-project-2003);
  [Architecture Today, Asif Khan's pavilion](https://architecturetoday.co.uk/?p=72548);
  [Atlas Obscura, Vantablack Hyundai Pavilion](https://www.atlasobscura.com/places/vantablack-hyundai-pavilion).
- Specular anti-aliasing: [Kaplanyan et al., Filtering Distributions of Normals for Shading Antialiasing](https://research.nvidia.com/sites/default/files/pubs/2016-06_Filtering-Distributions-of/NDFFiltering.pdf);
  [Tokuyoshi and Kaplanyan, JCGT 2021](https://www.jcgt.org/published/0010/02/02/paper-lowres.pdf).
