"""The one-page note of each Sonic Abstract prototype (04-brief-abstract-direction.md, step 3): what it is, what drives
what, and what would expand it into more scenes, with the review's honest verdict. `abstract.py notes` writes them to
the review folder as NN-<id>-note.md, beside the still, the clips and the generated modulation map.

The verdicts are written after looking at the stills and clips (the gate: would someone see a screenshot and think
"holy shit, that's cool"?), and revised when a prototype changes.
"""

NOTES = {
    "sacred-geometry": {
        "working_title": "The Armillary",
        "language": "A giant living geometric diagram: gold and ivory linework on ultramarine black, every element a "
                    "circle or a regular polygon about one centre, nested layers that are also a well receding into "
                    "the depth. Self-luminous lines, bloom, one vermilion accent.",
        "drives": [
            "Bass breathes the diagram radially, the inner layers most, like a pulse from the centre.",
            "The kick snaps the armillary rings open about their diameters, a ripple inward, and a loose spring rings "
            "them back flat.",
            "Mids speed the differential rotation (ring k gains k times the turn), so a busy middle spins the "
            "pattern into new alignments.",
            "Each spectral band lights its own layer, the seed (bass) out to the crown (treble).",
            "The snare flashes the star and fires sparks off the crown; the hats run beads along the rings.",
            "MIDI: the twelve pitch classes are a clock of diamonds and spokes, so a chord draws its own polygon; the "
            "number of notes sets the sides of a polygon in the heart; held notes keep the armillary open; the mod "
            "wheel opens it into a sphere by hand.",
            "Structure: intensity doubles the crown's petals (24 to 48) and deepens the well.",
        ],
        "expand": ["Kepler's nested Platonic solids", "a garden of small armillaries on a dark mirror",
                   "a flower-of-life tessellation flown over", "Islamic star tilings as a floor that the chords "
                   "rebuild", "a tunnel of mandalas for a build-up"],
        "verdict": "",
    },
    "neon-vector": {
        "working_title": "Pulsar Plain",
        "language": "Graphic design you can fly through: a plain of hidden-line ridgelines (the Unknown Pleasures plot "
                    "laid flat), a colossal outline circle floating in a black sky, flat blocks of one bold accent "
                    "colour. White line on black plus one colour at a time.",
        "drives": [
            "Bass swells the near rows under the camera; the kick heaves the whole plain and lurches it forward.",
            "The bands lie across the landscape in depth: low mids and mids swell the middle rows, high mids the far "
            "rows (a spectrum as terrain, not as bars).",
            "Highs and hats jitter the lines finely.",
            "The snare builds: three flat shapes snap into existence in sequence and shrink away; the lines flash.",
            "Mids breathe and turn the horizon circle; the centroid sets the section's accent colour (the white "
            "lines stay white).",
            "MIDI: a note raises a mountain across the plain at its pitch's place, as tall as its velocity, held while "
            "the note is held; a chord turns the horizon circle into a polygon of as many sides as notes.",
            "Structure: intensity makes the plain denser (more rows) and the peaks taller.",
        ],
        "expand": ["a vector city drawn only in outlines (with the engineer's wireframe lines)",
                   "an oscilloscope room where Lissajous figures are the architecture",
                   "a Bauhaus field of flat primitives", "a line-drawn ocean"],
        "verdict": "",
    },
    "cel-dream": {
        "working_title": "Candy Archipelago",
        "language": "A toy diorama in a candy sky: low-poly forms, two flat shade bands over lavender shadows (cel "
                    "lighting), plum ink outlines, pastel colours pushed to candy. Round creatures, lollipop trees, a "
                    "giant flower, a ring that leads nowhere, a waterfall into the void.",
        "drives": [
            "The kick makes the creatures hop, a ripple left to right, landing on a spring; bass squashes and "
            "stretches them and bobs the camera.",
            "The snare flicks the flower's petals and throws confetti; the hats sparkle round it.",
            "Mids sway the clouds and turn the creatures' heads; the centroid moves the time of day.",
            "Sustain blooms the giant flower; the beat bobs everyone.",
            "MIDI: the creature at the note's register sings (its mouth opens, it lifts by the velocity); a chord "
            "blooms as many lawn flowers as notes; a held note raises a rainbow; the mod wheel turns day to night.",
            "Structure: intensity brings two more creatures onto the island.",
        ],
        "expand": ["a candy village of creature houses", "an underwater cel reef", "a sky-train between islands",
                   "a creature choir, one creature per voice"],
        "verdict": "",
    },
    "color-geometry": {
        "working_title": "Chromatic Corridor",
        "language": "Space is the subject: a procession of monumental portals of pure flat colour that flows toward "
                    "you forever through coloured haze, toward a blinding light. The colour lives in the space (warm "
                    "near, cool far), so the frames travel through it.",
        "drives": [
            "Bass breathes the corridor like an accordion (its spacing and its travel scaled together round the "
            "viewer) and thickens the haze.",
            "The kick punches a zoom toward the light; the snare jumps the whole colour sequence a step round the "
            "wheel and leaves it there.",
            "Mids wind the corridor's twist; highs trace the frames' edges in light; flux tumbles the floating "
            "cards; the beat surges it forward.",
            "MIDI: each note recolours the space (the pitch sets the hue's phase); a chord sets the portal's polygon "
            "(a triangle, square, hexagon); velocity flares the light; the mod wheel thickens the frames.",
        ],
        "expand": ["nested colour rooms (Turrell's Ganzfeld)", "a colour-field staircase", "a sky of floating "
                   "slabs", "a portal sequence over still water"],
        "verdict": "",
    },
    "organic-garden": {
        "working_title": "Lantern Reef",
        "language": "A macro photograph of an alien meadow at twilight: translucent organisms built from a handful of "
                    "primitives repeated radially (Haeckel), glowing from their rims, in shallow focus with spore "
                    "bokeh. Coral, peach and lime light in teal and violet dark.",
        "drives": [
            "Bass breathes the garden: the lantern swells, the jelly-trees' bells contract and inflate like swimming.",
            "The kick blooms the lantern open (its petals lift from their droop) and a spore puff leaves its heart.",
            "The snare puffs the mushrooms' spores; the hats shimmer the vines and sparkle the spores.",
            "Mids are the current (the tentacles and tendrils sway harder); the centroid turns the glow from coral to "
            "mint and sharpens the petals.",
            "Sustain grows the lantern taller; the beat pulses the jelly-trees.",
            "MIDI: six flowers along the meadow are six voices (a held note opens its flower); a chord multiplies the "
            "lantern's petals; a note's release drops petals, more for a long note; the mod wheel turns the season.",
            "Structure: intensity grows the mushroom cluster and the jelly-trees' tendrils.",
        ],
        "expand": ["a jellyfish cathedral", "a coral forest that branches per phrase", "a radiolarian sky",
                   "a seed bank that grows one plant per note"],
        "verdict": "",
    },
    "particle-world": {
        "working_title": "Galaxy Engine",
        "language": "Particles are the forms: a vast spiral of points whose arms are rivers of light flowing into a "
                    "white-hot core, dust curtains, a stream arcing over the camera; feedback turns points into silk.",
        "drives": [
            "Bass is the pull: the arms contract toward the core and bloom out again; the core flares.",
            "The kick explodes the core (a burst and a shock ring) and the arms reform round it.",
            "The snare runs a flare along an arm with a split; the hats glitter the disc.",
            "Mids turn the galaxy faster; flux frays the arms with turbulence; the centroid shifts the colour and "
            "grows the points.",
            "MIDI: each note launches a comet from the rim at its pitch's angle; a chord lights more arms (up to six); "
            "held notes thicken the halo; the mod wheel tilts the galaxy.",
            "Structure: intensity multiplies the points.",
        ],
        "expand": ["a ring vortex (a smoke ring of light)", "a river of light over a dark sea", "a particle flower "
                   "that blooms per chord", "a murmuration of dark points against a sunset"],
        "verdict": "",
    },
    "impossible-architecture": {
        "working_title": "Relativity Court",
        "language": "Escher's Relativity built like Manifold Garden: a staircase module in three gravities repeated "
                    "forever in every direction, rooms inside rooms at its heart, in de Chirico's light (a low "
                    "golden sun, long hard shadows, warm flat colour, ink edges).",
        "drives": [
            "Bass breathes the lattice apart and together round the viewer.",
            "The kick slides a flight two steps; the snare folds another flight over into a new gravity and swings it "
            "back.",
            "Mids sweep the sun, so the long shadows move; highs sharpen the ink; the centroid moves the sky and "
            "haze from teal dusk to gold noon.",
            "MIDI: a note lights the doorways of the flight at its register; a chord twists the nested rooms; held "
            "notes warm the sun; the mod wheel rolls the world.",
            "Structure: intensity nests more rooms inside the room.",
        ],
        "expand": ["Penrose stairs in an isometric diorama", "a monastery of folding staircases", "endless arcades at "
                   "noon", "rooms inside rooms as a zoom"],
        "verdict": "",
    },
    "cinematic-void": {
        "working_title": "The Gate",
        "language": "One colossal object, one horizon, one light: a segmented ring 200 m across standing on a mirror "
                    "plane with a low sun burning inside it, amber haze against teal, one tiny figure, letterboxed "
                    "2.39:1, a majestic slow push.",
        "drives": [
            "Bass breathes the haze and swells the sun's glow; sustain brightens the sun.",
            "The kick sends a pulse of light round the ring's segments; the snare bursts dust in the light; the "
            "hats glitter it.",
            "Mids turn the ring slowly; the centroid grades the sky from amber to rose and teal; the beat breathes "
            "the ring's radius.",
            "MIDI: the segment at the note's angle lights (a beacon round the ring); a chord multiplies the segments; "
            "held notes drift them outward; the mod wheel raises the sun inside the ring.",
            "Structure: intensity parts the ring (its segments move apart as the piece builds).",
        ],
        "expand": ["a glowing sphere over an ocean", "a field of suspended monoliths", "a distant geometric city on "
                   "the horizon", "an eclipse"],
        "verdict": "",
    },
}
