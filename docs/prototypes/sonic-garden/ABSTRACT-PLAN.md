# Sonic Abstract: nine visual languages (the plan)

The art agent, 2026-10-02/03, under `04-brief-abstract-direction.md` as amended by the owner's four later briefs
(`05-brief-direction-correction.md`, `06-brief-glitch-signal.md`, `07-brief-digital-alpine.md`,
`08-brief-8bit-ocean.md`; the owner's words govern). This is the plan written before each build, with an "as built" note
where the prototype differs from it. Progress and resume notes: `PROGRESS-abstract.md`.

**Its own project** (the owner's decision, 2026-10-02): the prototypes are "Sonic Abstract" (a working title), separate
from Sonic Garden, whose live scene and Sonic VFX set stay as they are. The projects live in `examples/sonic-abstract/`
(generated meshes in `examples/sonic-abstract/meshes/`), listed under their own index category, "Sonic Abstract". They
are built by `tools/sonic_vfx/abstract.py build` from the modules in `tools/sonic_vfx/scenes/` named in
`scenes.ABSTRACT`.

**The set of nine** (the owner's order):
1. Sacred Geometry Flight (replaces the flat Sacred Geometry Garden)
2. Neon Vector World
3. Cel-Shaded Dream World
4. Infinite Color Geometry
5. Organic Digital Garden
6. Digital Alpine (replaces Particle/VFX World)
7. Chromatic Topography (new)
8. Glitch Signal (new)
9. 8-Bit Ocean (new)

Removed outright, with no replacement in their spirit (05): Impossible Architecture and Abstract Cinematic Void. Cut
(07): Particle/VFX World. Their modules and projects are deleted; the last commit that has them is `fc6f8030^`.

## What every prototype obeys

1. **Simple things arranged in extremely cool ways.** Primitives (rings, slabs, tubes, spheres, lines, particles) and
   meshes generated from code (facets, voxels, heightfields), no scanned assets, no textures, no realism. Each prototype
   is a visual GRAMMAR: a short list of rules that the scene follows everywhere, so it can grow into more scenes.
2. **The still frame works frozen.** The silent render (`variant.make_silent`: no routes, no interpret sources, no
   triggers, no audio) must already pass the gate: "would someone see this screenshot and think 'holy shit, that's
   cool'?" When it does not, the design is changed, not the polish.
3. **Nine artists, not nine presets.** Each prototype has its own palette family, light model, line or surface
   vocabulary, camera language and motion character (table below). No two read as variations of one look.
4. **Instruments, not visualizers.** Audio drives the structure of the artwork (counts, orders, spacing, configuration),
   not only its brightness. Every audio dimension has a different job in each prototype. Post effects are punctuation
   (except in Glitch Signal, where the post vocabulary is the subject).
5. **Live first.** Everything is built on the live bus (`response.*`, `notes.*`, `audio.*` bands, `beat.*`), so a
   keyboard and a synth play it exactly as a recording does. Each prototype must hold 60 fps live at the editor's
   adaptive scale.
6. **Places, not objects (05).** "No random shapes spinning around": every prototype is an environment with near, mid and
   far, a direction of travel, and a camera moving through it.

## How the nine differ (by construction)

| # | prototype | the artist it could be | palette family | surface and line vocabulary | light model | camera language | motion character |
|---|---|---|---|---|---|---|---|
| 1 | Sacred Geometry Flight: *The Golden Passage* | a sacred-geometry illuminator building a cathedral of light | gold and ivory on indigo, vermilion only for chords | fine emissive lines, beads; gates of hexagrams and six-circle roses on helical rails | self-luminous lines, a gold light at the vanishing point, the far gates dark against it | down the axis of a passage, symmetric, banking | a seamless flight; the lattice rifling as it comes |
| 2 | Neon Vector World: *Pulsar Plain* | a motion designer (Unknown Pleasures, oscilloscope music, Swiss posters) | black, white line, ONE flat accent colour per section | hidden-line ridgelines, outline primitives, flat colour blocks | none: lines and blocks are their own light | low, fast, forward flight over the lines | rippling lines, beat-built blocks, speed |
| 3 | Cel-Shaded Dream World: *Candy Archipelago* | an indie game art director (Wind Waker, A Short Hike, Monument Valley) | pastel candy daylight: peach sky, mint, lilac, butter; plum outlines | low-poly toy forms, 2-3 flat shade bands, outlines | one hard key light, tinted graphic shadows | a slow orbit round a toy diorama | bouncy, squash and stretch, creatures that dance |
| 4 | Infinite Color Geometry: *Chromatic Corridor* | a light-and-space installation artist (Turrell, Rothko, Cruz-Diez) | full-frame saturated gradient: vermilion, magenta, violet, cobalt, teal | monumental planes and portals of pure colour, no lines | coloured fog, a blinding light at the end, gradients | gliding forward through it | a corridor that breathes, twists and recolours |
| 5 | Organic Digital Garden: *Lantern Reef* | a generative biologist (Haeckel, teamLab) shot by a macro photographer | deep teal and emerald dark, coral, peach and lime glow | translucent jelly (fresnel rims), tubes, radial petals, polka dots, wire-line radiolarians | glow from within, soft bokeh, shallow depth of field | macro: slow lateral slide, focus pulls | breathing, blooming, swaying, growing |
| 6 | Digital Alpine: *Glass Caldera* | a digital landscape painter (low-poly art, Firewatch's layered ranges) | violet and indigo ranges, a rose and peach sky, deep blue water | flat-faceted low-poly ranges and their mirror twins; no lines | painted per facet (sun, sky, shadow), aerial haze in the sky's own colour | a low glide round a lake, banking | slow; waves of topography rolling through the ranges |
| 7 | Chromatic Topography: *Contour Valley* | a cartographer turned colour-field painter (hypsometric maps, Rothko) | seven hard warm bands by height, a turquoise river, cobalt sky | smooth sculpted hills, contour lines, cone and lollipop groves, crystals | flat light from the left, colour by height, cream haze | a glide down a winding valley that rises over the hills | contour lines flowing up the slopes |
| 8 | Glitch Signal: *Pixel Canyon* | a glitch artist (Ikeda's data, Menkman's corruption) | cyan and white on blue-black at rest; magenta, acid green, orange in corruption | a canyon of pixel blocks, scanlines, data ribbons, falling pixels | self-luminous data, a white-hot horizon | fast and straight down the canyon; turned after each collapse | stable, then a staged collapse and a rebuild in a new configuration |
| 9 | 8-Bit Ocean: *Bubble Reef* | a pixel artist who grew up on underwater game levels | three area palettes (blue, cyan, yellow / purple, pink, turquoise / deep blue, orange, green) | voxel floors, rocks, kelp and coral; chunky low-segment ruins, pipes, coins; navy outlines | two-band toon light from the surface, blue depth, caustics | a playful swim round a ring-shaped level | swaying, bobbing, schools circling; the level playing along |

Signals named below (all on the live bus): `response.bass`, `response.kick|snare|hat|low|onset` (events, with `...Env`
envelopes), `response.sustain`, `response.flux`, `response.melodic`, `response.intensity` (12 s), `response.level`; bands
`audio.bass|lowMid|mid|highMid|treble`, centroid `audio.spectralCentroid` and the timbre `sonic.brightness` (medium) and
`sonic.brightness.slow`; tempo `beat.phase|pulse|bar`, `beat.bpm`; MIDI `notes.lastPitch|lastVelocity|interval|
polyphony|chord|tension|held|density`, the voice slots `notes.voice.<0..7>.{held,velocity,pitch}`, the pitch-class lanes
`notes.class.<0..11>`, `response.note|noteEnv`, `notes.release`; the mod wheel (CC 1) through the project's control map.
`[S]` marks a STRUCTURAL target: a count, order, spacing, depth or configuration that rebuilds the artwork.

---

## 1. Sacred Geometry Flight: *The Golden Passage*

**Grammar (05).** One module, the pair of bays, repeated along the flight axis: a STAR gate (a hexagram inscribed in a
double rim, its inner circle, twelve spokes, twelve beads) and 14 m on a ROSE gate (six tangent circles of radius R/3
inside the rim round an open heart, the rose window's tracery, twelve beads). Every element sits on one twelve-fold
radial grid: twelve helical rails run the whole passage through the gates' bead points. A world twist about the axis
turns the grid with depth, so the rails are helices and the gates, travelling towards the camera along them, rifle as
they come. The flight is a seamless sawtooth (the gates advance one pair of bays and jump back by it, which leaves every
position and rotation where it was). Outside, enormous rings stand in the haze; at the vanishing point burns a gold light
against which the far gates stand dark. Coherent, symmetric, deep; no shape that is not on the grid.

**Hero frame.** Down the axis: the nearest star gate's rim and pearls sweep the frame's edge, the hexagram's lines cross
the frame, the rose gates' six circles nest inside one another receding, the rails spiral to the centre, and the
centre is a gold glow with a dense filigree of far gates in front of it. Indigo air.

**Palette.** Air `#0f0c33` to ink `#06061c`; lines gold `#f2b84b`, pale gold `#ffd98a`, ivory `#fff1d0`; the sun
`#ffb54a`; vermilion `#ff4a24` only for the chord's polygon.

**Technique.** Thin unlit tori (low `majorSegments` = polygons) on linear distributions; the twelve spokes, the twelve
beads and the rose's six circles are templates composed onto the gates by reference (ADR-029); one world twist
deformer shared by every part; one material program (`sgfWave`: the line's own emission plus a band of light whose depth
the kick carries); the light at the end a fogged radial disc; analytic fog; depth of field; bloom.

**Camera.** On the axis (the symmetry is the point), drifting a metre about it, banking ±10 degrees over 36 s; 20 mm.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | `[S]` the passage widens and narrows (every part scaled about the axis); the enormous outer rings expand |
| kick | `response.kick` | light: a wave of light runs from the camera to the far end (the band in `sgfWave`); a radial burst; a lens punch |
| snare | `response.snare` | geometry: the stars' triangles and the roses' circles swell past the rim and ring back (a bloom), and flash; sparks |
| hat | `response.hat` | the beads glint |
| highs | `audio.treble`, `audio.highMid` | sparks stream past the walls; the inner circles brighten |
| mids | `audio.mid` | geometry: the whole lattice turns (the twist's phase, integrated) |
| centroid | `sonic.brightness` | colour: gold towards ivory |
| tempo | `beat.pulse` | the rails pulse |
| intensity | `response.intensity` | `[S]` the gates double: a rim every 7 m, and every gate both a star and a rose |
| pitch class | `notes.class.<k>` | the twelve rails are the twelve pitch classes (C at the top, clockwise): a chord lights its own polygon of rails down the whole passage |
| chord | `notes.polyphony` | `[S]` every rose's open heart becomes a vermilion polygon with as many sides as notes sounding |
| pitch | `notes.lastPitch` | the sun's colour, deep orange to white gold |
| velocity | `notes.lastVelocity` | how hard a note flares the gates' circles |
| held | `notes.held` | the enormous outer rings glow |
| mod wheel | CC 1 | `[S]` the helix tightens (the twist's rate) |

**Structural parameters.** Gate spacing (count), the passage's width, the helix's pitch, the chord polygon's order.

**As built (2026-10-02).** As planned. Found on the way: a template must stay VISIBLE (a hidden one composes nothing)
and untransformed (its distribution transform reaches the copies), so the three templates stand in the camera's own
plane where the near plane clips them; the sky's sun glows only above its horizon, so the light at the end is geometry.

**Expansion.** A passage of Platonic solids; a Metatron's-cube lattice flown between; a flower-of-life floor under a nave
of rings; a mandala well that is also a tunnel.

---

## 2. Neon Vector World: *Pulsar Plain*

**Grammar.** Only lines and flat planes. The ground is a field of parallel ridgelines, each a glowing line on top of a
black fin that hides the lines behind it (the hidden-line look of the Unknown Pleasures pulsar plot). Above it stand
outline primitives (great circles, wireframe spheres, triangles) and flat blocks of one bold colour. The scene is
white lines on black plus exactly one accent colour at a time; the accent changes by section.

**Hero frame.** A low camera skims over sixty ridgelines that fill the lower two thirds of the frame and recede to a
horizon. They ripple; near the centre rises a ridge of tall spiky peaks. On the horizon sits a colossal outline circle
of seven concentric fine rings, cut by the ridgelines in front of it. In the mid-distance a huge flat magenta triangle
stands at an angle, half hidden by the ridges, with a hairline white outline. The sky is pure black with one fine
horizon line. The effect is a poster that you can fly through.

**Palette.** Black `#000000`, line white `#f4f4f0`, accents `#ff2e88` (magenta), `#ffe500` (acid yellow) and `#3a3dff`
(ultramarine), one at a time.

**Technique.** Each ridge is a thin `tube` along a line (200 segments), unlit white, on a black unlit fin of the same
curve. Both are deformed by the same world-space `sine` and `noise` deformers, so a fin's top follows its line exactly.
A `linear` distribution along Z gives the line count. Row groups (near, middle, far) are separate nodes, so different
bands own different depths. Outline circles are thin tori. Wireframes use the engineer's mesh edge lines when they land;
until then thin tori and the SDF line look. Colour blocks are flat unlit slabs whose scale pops on hits.

**Camera.** Low over the field (1.5 m), flying forward. The speed is integrated from tempo and energy. A slow
side-to-side yaw carves the flight.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | geometry: the near rows' ridge amplitude (the plain swells under the camera) |
| bands | `audio.lowMid`, `audio.mid`, `audio.highMid` | geometry: the middle rows and the far rows each swell with their own band, so the spectrum lies across the landscape in depth, not as a bar graph |
| highs | `audio.treble`, `response.hat` | geometry: fine high-frequency jitter on the lines (noise amount); VFX: sparks running along the line tops |
| kick | `response.kick` | geometry: a rolling wave travels down the field toward the camera; camera: a forward lurch |
| snare | `response.snare` | `[S]` beat-built construction: a colour block snaps into existence (scale from 0) and the lines flash the accent |
| onset | `response.onset` | colour: the hairline outlines flash |
| mids | `audio.mid` | geometry: the horizon circle's rings turn and breathe their radii |
| centroid | `sonic.brightness.slow` | colour: which accent owns the section (dark timbre ultramarine, middle magenta, bright acid yellow) |
| tempo | `beat.bpm`, `beat.phase` | camera: flight speed; geometry: blocks are built on the beat |
| intensity | `response.intensity` | `[S]` ridgeline count (40 to 90 rows) and `[S]` the horizon circle's ring count |
| MIDI notes | `notes.voice.<0..3>.pitch`, `.velocity` | geometry: each held voice raises a peak in the ridges at its pitch's x position (low left, high right); velocity sets its height; a chord raises a mountain range |
| MIDI chords | `notes.polyphony` | `[S]` the wireframe sphere's segment count |
| sustained notes | `notes.held` | geometry: the peaks hold; colour: the accent block grows |
| mod wheel | CC 1 | geometry: the ridges' wavelength (smooth swells to jagged peaks) |

**Structural parameters.** Row count, row spacing, the horizon's ring count, wireframe segments, and the number of
blocks built.

**As built (2026-10-02).** As planned, plus:
- the beat sweep (a band of light down the plain once per beat, a material program);
- two rows of wireframe monoliths (ADR-1073 wire lines, lines only) that the fins hide;
- three flat shapes the snare builds in the sky.

The terrain's flight is a noise FIELD travelling toward the camera through static rows. The far rows open out with
distance, so they don't merge into a band.

**Expansion.** A vector city drawn only in outlines; an oscilloscope room (Lissajous figures as architecture); a
flat-colour Bauhaus field of primitives; a line-drawn ocean.

---

## 3. Cel-Shaded Dream World: *Candy Archipelago*

**Grammar.** Low-poly toy forms with soft rounded silhouettes. Two or three flat shade bands. Hard graphic shadows
tinted violet. Dark plum outlines. Pastel colours pushed to candy saturation. Every object is made of a few primitives
(lollipop trees, cone trees, mushroom houses, round creatures) and everything floats on islands in a candy sky.

**Hero frame.** A floating island hovers left of centre in a peach-to-pink gradient sky with big flat pastel clouds.
Its top is a mint lawn. It holds two lollipop trees and one giant five-petal flower taller than the trees. Three round
creatures stand in a row and look at the camera: lilac, butter and coral bodies, white eyes with black pupils, tiny
feet. There is also a little stone arch that leads nowhere. A waterfall of pale blue drops off the island's edge into
the void. Smaller islands float behind at a smaller scale. A hard key light from the upper left throws violet shadows,
and everything is outlined in plum.

**Palette.** Sky `#ffd2b0` to `#ff9ec4`. Lawn `#8ff0c0`. Creatures `#c7a6ff`, `#ffe27a`, `#ff8a7a`. Clouds `#fff4f8`.
Outline `#3d1d4f`. Shadow tint `#7a5ab8`.

**Technique.** Lit meshes in `environment.stylized` with banded material programs (a `gradient` of the normal against a
fixed light direction, `threshold` into bands, a 3-stop `ramp`). Creatures are small SDFs (smooth unions) with the line
look, so they have outlines and squash and stretch through node parameters. Toon lighting and mesh outlines come from the
engineer (the cel pass and the outline pass) when they land. The island is a lathe with a faceted rim. The waterfall is
particles.

**Camera.** A slow 40-degree arc round the diorama from slightly above (toy-box view, 35 mm), with a gentle bob on the
bass.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | geometry: the creatures squash and stretch; the island bobs; the trees sway |
| kick | `response.kick` | geometry: the creatures hop (a jump with a spring landing); the lollipops bounce |
| snare | `response.snare` | geometry: the flower's petals flick open; VFX: a confetti puff |
| hats | `response.hat`, `audio.treble` | VFX: sparkles twinkle round the flower; geometry: grass tufts shiver |
| mids | `audio.mid` | geometry: the clouds drift faster; the creatures' heads turn |
| centroid | `sonic.brightness.slow` | colour and light: the time of day (bright timbre is noon pastel, dark timbre is lavender dusk) through the sky gradient and the key light's colour |
| sustain | `response.sustain` | geometry: the giant flower blooms (petal angle); the waterfall's flow |
| tempo | `beat.phase` | geometry: the creatures bob on the beat |
| intensity | `response.intensity` | `[S]` the number of creatures (3 to 7) and `[S]` the number of floating islands |
| MIDI notes | `notes.voice.<0..2>`, `notes.lastPitch` | geometry: each creature is a voice; a held note opens its mouth and lifts it; pitch picks which creature sings (low left, high right) |
| velocity | `notes.lastVelocity` | geometry: how high the singer jumps |
| chords | `notes.polyphony` | `[S]` how many flowers bloom on the lawn |
| sustained notes | `notes.held` | VFX: a rainbow arch fades in over the island |
| mod wheel | CC 1 | colour: day to night |

**Structural parameters.** Creature count, flower count, petal count, tree count, island count, and cloud count.

**As built (2026-10-02).** Cel lighting (ADR-1071) and the plum outline (ADR-1072) on every surface. The frame was
recomposed after review into a wide diorama: the hero island, a chain of five islands fading into pink haze, a ringed
pastel planet, a moon, a sea of clouds. The creatures are parented rigs (node scale squashes the whole creature). The
two extra creatures wait under the lawn and pop up.

**Expansion.** A candy village; an underwater cel reef; a sky-train between islands; creature choirs.

---

## 4. Infinite Color Geometry: *Chromatic Corridor*

**Grammar.** Space is made of large planes of pure, flat, saturated colour, with no lines and no texture. Colour comes
from the planes, the coloured fog between them and the light at the end. The architecture is one module (a portal
frame) repeated forever along a path, each repetition a step further round a colour wheel. Floating slabs and ribbons
cross the space at angles.

**Hero frame.** You stand inside a procession of monumental square portals that recedes into a twisting corridor, each
frame turned 6 degrees from the last. The near frames are vermilion, then magenta, violet, cobalt and teal; coloured
haze between them softens each plane into the next. At the vanishing point a blinding white-gold light floods through
the last frames and blooms. Thin coloured slabs hover at angles in the space between frames like cards. The floor is one
deep colour plane that fades into the fog.

**Palette.** A cosine palette over depth: `#ff3b1f` vermilion, `#ff2d8a` magenta, `#8a2be2` violet, `#1f4fff` cobalt,
`#00c2b8` teal. Light `#fff3d4`.

**Technique.** One compiled SDF: a portal frame (a box minus a box) under `repeat` along the corridor axis
(infinite), `twist` about the axis, and `polarRepeat` for the frame's polygon order. A material program colours each
fragment from a cosine palette of its depth along the corridor plus a phase, so every portal gets its own hue and colour
can flow along the corridor. Volumetric coloured fog, a strong point light at the end, and bloom. Floating slabs are
procedural boxes.

**Camera.** Gliding forward through the corridor. The speed is integrated from energy; there is a slow lateral sway and
the view looks slightly off-axis.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | `[S]` the portal spacing (`repeat` size): the corridor compresses and expands like an accordion; VFX: the fog thickens |
| kick | `response.kick` | VFX: a zoom punch (radial blur, field of view); light: the frames flash toward white |
| snare | `response.snare` | colour: a palette jump (the whole sequence steps a fifth of a turn) |
| highs | `audio.treble`, `response.hat` | light: a fine edge glow traces the frames; VFX: glints on the slabs |
| mids | `audio.mid` | geometry: the corridor's twist rate (it winds and unwinds) |
| centroid | `sonic.brightness.slow` | colour: the palette's temperature range (warm sequence for a dark timbre, cool for a bright one) |
| flux | `response.flux` | geometry: the floating slabs' tumble |
| tempo | `beat.phase`, `beat.bpm` | camera: glide speed; colour: the palette's phase advances on the beat |
| intensity | `response.intensity` | light: the end light's power; VFX: the bloom |
| MIDI notes | `notes.lastPitch`, `response.note` | colour: each note recolours the corridor (the pitch sets the palette's phase) |
| MIDI chords | `notes.polyphony` | `[S]` the portal's polygon order (`polarRepeat` count): square, hexagon, octagon |
| velocity | `notes.lastVelocity` | light: how hard the end light flares |
| sustained notes | `notes.held` | VFX: the fog glows from within |
| mod wheel | CC 1 | `[S]` the frame thickness, from hairline to monumental |

**Structural parameters.** Portal spacing, polygon order, twist rate, frame thickness, and slab count.

**As built (2026-10-02).** The frames became monumental (7 m wide, mitred in the polygon's wedges) in a luminous
cream haze that the far frames dissolve into: Albers' squares in depth rather than outlines. Colour is an OKLCH hue
rotation of vermilion by depth (one program); the travel is a sawtooth of one spacing; the accordion scales the spacing
and the travel together.

**Look round (2026-10-03).** The first still failed the gate: a 24 mm lens at the corridor's mouth saw one pink frame
with a small spiral in it, the haze washed every colour to pastel, and the far opening showed the sky's dark ground
hemisphere as a grey box. Now: an 85 mm lens down the axis from just inside, so the frames 20 to 110 m away stack into
one spiral of the whole colour wheel round the light; the haze a third as thick; the colour's chroma pushed (a
`saturate` op after the hue rotation) and tonemap 4 (clamp) to keep it flat and pure; a uniform sky; and the light at
the end as geometry (an emissive sphere the kick, the notes and the intensity flare). The kick's zoom punch is 10 mm at
the long lens. The march stops at the light (112 m, 72 steps): the open axis is where a long lens sends its rays.

**Expansion.** Nested colour rooms (Turrell's Ganzfeld); a colour-field staircase; a sky of floating slabs; a portal
sequence over water.

---

## 5. Organic Digital Garden: *Lantern Reef*

**Grammar.** Organisms built from a handful of primitives, repeated radially as Haeckel drew them. Petals are elongated
spheres on a ring, stalks and tentacles are tapering tubes, bells are hemispheres, gills are rings, and caps carry polka
dots. Everything glows from within at its rim (fresnel), so it reads as translucent jelly. Seen as macro photography:
shallow depth of field and soft bokeh spores.

**Hero frame.** A macro view into a twilight alien meadow. In focus on the left third rises a tall lantern flower: a
curving stalk topped by a bell of 12 translucent coral petals, half open round a glowing peach core. Around it curl
tentacle vines and a cluster of polka-dot mushrooms with lime caps. In the foreground, out of focus, are big dark leaves
and glowing spore bokeh. Behind, softly blurred, stand giant jelly-trees: umbrella bells trailing long tendrils. The
background is a deep teal to violet gradient.

**Palette.** Dark `#031a1c`, `#06302f`, violet `#2a1648`. Glows: coral `#ff6f61`, peach `#ffb38a`, lime `#c6ff5e`,
pale mint `#bfffe8`.

**Technique.** Procedural spheres, tubes and tori with emissive fresnel programs. Tentacles are tubes with local
`sine` and `noise` deformers. Petals use a radial distribution and hierarchy. A polka-dot program uses thresholded
`voronoi` on object position. `post/dof` gives the macro focus. Spores are additive particles.

**Camera.** A macro slide: a slow lateral dolly at about 1 m from the flower, with the focus pulled from the foreground
to the flower over 20 s, and a breathing focus on the kick.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | geometry: the garden breathes; the jelly bells contract and inflate; the bells swell |
| kick | `response.kick` | geometry: the flower blooms open (petal ring radius and tilt) and closes again; VFX: a spore puff from its core |
| snare | `response.snare` | VFX: the mushrooms puff spores; light: their dots flash |
| highs | `audio.treble`, `response.hat` | geometry: cilia shimmer (fine noise on the tentacles); VFX: spores sparkle |
| mids | `audio.mid` | geometry: the sway (a current through the meadow): the tentacles' amplitude and direction |
| centroid | `sonic.brightness` | colour: the glow hue (coral for a dark timbre, mint and lime for a bright one); geometry: the petals' shape (round to pointed) |
| sustain | `response.sustain` | geometry: growth; stalks rise and tentacles lengthen while sound is held, and sink in silence |
| tempo | `beat.phase` | geometry: the jellies pulse on the beat |
| intensity | `response.intensity` | `[S]` the number of organisms in the meadow; `[S]` the coral's branch recursion depth |
| MIDI notes | `notes.voice.<0..7>` | geometry: each voice is a flower along an arc; a held note opens its flower; velocity is its size; pitch sets its place |
| MIDI chords | `notes.polyphony` | `[S]` the hero flower's petal count (polyphony x 3) |
| sustained notes | `notes.held` | geometry: the vines extend |
| release | `notes.release` | VFX: a flower drops its petals when its note ends, more for a long note |
| mod wheel | CC 1 | colour: the season of the garden (the palette's rotation) |

**Structural parameters.** Petal count, tentacle count, organism count, branch recursion depth, and dot density.

**As built (2026-10-02).** Recomposed as a true macro: a close camera just under the lantern (now with an inner ring
of upright petals and seven hanging stamens with glowing beads), f/0.8 depth of field, the jelly-trees behind as soft
backlight. Six voice flowers stand in the middle ground (closed buds at rest). The floor is 900 m wide, so its edge is
lost in haze. Late in the pass three radiolarians joined it (wire-line lattice spheres, ADR-1073: the hats light
their lattices, the treble thickens their lines, the bass swells them, the kick lights their cores).

**Expansion.** A jellyfish cathedral; a coral forest; a radiolarian sky; a seed bank that grows a tree per phrase.

---

## 6. Digital Alpine: *Glass Caldera*

**Grammar (07).** The land is FACETS: four rings of irregular flat triangles round a caldera lake (a jittered polar grid,
ridged noise for the crests, generated into GLB meshes), each ring farther, higher and paler, separated by arms of water,
plus an island and rocks. No textures, no lines, no outlines, no neon. The facets are coloured by a painter's rule (an
unlit material program): warm where they face the low sun, lilac where they face the sky, indigo in shadow, mist at the
waterline, and with distance the colour of the sky behind them (warm towards the sun, cool away). The water is not a
surface: every landform has a mirrored TWIN below the waterline, drawn darker and bluer by the same program, and the sky
is a dome whose program paints the sky above the horizon and its mirror image below it, with ripples and the sun's
broken path where each view ray meets the water. So the lake reflects mountains and sky exactly, for nothing.

**Hero frame.** A low sun just behind a violet peak, its glow over the ridge and its reflection broken on the water;
dark faceted ranges in layers, each paler than the last; the far peaks almost the colour of the sky; streaky blue
water below a perfect mirror image.

**Palette.** Blue and violet with a peach sun: zenith `#0e1840`, violet `#4d4386`, horizon `#f4c0a0` (towards the sun)
and `#a985b4` (away); shadow facets `#160f38`, sky-lit `#2f2a6e`, sun-lit `#e0785e`; the water keeps (0.36, 0.45, 0.86)
of what it reflects.

**Technique.** `meshes.py` (polar ranges, rocks, clipping at the waterline, mirroring); procedural Mesh sources; two
unlit programs (`daRock` with distances in decametres -- a material op's constants are clamped to ±1000 --, `daSky`);
a travelling-wave field (`swell`) on every landform through a field deformer; splash-ring particles; glints.

**Camera.** A low glide round the lake (radius 320 m, 170 s a circuit), banking into the turn, the view swinging twice a
circuit between looking ahead along the glide and looking in across the water at the island. As the circuit turns, the
light turns from backlit dusk (violet silhouettes and a gold path) to front-lit alpenglow (coral facets, deep blue water):
two coherent palette states, one world.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| level | `response.level` | AUDIO TOPOGRAPHY: a travelling sine wave field rolls out from the island through every range (and, mirrored, every twin), as tall as the music is strong |
| bass | `response.bass` | the shore range lights from within (spectral depth); the sun's path on the water brightens |
| low mids / mids / high mids | `audio.lowMid`, `audio.mid`, `audio.highMid` | SPECTRAL DEPTH: the main, far and distant ranges each light with their own band |
| kick | `response.kick` | the low sun flares across every facet that faces it (the sunlit colour in `daRock`); rings of light land on the water (splash particles) |
| snare | `response.snare` | a flash along the horizon, in the sky and in the lake |
| mids | `audio.mid` | the water's ripples deepen |
| highs | `audio.treble` | glints scatter on the water; the sun's glow brightens |
| tempo | `beat.phase` | the ripples drift |
| centroid | `sonic.brightness.slow` | the palette drifts warmer or cooler (hue) |
| intensity | `response.intensity` | `[S]` the ranges rise as the piece builds |
| pitch class | `notes.class.<k>` | twelve lanterns round the shore, one per pitch class, with their reflections |
| chord | `notes.polyphony` | a bigger chord lays a brighter path of light on the water |
| velocity | `notes.lastVelocity` | how bright a lantern flares |
| mod wheel | CC 1 | `[S]` night falls: the sky and the lake darken, the sun's light leaves the facets, the haze dims, the lanterns come up |

**Structural parameters.** The ranges' heights, the swell's height, day to night.

**As built (2026-10-02/03).** First built from boxes (a cube on its corner is a peak), which read as a pile of slabs;
rebuilt from generated faceted meshes. Engine facts found: material op constants clamp to ±1000; the engine water
reflects only the sky, hence the twins. For the live frame rate (the map above is as built): the waterline mist and the
kick's distance band went (each was ops on every rock fragment); the kick became the sun's flare; the sky is a short dome
program and the lake a disc with the water's program behind the twins; the default key is authored without its shadow.

**Expansion.** A fjord flown at dawn; an archipelago of faceted islands; a crater lake at night with the lanterns lit; a
waterfall of facets.

---

## 7. Chromatic Topography: *Contour Valley*

**Grammar (05).** SMOOTH FORM + COLOUR BY HEIGHT: one smooth-shaded heightfield (a sculpted winding valley, rounded
hills, ridged crests; periodic along the flight, so three copies and a sawtooth make an endless valley), coloured by an
unlit program: its height quantised into seven hard hypsometric bands, fine contour lines every 22 m that flow up the
slopes with the music, soft light from the left, the river's turquoise glow where the land meets the valley floor, cream
haze with distance. Groves (teal cones, cobalt lollipop trees) and cyan crystals are baked onto the slopes and crests
from the same height function. Distinct from Digital Alpine: smooth, saturated, graphic, map-like.

**Hero frame.** From a glide above the river: the valley winding away between banded hills -- indigo floor, violet,
magenta, coral, orange, gold, cream crests -- each ruled with contour lines, the turquoise river glowing down the
middle, groves dotting the slopes, the far hills fading into a cream horizon under a cobalt sky.

**Palette.** Bands `#1d2a72`, `#5a2a92`, `#b0308c`, `#ec4f66`, `#f68f4c`, `#fbd36a`, `#fff3cf`; river `#39f2dc`;
groves `#0d5f73` and `#2346b8`; crystals `#9ff6ff`; sky `#ffe3c6` to `#86d4ea` to `#2448a8`.

**Technique.** `meshes.heightfield` (periodic noise), `ribbon`, `cone`, `ball`, `prism`; four unlit programs
(`ctLand`, `ctFlat`, `ctRiver`, `ctSky`); seven bands from thresholds and two three-stop ramps; the contour lines a cosine
palette whose phase is routable; a sine wave field for the land's breathing.

**Camera.** The land slides towards the camera (20 m/s); the camera steers to stay over the river, looking at the next
bend and banking into it, and twice a period rises from 64 m to over 300 m above the hills and dives back.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | the river rises up its banks (the glow's height) and brightens |
| kick | `response.kick` | the contour lines flash white across the land; the river surges (as built: a band of light was ops on every land fragment) |
| snare | `response.snare` | the groves bloom (their colours flare) |
| mids | `audio.mid` | the contour lines flow up the slopes (the lines' phase, integrated) |
| low mids | `audio.lowMid` | the round trees glow |
| highs | `audio.treble` | pollen sparks over the river; the contour lines brighten |
| tempo | `beat.pulse` | the contour lines pulse |
| centroid | `sonic.brightness.slow` | the palette drifts (hue) and the haze warms |
| intensity | `response.intensity` | `[S]` the land rises (the hills grow taller as the piece builds) |
| level | `response.level` | the land breathes (a slow wave rolls through it) |
| pitch class | `notes.class.<k>` | twelve families of crystals on the crests, one per pitch class |
| chord | `notes.polyphony` | the river's glow widens with the chord |
| velocity | `notes.lastVelocity` | how bright a crystal flares |
| mod wheel | CC 1 | `[S]` the contour spacing, from fine survey lines to broad terraces |

**Structural parameters.** Band count and heights, contour spacing, the land's vertical scale, the river's level.

**As built (2026-10-03).** As planned; the first look was a canyon (walls everywhere), so the outer walls were lowered and
the glide given its rise over the hills. For the live frame rate (the land covers the frame and every pixel of it runs
the program): the air is the engine's analytic fog taking the sky's colour (`fogSky`), not a program; the land is a
grid that follows the valley (dense on the floor and banks, 75 m apart on the far hills: half the triangles of a
uniform grid); the bands are found four thresholds at a time (a 24-op program); and the default key light is authored
without its shadow (every material is unlit). At the live floor (1366x988) the last two steps took it from 18.3 ms to
14.5.

**Expansion.** A delta of glowing rivers seen from above; terraced islands in a colour-field sea; a night version with
contour lines of light.

---

## 8. Glitch Signal: *Pixel Canyon*

**Grammar (06).** Everything is a BLOCK on one 6 m grid: a canyon whose walls and floor are grids of thousands of dark
blocks, some lit cyan and white in a data pattern, ruled with scanlines, streaked with flowing data ribbons, rained on by
falling pixels, with floating fragments of the signal at the sides and a white-hot horizon where the signal comes from.
The blocks' colours come from each block's own centre (a noise sampled on a cylinder round the flight axis, so the pattern
is periodic and rides with the blocks through the seamless sawtooth). Damage is deformers (a row shear for drifting
sections, a block-scale noise for fragmentation) and the post-glitch vocabulary. Not a city, not Tron: a broken signal.

**Hero frame.** Down the canyon: walls of blue-black blocks with lit cyan pixels converging on a white diamond of light,
the floor tiles lit in a pattern, pixel rain falling, thin data ribbons streaking along the walls; scanlines over all.

**Palette.** Blue-black `#020309`, dim `#0b1c5a`, lit `#9ef6ff`, white; in corruption magenta `#ff2bd6`, acid green,
orange `#ff5a1f`; after each collapse the whole palette moves round the hue circle.

**Glitch at three scales.** MICRO, always: scanlines (0.22), 1.4 px of RGB split, a 1.5% flicker of glitch blocks, the
hats' block flicker. MESO: snares tear the frame and shear the rows, onsets swap channels. MACRO, the signature: a STRONG
EVENT (a hard kick at a moment of large spectral change: the kick's envelope times the flux above 0.62, measured on the
review music at three to five times in thirty seconds; or a note at full velocity) sets off the staged COLLAPSE --
0 ms RGB separation begins and grows spectral; 250 ms the rows drift and the frame tears; 520 ms the walls fragment into
blocks; 820 ms all but total corruption (blocks, channel swaps, pixel sort, mosaic, saturation); 1000 ms a wave of colour
runs through the camera (a shock ring closing on the centre, a hue flash); 1120 ms RECONSTRUCTION in a NEW
CONFIGURATION, integrated so it stays: the world turned about the flight (walls become floor and sky), the palette moved
round the hue circle, the data pattern and the row stagger re-drawn. Reactivity continues from the new state.

**Technique.** Grid distributions of boxes; `gsPixel` (centre = world - local - the sawtooth offset; a periodic noise;
three levels; scanlines; the kick's band; a fade into the dark); `gsRibbon`; sine and noise deformers; route chains with
delays, peak-hold envelopes, springs and integration staging one trigger into the six stages.

**Camera.** Down the middle of the canyon at 30 m/s (the world slides), drifting; after each collapse a new roll.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| strong event | `visual.collapse` (`kickEnv` x `flux`, or a full-velocity note) | MACRO: the staged collapse and `[S]` the reconstruction in a new configuration (roll, palette, pattern, stagger) |
| bass | `response.bass` | the RGB split widens; the walls push apart; the horizon flares |
| kick | `response.kick` | a ring of distortion comes out of the horizon through the frame (post shock); the lit pixels flash (as built: the wave of light was ops on every block fragment) |
| snare | `response.snare` | MESO: the frame tears; the rows shear sideways |
| hat | `response.hat` | MICRO: glitch blocks flicker |
| onset | `response.onset` | MESO: channels swap in the glitch blocks |
| mids | `audio.mid` | the data ribbons stream faster; the rows' stagger drifts |
| highs | `audio.treble` | the scanlines deepen; the pixel rain thickens |
| brightness | `sonic.brightness` | the lit pixels brighten |
| tempo | `beat.pulse` | the lit pixels pulse |
| intensity | `response.intensity` | `[S]` more of the pattern lights as the piece builds |
| pitch | `notes.lastPitch` | the fragments rise with the pitch |
| velocity | `notes.lastVelocity` | how hard a note glitches the frame |
| held | `notes.held` | the fragments hold their light |
| mod wheel | CC 1 | corruption by hand: blocks, split and sort |

**As built (2026-10-03).** As planned. The block program is 20 ops (one palette op makes the whole noise cylinder, the
sawtooth rides in its value, the three colours are one ramp), the air is analytic fog, and the default key is authored
without its shadow: 13.0 ms at the live floor.

**Engine needs.** None for the collapse: it is built from what exists (route delays, envelopes, springs, integration, the
post-glitch pass). Two notes for the coordinator in `PROGRESS-abstract.md`: a route's delay stage interpolates between
frames (a one-frame event delayed can fall below its threshold: trigger from envelopes), and there is no way to give a
trigger a refractory period (a strong passage can chain collapses; the threshold was measured so it rarely does).

**Expansion.** A datamosh river; a pixel-sorted waterfall; a signal tower that collapses into a city of blocks and
rebuilds as a forest.

---

## 9. 8-Bit Ocean: *Bubble Reef*

**Grammar (08).** The 8-BIT BLOCK and the CHUNKY PRIMITIVE: voxel floors, rock walls, kelp, branch coral, rocks, ledges
and fish (generated meshes, only the faces between a block and water), with low-segment primitives for the rest
(eight-sided columns, ten-sided pipes, low-poly balls, a spiral shell); two-band toon light from the surface, thin navy
outlines, blue depth haze, caustics crawling over the floor. The level is a RING round a deep basin with a giant coral
tower, so the swim never ends: three areas with their own palettes -- SUNNY SHALLOWS (blue, cyan, yellow: brain coral,
cyan kelp, a trail of spinning coins, the coral arch you enter by), FLOWER RUINS (purple, pink, turquoise: a colonnade
and arches, enormous pink flowers, jellies, gems), PIPE DEEP (deep blue, orange, green: green pipes, orange branch coral,
the house-sized shell) -- and the water's colour changes as you pass from one to the next. Original shapes throughout.

**Hero frame.** Low over the golden checker floor of the shallows, the coral arch ahead with a trail of coins leading
through it, cyan kelp stacks either side, brick ledges floating overhead, light shafts from the surface, bubbles rising.

**Palette.** Per area (floor, rock, kelp, coral, water): shallows `#f7c548` `#3d6fd6` `#22d8ff` `#ffcf1f` `#0e58c8`;
ruins `#8a52e0` `#5b34b0` `#18e6c8` `#ff4fb0` `#2f22a8`; deep `#2648c8` `#1a2c86` `#4fe03a` `#ff7a1a` `#06206e`;
outlines `#081a52`.

**Camera.** A playful swim round the ring (150 s a lap, about 5 m/s), bobbing, weaving across the lane, glancing in and
out, rolling a little.

**Modulation map (THE LIVING LEVEL).**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | every kelp plant bends at once (a bend about each plant's base, on a loose spring); the brain coral pulses; the sunlight swells |
| kick | `response.kick` | the coins and gems flash (treasure); a ring ripples out through the water |
| snare | `response.snare` | a swarm of bubbles bursts round the swimmer; the fish schools turn and dart |
| hat | `response.hat` | sparkles glint |
| mids | `audio.mid` | the fish swim faster (integrated); the bubble streams thicken |
| highs | `audio.treble` | tiny sparkling creatures swarm |
| tempo | `beat.pulse` | the caustics pulse; the jellies pump |
| brightness | `sonic.brightness.slow` | the water clears |
| intensity | `response.intensity` | `[S]` the coral grows as the piece builds |
| pitch class | `notes.class.<k>` | a row of twelve bell flowers in each area, one per pitch class: a melody rings them note by note (a xylophone) |
| chord | `notes.polyphony` | a chord blooms every brain coral and lights it |
| velocity | `notes.lastVelocity` | how big the bubble burst a note releases |
| held | `notes.held` | the kelp glows while notes are held |
| mod wheel | CC 1 | `[S]` the depth: the water darkens and thickens |
| pitch | `notes.lastPitch` | THE GUIDE (a big friendly voxel fish circling over the lane) swims the melody's contour: higher notes lift it |
| kick, bass | `response.kick`, `response.bass` | the guide bobs on the kick; the bass beats its tail (its body's bend, on a spring) |
| held | `notes.held` | the guide's eyes light while notes are held |

**As built (2026-10-03).** The radial distribution's angle convention (angle e at (cos e, 0, -sin e)) cost one round;
the first looks were pastel and empty, so the palettes were saturated, the shadows deepened, the lane lined with coral
and kelp, brick ledges floated over it, and outlines added for the sprite silhouette.

**The guide (2026-10-03, the review's "it needs a hero").** A big friendly voxel fish (29 m: an orange body, a cream
belly, coral fins, white eyes with navy pupils, cut from one solid so no face is drawn between two colours) circling a
point over the lane just past the arch (a 20 m loop every 16 s at the swimmer's eye level), so every lap begins by
meeting it, and the hero still (14 s) has it crossing the lane ahead in profile over the coin trail. Tried first:
swimming the ring against the swim, which a camera looking along the lane sees nose-on (a blob of blocks), and above
the arch, cut by the frame. It sings along: the pitch lifts it, the kick bobs it, the bass beats its tail.

**Expansion.** A sunken castle level; a night level of glowing jellies; a boss chamber where the guide waits; a lava
level in the same grammar.

---

## The set list

`examples/index.json`, category "Sonic Abstract", in this order: 1 Sacred Geometry Flight, 2 Neon Vector World,
3 Cel-Shaded Dream World, 4 Infinite Color Geometry, 5 Organic Digital Garden, 6 Digital Alpine, 7 Chromatic Topography,
8 Glitch Signal, 9 8-Bit Ocean. Entries are named "Sonic Abstract - <direction>", so the owner can say "number seven" and
be understood. The live switcher (ADR-1074) steps through the set the open project belongs to: the nine, program n =
scene n mod 9.
