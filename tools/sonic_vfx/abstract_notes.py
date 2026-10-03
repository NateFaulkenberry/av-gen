"""The one-page note of each Sonic Abstract prototype (04-brief-abstract-direction.md, step 3, and the owner's briefs
05-08 that made the set nine): what it is, what drives what, and what would expand it into more scenes, with the
review's honest verdict. `abstract.py notes` writes them to
the review folder as NN-<id>-note.md, beside the still, the clips and the generated modulation map.

The verdicts are written after looking at the stills and clips (the gate: would someone see a screenshot and think
"holy shit, that's cool"?), and revised when a prototype changes.
"""

NOTES = {
    "sacred-flight": {
        "working_title": "The Golden Passage",
        "language": "A flight down the axis of an endless luminous sculpture of sacred geometry: a procession of "
                    "colossal gates drawn in gold light (hexagram stars and six-circle rose windows, double rims, "
                    "pearls) strung on twelve helical rails, rifling as they come, towards a gold light at the "
                    "vanishing point against which the far gates stand dark. Indigo air, symmetric, deep.",
        "drives": [
            "The bass widens and narrows the whole passage; the enormous outer rings expand.",
            "The kick sends a wave of light from the camera down the passage to the far end.",
            "The snare blooms the gates (the stars and roses swell past the rim and ring back) and fires sparks.",
            "Mids turn the whole lattice; the hats glint the pearls; the highs stream sparks past the walls.",
            "MIDI: the twelve rails are the twelve pitch classes (C at the top, a clock), so a chord lights its own "
            "polygon of rails down the whole passage; the chord's size is a vermilion polygon in every rose's heart; "
            "the pitch colours the sun; held notes light the outer rings; the mod wheel tightens the helix.",
            "Structure: intensity doubles the gates (every gate both a star and a rose, a rim every 7 m).",
        ],
        "expand": ["a passage of Platonic solids", "a Metatron's-cube lattice flown between",
                   "a flower-of-life floor under a nave of rings", "a mandala well that is also a tunnel"],
        "verdict": ("Holy shit: yes. The flat mandala became a place: you are inside a cathedral of gold light that "
                    "rushes past and converges on a glowing core, every line on one twelve-fold grid. The chord "
                    "lighting its own polygon of rails down the whole passage is the clearest picture of harmony in "
                    "the set. Risk: in a dense passage the near gates' lines cross a lot; the symmetric camera is "
                    "hypnotic but less varied than the landscapes."),
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
        "verdict": ('Close to yes. It reads as a poster you can fly through: hidden-line ridges, a magenta triangle, '
                    "an outline circle, wireframe monoliths. The flight (the plain's shapes advancing), the beat "
                    'sweep and the beat-built shapes carry the energy. Not quite: the left foreground is empty black,'
                    ' and the accent colour lives on one object.'),
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
        "verdict": ("Charming rather than holy-shit. It's a complete, coherent toy world: inked, cel-banded and "
                    'pastel, with a sky-whale, a ringed planet, an island chain and creatures who hop and sing. It '
                    "reads as a game's key art and is the friendliest of the nine. Its ceiling is the genre: it "
                    'looks like a lovely indie game, not like something only Sonic Garden could make.'),
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
        "verdict": ('Close. Monumental colour frames in a luminous haze, twisted into a spiral and flowing at you: '
                    "Albers' squares in depth. As a still it's a flat pink wall with a spiral hole. It lives in "
                    'motion: the accordion on the bass, the colour jumps on the snare, the polygon order on chords. '
                    'Needs a second element (light, or a figure) for scale.'),
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
            "The radiolarians: the hats light their lattices, the treble thickens their lines, the bass swells them, "
            "the kick lights their cores.",
        ],
        "expand": ["a jellyfish cathedral", "a coral forest that branches per phrase", "a radiolarian sky",
                   "a seed bank that grows one plant per note"],
        "verdict": ('Nearly. A glowing coral lily-lantern with luminous stamens in macro focus, translucent jelly-'
                    "trees as soft backlight, spore bokeh, and three radiolarians (wire-line lattice organisms) "
                    "drifting through the depth of field. It's pretty and alive (it breathes, blooms, sways and "
                    "grows), but it's the closest of the set to familiar 'bioluminescent alien flora', and the jelly "
                    "doesn't read as jelly yet (no real translucency in the engine)."),
    },
    "digital-alpine": {
        "working_title": "Glass Caldera",
        "language": "A digital painting flown through: four rings of flat-faceted low-poly ranges round a caldera lake, "
                    "each paler than the last, a low sun behind the peaks, and a mirror lake that doubles everything "
                    "(every landform has a twin below the waterline; the sky is painted mirrored below the horizon, "
                    "broken by ripples and the sun's path). Violet, indigo, rose and peach; no lines, no neon.",
        "drives": [
            "AUDIO TOPOGRAPHY: the level raises waves that roll out from the island through every range and its "
            "reflection, as tall as the music is strong.",
            "Spectral depth: the bass lights the shore range, the low mids the main range, the mids the far range, "
            "the high mids the distant peaks; the highs scatter glints on the water and brighten the sun.",
            "The kick flares the low sun across every facet that faces it and drops rings of light on the water; the "
            "snare flashes the horizon in the sky and in the lake; the mids deepen the ripples; the bass brightens the "
            "sun's path on the water.",
            "MIDI: twelve lanterns round the shore are the pitch classes (with their reflections), as bright as the "
            "velocity; a bigger chord lays a brighter path of light on the water; the mod wheel brings the night (the "
            "sky and the lake darken, the sun leaves the facets, the lanterns come up).",
            "Structure: intensity raises the ranges as a piece builds.",
        ],
        "expand": ["a fjord at dawn", "an archipelago of faceted islands", "a crater lake at night with the "
                   "lanterns lit", "a waterfall of facets"],
        "verdict": ("Holy shit: yes, as a still: a low-poly sunset fjord with a perfect reflection and the sun's path "
                    "on the water, and, half a circuit later, coral alpenglow over deep blue water: two palettes, one "
                    "world. The audio is restrained by design (the brief asks for breathing, not bouncing), so the "
                    "topography waves read best in loud passages. Risk: the faceted lake view is the most "
                    "'wallpaper-like' of the set; it is beautiful rather than strange."),
    },
    "chromatic-topography": {
        "working_title": "Contour Valley",
        "language": "A landscape as a sculptural colour field: smooth sculpted hills coloured by their height in seven "
                    "hard bands (indigo, violet, magenta, coral, orange, gold, cream) ruled with contour lines, a "
                    "turquoise river glowing down a winding valley, teal and cobalt groves, cyan crystals on the "
                    "crests, cream haze under a cobalt sky.",
        "drives": [
            "The mids make the contour lines flow up the slopes; the beat pulses them; the highs brighten them.",
            "The bass raises the river up its banks and brightens it; the level makes the land breathe.",
            "The kick flashes the contour lines white across the land and surges the river; the snare blooms the "
            "groves' colours; the low mids light the round trees.",
            "MIDI: twelve families of crystals on the crests are the pitch classes; the chord widens the river's "
            "glow; the mod wheel sets the contour spacing (survey lines to broad terraces).",
            "Structure: intensity raises the land as a piece builds.",
        ],
        "expand": ["a delta of glowing rivers seen from above", "terraced islands in a colour-field sea",
                   "a night valley drawn in contour lines of light"],
        "verdict": ("Yes from above, close down in the valley. Distinct from everything else in the set: a map you "
                    "can fly, saturated and graphic, and the flowing contour lines are a new kind of audio response "
                    "(the music re-surveys the land). The glide's rise over the hills, the bands stacked to the "
                    "horizon, is the money shot; down on the river it reads as a soft painted canyon. Risk: the "
                    "river's flowing stripes are a little 'neon tube', and it is the most expensive of the nine live "
                    "(the land covers the frame)."),
    },
    "glitch-signal": {
        "working_title": "Pixel Canyon",
        "language": "A flight down a canyon made of a broken signal: walls and floor of thousands of pixel blocks lit "
                    "in a data pattern, scanlines, data ribbons, falling pixels, a white-hot horizon. Almost "
                    "monochrome at rest (cyan and white on blue-black); the music corrupts it, at three scales.",
        "drives": [
            "MACRO, the signature: a strong event (a hard kick at a moment of large spectral change, or a "
            "full-velocity note) stages a collapse -- RGB parts, the rows drift, the walls fragment into blocks, the "
            "frame is all but corrupted, a wave of colour runs through the camera -- and the world rebuilds in a NEW "
            "CONFIGURATION: turned about the flight, recoloured, re-patterned, re-staggered.",
            "MESO: snares tear the frame and shear the rows; onsets swap channels.",
            "MICRO: scanlines, a pixel of split and a flicker of blocks always; the hats flicker blocks; the highs "
            "deepen the scanlines and thicken the pixel rain.",
            "The bass widens the split, pushes the walls apart and flares the horizon; the kick sends a ring of "
            "distortion out of the horizon through the frame and flashes the lit pixels; the mids stream the data "
            "faster; the brightness lifts the lit pixels and the beat pulses them.",
            "MIDI: velocity is how hard a note glitches the frame; pitch lifts the fragments; held notes keep them "
            "lit; the mod wheel is corruption by hand.",
        ],
        "expand": ["a datamosh river", "a pixel-sorted waterfall", "a signal tower that collapses into a city of "
                   "blocks and rebuilds as a forest"],
        "verdict": ("Holy shit: yes, in motion. The canyon is a strong still, and the collapse is a real event: you "
                    "watch the world shatter into RGB blocks and come back turned and recoloured, and the next "
                    "configuration is a new place. Risk: a dense, loud passage can chain collapses (the engine has no "
                    "refractory period for a trigger), which reads as continuous corruption."),
    },
    "bit-ocean": {
        "working_title": "Bubble Reef",
        "language": "A lost underwater game level grown into 3D and swum through for ever: a ring round a deep basin "
                    "in three areas with their own palettes (sunny shallows, flower ruins, pipe deep), voxel floors, "
                    "kelp, coral and rocks, chunky columns, arches and pipes, a trail of coins, a house-sized shell, "
                    "fish schools, bubbles, caustics, two-band toon light and thin navy outlines.",
        "drives": [
            "THE LIVING LEVEL: the bass bends every kelp plant at once and pulses the coral; a melody rings a row of "
            "twelve bell flowers note by note (one row per area: a xylophone); a chord blooms every brain coral; "
            "the snare bursts bubbles round you and turns the schools; the kick flashes the coins and gems and sends "
            "a ring rippling out through the water.",
            "The mids speed the fish and thicken the bubble streams; the highs swarm sparkles; the beat pumps the "
            "jellies and the caustics; the brightness clears the water.",
            "MIDI: velocity sizes a note's bubble burst; held notes light the kelp; the mod wheel takes the level "
            "deeper (darker, thicker water).",
            "Structure: intensity grows the coral.",
        ],
        "expand": ["a sunken castle level", "a night level of glowing jellies", "a boss chamber with a giant friendly "
                   "fish", "a lava level in the same grammar"],
        "verdict": ("Charming, not yet stunning. It reads instantly as a game water level: the outlined checker "
                    "floors, the coin trail through an arch, voxel kelp, brain coral, bubbles and light shafts, and "
                    "the three palettes give the lap a story. The look round (the lane dressed, outlines block by "
                    "block, clearer water, deeper toon shadows) took it from flat to crisp. What it lacks for the gate "
                    "is a hero: one striking thing per area (a giant fish, a sunken castle) instead of a lap of "
                    "even dressing."),
    },
}
