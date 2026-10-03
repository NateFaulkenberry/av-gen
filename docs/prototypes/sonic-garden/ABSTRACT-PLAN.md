# Sonic Abstract: eight visual languages (the plan)

The art agent, 2026-10-02, under `04-brief-abstract-direction.md` (the owner's words govern). This is the plan written
before the build. It is a living document: each section gets an "as built" note when its prototype changes from what is
written here. Progress and resume notes: `PROGRESS-abstract.md`.

**Its own project** (the owner's decision, 2026-10-02): the eight prototypes are "Sonic Abstract" (a working title),
separate from Sonic Garden, whose live scene and Sonic VFX set stay as they are. The projects live in
`examples/sonic-abstract/`, listed under their own index category, "Sonic Abstract". They are built by
`tools/sonic_vfx/abstract.py build` from the modules in `tools/sonic_vfx/scenes/` named in `scenes.ABSTRACT`.

## What every prototype obeys

1. **Simple things arranged in extremely cool ways.** Primitives (rings, slabs, tubes, spheres, lines, particles), no
   scanned assets, no textures, no realism. Each prototype is a visual GRAMMAR: a short list of rules that the scene
   follows everywhere, so it can grow into more scenes.
2. **The still frame works frozen.** The silent render (`variant.make_silent`: no routes, no interpret sources, no
   triggers, no audio) must already pass the gate: "would someone see this screenshot and think 'holy shit, that's
   cool'?" When it does not, the design is changed, not the polish.
3. **Eight artists, not eight presets.** Each prototype has its own palette family, light model, line or surface
   vocabulary, camera language and motion character (table below). No two read as variations of one look.
4. **Instruments, not visualizers.** Audio drives the structure of the artwork (counts, orders, spacing, recursion
   depth), not only its brightness. Every audio dimension has a different job in each prototype: bass is never only
   "scale everything", highs are never only "sparkle". Post effects are punctuation.
5. **Live first.** Everything is built on the live bus (`response.*`, `notes.*`, `audio.*` bands, `beat.*`), so a
   keyboard and a synth play it exactly as a recording does. Each prototype must hold 60 fps live at the editor's
   adaptive scale.

## How the eight differ (by construction)

| # | prototype | the artist it could be | palette family | surface and line vocabulary | light model | camera language | motion character |
|---|---|---|---|---|---|---|---|
| 1 | Sacred Geometry Garden: *The Armillary* | a sacred-geometry illuminator; John Whitney's differential motion | gold and ivory on ultramarine black, one vermilion accent | fine emissive lines, beads, nested rings and polygons | self-luminous lines, bloom, faint indigo haze between layers | locked frontal, perfect symmetry, slow breath | hypnotic differential rotation; rings opening into a sphere |
| 2 | Neon Vector World: *Pulsar Plain* | a motion designer (Unknown Pleasures, oscilloscope music, Swiss posters) | black, white line, ONE flat accent colour per section | hidden-line ridgelines, outline primitives, flat colour blocks | none: lines and blocks are their own light | low, fast, forward flight over the lines | rippling lines, beat-built blocks, speed |
| 3 | Cel-Shaded Dream World: *Candy Archipelago* | an indie game art director (Wind Waker, A Short Hike, Monument Valley) | pastel candy daylight: peach sky, mint, lilac, butter; plum outlines | low-poly toy forms, 2-3 flat shade bands, outlines | one hard key light, tinted graphic shadows | a slow orbit round a toy diorama | bouncy, squash and stretch, creatures that dance |
| 4 | Infinite Color Geometry: *Chromatic Corridor* | a light-and-space installation artist (Turrell, Rothko, Cruz-Diez) | full-frame saturated gradient: vermilion, magenta, violet, cobalt, teal | monumental planes and portals of pure colour, no lines | coloured fog, a blinding light at the end, gradients | gliding forward through it | a corridor that breathes, twists and recolours |
| 5 | Organic Digital Garden: *Lantern Reef* | a generative biologist (Haeckel, teamLab) shot by a macro photographer | deep teal and emerald dark, coral, peach and lime glow | translucent jelly (fresnel rims), tubes, radial petals, polka dots | glow from within, soft bokeh, shallow depth of field | macro: slow lateral slide, focus pulls | breathing, blooming, swaying, growing |
| 6 | Particle / VFX World: *Galaxy Engine* | a generative particle artist | violet black, iridescent spectrum (cyan, magenta, gold) | particles only: arms, streams, ribbons, curtains | particles are the light; feedback silk; chromatic fringe | a slow orbit at three quarters | flowing, orbiting, exploding and reforming |
| 7 | Impossible Architecture: *Relativity Court* | a surrealist painter (de Chirico's light, Escher's topology) | terracotta, ochre, cream; deep green-teal sky | architectural primitives (stairs, arches, slabs) with ink edge lines | low raking sun, long hard shadows, warm haze | a slow drift through an infinite lattice; the world rolls | slow, inevitable; gravity changes |
| 8 | Abstract Cinematic Void: *The Gate* | a film cinematographer (Deakins, Villeneuve) | amber haze and teal shadow, one white-gold light | one colossal segmented ring, a mirror plane, a tiny figure | volumetric haze, a sun inside the ring, god rays | a majestic slow push, letterboxed | almost still; the ring's segments drift and close |

Signals named below (all on the live bus): `response.bass`, `response.kick|snare|hat|low|onset` (events, with `...Env`
envelopes), `response.sustain`, `response.flux`, `response.melodic`, `response.intensity` (12 s), `response.level`; bands
`audio.bass|lowMid|mid|highMid|treble`, centroid `audio.spectralCentroid` and the timbre `sonic.brightness` (medium) and
`sonic.brightness.slow`; tempo `beat.phase|pulse|bar`, `beat.bpm`; MIDI `notes.lastPitch|lastVelocity|interval|
polyphony|chord|tension|held|density`, the voice slots `notes.voice.<0..7>.{held,velocity,pitch}`, the pitch-class lanes
`notes.class.<0..11>`, `response.note|noteEnv`, `notes.release`; the mod wheel (CC 1) through the project's control map.
`[S]` marks a STRUCTURAL target: a count, order, spacing or depth that rebuilds the artwork.

---

## 1. Sacred Geometry Garden: *The Armillary*

**Grammar.** Everything is a circle or a regular polygon, all sharing one centre, drawn as fine lines of light with beads
where lines meet. Nested layers recede in depth, so the diagram is also a well. Motion is differential rotation (John
Whitney): ring k turns at k times a base rate, so the pattern dissolves and re-forms, re-aligning on the bar. At rest the
rings are coplanar, a flat mandala; energy opens them about their own diameters into a 3D armillary sphere, and they close
again.

**Hero frame.** Perfectly frontal and centred, filling the frame edge to edge on an ultramarine-black void. The outermost
layer is a crown of 24 vesica petals and a ring of 48 beads. Inside it is a 12-pointed star from two hexagrams, then
nested squares and hexagons. Each of these is turned a little more than the one outside it, so together they spiral
inward like a golden-ratio construction. At the centre is a seed of life (7 circles) around a white-hot core. Gold lines
of two weights, ivory beads, and one vermilion accent ring. Faint indigo haze lies between the layers, so the deeper
rings dim. A dim flower-of-life lattice fills the whole background like a watermark.

**Palette.** Ground `#05061a` to `#0d1240`. Lines gold `#f2b84b`, pale gold `#ffd98a`, ivory `#fff1d0`. Accent
vermilion `#ff4a24`, used only on the core and the chord's ring.

**Technique.** Procedural tori with low `majorSegments` (polygons) and thin `minorRadius` (lines), unlit and emissive.
Radial distributions for petals, beads and spokes. One node per ring, each with its own rotation track and routes, at
staggered depths. Bloom at a low threshold for the glow. `temporal/feedback` is held at zoom 1, so rotating rings leave
woven light trails (long exposure). Particles ride circular splines as beads (`shape: spline`). Tonemap 4 (clamp) keeps
the flat gold.

**Camera.** Locked frontal (50 mm), with a 4-degree breathing arc over 64 s and a slow push-in while sound is
sustained.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | geometry: the whole diagram's radial breathing (each layer's scale, deeper layers less); light: the core's glow |
| kick | `response.kick` | geometry: the rings snap OPEN about their axes (a spring chain, so they ring and settle back flat); VFX: a bead flash running outward |
| snare | `response.snare` | colour: the star flashes vermilion; VFX: the petals fire sparks outward |
| highs | `audio.treble`, `response.hat` | VFX: bead particles released along the ring paths; geometry: the fine outer ring of 48 beads shimmers (scale) |
| mids | `audio.mid`, `response.melodic` | geometry: the differential rotation rate (k x base), so a busy middle spins the pattern faster |
| centroid | `sonic.brightness` | colour: gold (dark timbre) through to ivory and pale cyan-white (bright); line weight thins as the sound brightens |
| bands | `audio.bass` / `lowMid` / `mid` / `highMid` / `treble` | each band owns one LAYER of the well, from the core (bass) outward to the crown (treble) |
| sustain | `response.sustain` | camera: the slow push into the well; VFX: the feedback trails lengthen |
| tempo | `beat.phase`, `beat.bar` | geometry: the base rotation is locked to the bar, so the rings re-align into the flat mandala on each downbeat |
| intensity | `response.intensity` | `[S]` ring count (inner layers switch on as a piece builds), `[S]` petal count 12, 24, 48 |
| MIDI notes | `notes.class.<k>`, `response.note` | geometry: the 12 pitch classes are 12 points on the outer circle; a sounding class lights its petal and spoke, so a chord draws its own polygon |
| MIDI chords | `notes.polyphony` | `[S]` the polygon order of the inner star: 3 notes give a triangle, 4 a square, 6 a hexagram |
| MIDI pitch | `notes.lastPitch` | geometry: which nested layer answers the note (low notes deep, high notes at the crown) |
| velocity | `notes.lastVelocity` | light: how hard a note flares its ring |
| sustained notes | `notes.held` | geometry: the armillary stays open while a note is held |
| mod wheel | CC 1 | geometry: opens the whole armillary into a sphere by hand |

**Structural parameters.** Ring count, petal count (symmetry order), each ring's polygon order, bead count, the inner
spiral's turn per layer, and the layers' depth spacing.

**As built (2026-10-02).** As planned, with these differences:
- the armillary is four rings (a circle, a 12-gon, a circle, a hexagon);
- the inner star is four triangles (a 12-pointed star);
- the twelve pitch classes are a clock of diamonds and spokes round the crown;
- a polygon in the heart has as many sides as notes sounding;
- the light trails are `temporal/feedback` at zoom 1.

The lattice fades into the halo's colour, because opaque lines cannot fade to nothing.

**Expansion.** Kepler's nested Platonic solids; a garden of small armillaries on a dark plane; a flower-of-life
tessellation flown over; Islamic star-pattern tilings; a tunnel of mandalas.

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
lost in haze.

**Expansion.** A jellyfish cathedral; a coral forest; a radiolarian sky; a seed bank that grows a tree per phrase.

---

## 6. Particle / VFX World: *Galaxy Engine*

**Grammar.** Particles are the forms; there is almost no geometry. Large structures are made of points: spiral arms,
streams, a ring vortex, curtains. Motion is orbit and flow, explosions reform into the structure, and the feedback turns
points into silk.

**Hero frame.** A vast two-armed spiral of luminous points at three quarters. The arms shade from deep blue at the
rim through magenta to gold at the core, around a white-hot core. Ribbon trails flow along the arms. A halo of dust
curtains sits round it, and one stream of particles arcs over the camera. A violet-black ground carries faint nebular
colour from feedback smears, with a chromatic fringe at the edges.

**Palette.** Ground `#05020c`. Points from `#2b6bff` (rim) through `#ff3fb4` to `#ffc35a` (core). Core `#fff6e0`.

**Technique.** Particle systems with an attractor and orbit (rotation), emitted along spiral splines (arms) and a disc
(the halo). The arms' splines are modulatable (`spline/<arm>/turns`, `radius`). Trails make the ribbons. Velocity stretch
makes the streaks. `temporal/feedback` (decay 0.85, a slight zoom and turn) makes the silk. Bloom, split and shock are
used on hits.

**Camera.** A slow orbit at 30 degrees elevation, about 60 s a turn, with shake on the kick.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | VFX: the attractor's pull (the galaxy contracts and blooms out); light: the core's brightness |
| kick | `response.kick` | VFX: an explosion; a radial burst from the core and a shock ring; the arms reform after it |
| snare | `response.snare` | VFX: a flare runs along an arm (a burst on the arm's spline) |
| highs | `audio.treble`, `response.hat` | VFX: glitter (tiny fast sparks); colour: the rim's sparkle |
| mids | `audio.mid` | VFX: the orbit speed (the galaxy turns); the curl turbulence |
| flux | `response.flux` | VFX: turbulence; the arms fray into swirls |
| centroid | `sonic.brightness` | colour: the arms' gradient (warm to cool); particle size |
| tempo | `beat.phase` | `[S]` the spiral's turns step on the bar |
| intensity | `response.intensity` | `[S]` particle count (spawn rate) and `[S]` trail length |
| MIDI notes | `notes.lastPitch`, `response.note` | VFX: each note launches a comet from the rim at its pitch's angle; velocity is its brightness |
| MIDI chords | `notes.polyphony` | `[S]` the number of arms (2 to 6 arm emitters) |
| sustained notes | `notes.held` | VFX: the halo curtains form |
| mod wheel | CC 1 | geometry: the galaxy's tilt |

**Structural parameters.** Arm count, spiral turns, particle count, trail length, and halo size.

**As built (2026-10-02).** Two arms of three spiral segments each (gold, magenta, blue), whose splines turn (the
galaxy rotates while its points flow inward); a fine-dust halo; four extra arms that chords light; comets from a short
spiral arc whose start angle the pitch sets; a blast system and a screen shock for the kick.

**Expansion.** A ring vortex (a smoke ring of light); a river of light over a dark sea; a particle flower that blooms
per chord; a murmuration (dark particles against a sunset).

---

## 7. Impossible Architecture: *Relativity Court*

**Grammar.** A few architectural primitives (stairs, arches, slabs, columns, doorways) arranged in a space with three
gravities, as in Escher's *Relativity*. One module repeats forever in every direction (*Manifold Garden*), and rooms sit
inside rooms. Painted in de Chirico's light: low raking sun, long hard shadows, warm flat colours, and ink edge lines on
every crease.

**Hero frame.** A cubic courtyard seen from inside one of its stairways. Stairs climb its floor, its walls and its
ceiling, each flight in its own gravity, with arches and doorways on every face. Through the openings the same court
repeats in all directions into a warm haze. A low golden sun throws long, hard shadows across the terracotta and cream
surfaces. In the gaps, the sky is deep green-teal. One doorway glows.

**Palette.** Terracotta `#c8553d`, ochre `#e0a458`, cream `#f3e3c3`, ink `#2a1b14`. Sky `#1f6f6a` to `#0d3b3a`. Sun
`#ffd08a`.

**Technique.** One compiled SDF: the `stairs` primitive (ADR-1040), arches (box minus cylinders), `rotate` for the three
gravities and `repeat` (infinite) in three axes. A `recurse` gives the room inside the room. Look: SDF shadows toward the
sun (a penumbra march), ambient occlusion and ink edge lines (`edgePixels`). Fog fades the repetitions, and surfaces use
the line look.

**Camera.** A slow drift through the lattice. The world rolls (the SDF's rotation) a quarter turn per phrase, so "up"
changes and the shadows sweep.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | `[S]` the lattice's spacing (`repeat` size): the courts breathe apart and together |
| kick | `response.kick` | geometry: a stair module slides one step (a translation impulse with a spring); light: a doorway flashes |
| snare | `response.snare` | geometry: a flight of stairs folds over to a new gravity (a 90-degree turn on a spring) |
| highs | `audio.treble`, `response.hat` | light: the ink lines sharpen and the edge intensity rises; VFX: dust motes in the light |
| mids | `audio.mid` | light: the sun's direction sweeps, so the shadows move |
| centroid | `sonic.brightness.slow` | colour: the sky and haze colour (teal dusk to gold noon); the light's warmth |
| tempo | `beat.bar` | camera: the world's roll steps a quarter turn per phrase |
| intensity | `response.intensity` | `[S]` how many rooms sit inside the room (`recurse` count) |
| MIDI notes | `notes.lastPitch`, `response.note` | light: a doorway at the pitch's height lights up (low notes low, high notes high) |
| MIDI chords | `notes.polyphony` | `[S]` the stairs' step count per flight |
| velocity | `notes.lastVelocity` | light: how bright the doorway glows |
| sustained notes | `notes.held` | geometry: the space unfolds (a fold plane opens the court) |
| mod wheel | CC 1 | camera and geometry: the world's roll by hand |

**Structural parameters.** Lattice spacing, room recursion depth, step count, and arch count per face.

**As built (2026-10-02).** One compiled SDF: three flights (the same flight under the two cyclic axis permutations)
round nested open cubes (`recurse`), repeated forever in three axes with an 18 m period; cel lighting on the SDF, its
own shadow march toward the sun, the ink outline fading with distance. The world rolls a full turn in four minutes.

**Expansion.** Penrose stairs in an isometric diorama; a monastery of folding staircases; endless arcades at noon;
rooms inside rooms as a zoom.

---

## 8. Abstract Cinematic Void: *The Gate*

**Grammar.** One colossal object, one horizon, one light. The ring is 64 segments that can part. A perfect mirror
plane lies beneath it. A tiny figure gives the scale. Atmosphere does the rest: haze, god rays and dust. Framing is
letterboxed 2.39:1, the camera is slow and majestic, and the colour story is amber against teal.

**Hero frame.** A colossal ring 300 m across stands on the horizon of a perfect mirror plane. A low sun sits exactly
inside it, so its opening is a white-gold disc of light, and god rays stream through it toward the camera across thick
amber haze. The ring is a silhouette of dark segments with thin gaps of light between them. Its reflection lies in the
plane below. In the foreground, small on the lower third, stands one human figure as a simple silhouette. The sky grades
from deep teal at the top to amber at the horizon, inside black letterbox bars.

**Palette.** Teal `#0e2a33`, `#1d4a52`. Amber `#ff9a3c`, `#ffcf86`. Light `#fff4dc`. Silhouette `#0a0a0c`.

**Technique.** A radial distribution of 64 box segments (the ring), dark and lit from behind. A mirror twin below the
plane, since the engine has no planar reflection: there is no floor geometry, and the sky's lower hemisphere is the
plane. Volumetric fog with the sun's volumetric light, a `lightBeam` through the ring, and dust particles in the beams.
A tiny figure is a capsule silhouette. Letterbox bars are black slabs in front of the lens, or the render size for review
clips.

**Camera.** A very slow push toward the ring from far away (120 s), low (1.7 m), on a long lens (85 mm), with a slight
shake on the kick.

**Modulation map.**

| dimension | signals | drives |
|---|---|---|
| bass | `response.bass` | VFX: the haze density breathes; light: the sun's bloom swells |
| kick | `response.kick` | light: a pulse of light runs round the ring's gaps; camera: a slight shake |
| snare | `response.snare` | VFX: dust bursts in the beams |
| highs | `audio.treble`, `response.hat` | VFX: the dust glitters; light: specular glints on the segments' edges |
| mids | `audio.mid` | geometry: the ring turns slowly about its axis |
| centroid | `sonic.brightness.slow` | colour: the sky's grade (amber to rose to teal) |
| sustain | `response.sustain` | light: the sun inside the ring brightens; VFX: the god rays lengthen |
| tempo | `beat.bar` | geometry: the segments step apart and back on the phrase |
| intensity | `response.intensity` | `[S]` the gaps between segments (the ring parts as the piece builds); `[S]` concentric rings appear |
| MIDI notes | `notes.lastPitch`, `response.note` | light: a segment lights at the pitch's angle round the ring |
| MIDI chords | `notes.polyphony` | `[S]` the segment count (32 to 96) |
| sustained notes | `notes.held` | camera: the push holds; geometry: the ring's segments drift outward |
| mod wheel | CC 1 | light: the sun's height inside the ring (a sunrise by hand) |

**Structural parameters.** Segment count, gap size, ring count, and segment depth.

**As built (2026-10-02).** Rebuilt after review:
- almost no air: beyond the march the density is surface fog, which had turned the ring into fog colour;
- a geometric sun disc whose rim is the sky's own gradient, because the sky's sun renders blocky from the lighting
  cube;
- the mirror as one surface carrying the sun's reflection (the sky's gradient mirrored).

The ring is 200 m across on the horizon; the letterbox is the engine's `post/display/letterbox` (ADR-1075).

**Expansion.** A glowing sphere over an ocean; a field of suspended monoliths; a distant geometric city on the horizon;
an eclipse.

---

## The set list

`examples/index.json`, category "Sonic Abstract", in this order: 1 Sacred Geometry Garden, 2 Neon Vector World,
3 Cel-Shaded Dream World, 4 Infinite Color Geometry, 5 Organic Digital Garden, 6 Particle / VFX World, 7 Impossible
Architecture, 8 Abstract Cinematic Void. Entries are named "Sonic Abstract - <direction>", so the owner can say
"number three" and be understood. The live switcher (ADR-1063) steps through the Sonic VFX set; the engineer is
making it step through the set the open project belongs to. Until that lands, each prototype is opened directly.
