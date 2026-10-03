# Sonic VFX: the scene catalog (deliverable 6)

Art agent, 2026-10-02. The owner's brief is `02-brief-vfx-expansion.md`. The rules these designs follow are in
`research/01-vfx-scene-construction.md` §13 and `research/03-authoredness.md`. The signal vocabulary is the engineer's
`VFX-ARCHITECTURE.md` §1.1.

**Sixteen proposed worlds. Each is a shot a VFX artist would design: a thesis, a composition, a palette, a motion
hierarchy, and an instrument that belongs to that world.** None of them is a variation of another, and none is
the garden made more complicated (§23). Eight are built first (tier A). The other eight follow, and the weakest are
killed rather than polished (§16, deliverable 23).

## How to read a scene

| field | what it fixes |
|---|---|
| thesis | one sentence: the subject, and the musical role of its parts |
| composition | background, midground, foreground, focal subject, secondaries, atmosphere, post, camera |
| palette | dominant, secondary, accent (under 10% of the frame), highlight (reserved for the subject and the hits), the background value |
| motion | very slow (sky, far structure), medium (environment, secondary objects), fast (focal VFX, note events), extremely fast (flashes, glints, glitches) |
| instrument | each signal class's job in this world |
| build | the engine features it uses, its live cost class, and what it needs from engineering |
| kill if | the failure that would make it go |

The signal classes (`VFX-ARCHITECTURE.md` §1.1, live and from files alike):
- **Sustained:** `response.sustain`, `sonic.energy.slow`, and `notes.held` / `notes.legato` for MIDI.
- **Melodic:** `notes.noteOn` with `notes.lastPitch` and `notes.lastVelocity`, `notes.interval` / `notes.step`, and
  `response.melodic`.
- **Bass:** `response.bass` (the level) and `response.low` (a bass attack).
- **Kick, snare, hat:** `response.kick`, `response.snare`, `response.hat`, each with an envelope twin, plus
  `response.hatRate`.
- **The distinct MIDI facts:** `notes.polyphony`, `density`, `rhythm`, `velocity`, `velocitySpread`, `range`,
  `duration`, `chord`, `tension`, `release` (note-off with its duration), and `low` / `high` (the register split).
- **Timbre**, the identity of the sound, which colours each world: `sonic.brightness`, `warmth` and `roughness`
  (medium tier).

The common grammar:
- **Sustained energy grows, opens and warms a world.**
- **Melody places light where the pitch says.**
- **Bass moves mass.**
- **Hits flash, burst and crack, each in its own region** (the kick and the hat never move the same thing).
- **Timbre colours** (the filter opens the light; distortion roughens the forms).

Every route that serves a tier uses that tier's chain timing: SLOW 0.9/2.4 s, MEDIUM 250/900 ms, FAST 30/350 ms, HIT
0/180 ms, SNAP 0/70 ms.

Every scene is a `sonic.live` project in the Examples menu (category "Sonic VFX"). It reads only bus signals, so a
keyboard and a synth play it exactly as a replayed recording does. Each records its own design in the project's
`sonicScene` block, so the evaluator and a reader can check the scene against it.

---

## Tier A

### 1. Event Horizon (cosmic)

**Thesis.** A black hole drinks a disk of light: every note is a star that falls into it, and the bass is the hole's
own mass bending the sky.

| | |
|---|---|
| background | a desaturated star field and the galaxy's dust band, lensed into arcs round the shadow |
| midground | the accretion disk on a shallow diagonal: white-hot at its inner edge, ember-red outside; its far side bent up over the shadow and under it by the lens |
| foreground | a basalt fragment drifting out of focus in the lower left; fine dust |
| focal | the shadow and its photon ring, on the right third |
| secondaries | note-stars spiralling in with trails; the hot spot on the approaching side; faint polar jets |
| atmosphere | none (vacuum): glowing gas and dust only |
| post | bloom and warm halation on the inner disk, a whisper of anamorphic streak (DNGR's IMAX flare), edge chromatic aberration, grain, a heavy vignette |
| camera | a 36-degree arc swept there and back over 160 s, 5-7 degrees above the disk plane, the hole held on the right third |

**Palette.**
- dominant: void indigo-black `#04030a`;
- secondary: ember `#ff7a2a`, falling to deep red `#7a1808` outside;
- accent: jet blue `#6aa8ff`;
- highlight: white-hot `#fff3dc`, reserved for the inner edge and the photon ring;
- background value: very dark.

Saturation is highest in the inner disk, and the stars are desaturated.

**Motion.**
- very slow: the camera's arc, and the lensed star field drifting with it;
- medium: the disk turning Keplerian, inner bands faster, and the gas spiralling in;
- fast: note-stars falling in over 2-4 s, and the flare;
- extremely fast: the photon ring's flash, sparks, and the lens's chroma tick.

**Instrument.**

| class | signal | what the world does |
|---|---|---|
| sustained | `response.sustain` | the disk heats: brighter, denser, whiter gas |
| melodic | `notes.noteOn` + `notes.lastPitch` | a star is born at the orbit its pitch sets (low notes far out and slow, high notes close and fast: Kepler) and spirals in, trailing light |
| velocity | `notes.lastVelocity` | how many stars, and how bright |
| polyphony / chord | `notes.polyphony` | a chord releases a cluster: the birthplace spreads |
| bass | `response.bass` | the hole's mass: the Einstein radius and the shadow swell, and the sky bends more |
| kick | `response.kick` | the photon ring flashes and the jets pulse outward |
| snare | `response.snare` | a flare erupts from the hot spot |
| hat | `response.hat` | sparks glint across the disk |
| density | `notes.density`, `rhythm` | busy playing churns the gas |
| tension | `notes.tension` | dissonance splits the lens's colours |
| timbre | `sonic.brightness`, roughness | a bright sound turns the inner edge blue-white; a rough one breaks the disk up |

**Build.**
- `gravLens` in black-hole mode. Look development showed the *Interstellar* image emerge from it unauthored: the
  disk's far side lensed over and under the shadow.
- Three thin opaque bands at the innermost orbit, each turned by a twist with speed only (safe since ADR-1021).
- Particle gas: outer and inner populations, and a beaming patch on the approaching side, all with the orbit force
  and velocity stretch.
- Note-stars: a particle burst at a pitch-placed emitter, with ribbons.
- Jets, a flare and sparks as particles; `stars`.
- No fog. **Cost class: light.**

**Kill if** the disk still reads as dots or as a plate after two passes. DNGR's "anemic", thin, bright disk is the
target.

**As built** (`scenes/event_horizon.py`):
- The disk reads, and the lens bends its far side over and under the shadow as planned: three thin opaque bands at
  the innermost orbit, inner and outer particle gas and a beaming patch, all on the orbit force.
- Each note is born as stars at the orbit its pitch sets (low notes far out, high notes close) and spirals in; the
  velocity scales the burst.
- The kick flashes the photon ring and pulses the jets, and sends a shock ring out from the hole (ADR-1065's
  `post/shock`, centred on the hole's projected position).
- No fog; the cost is the lens, in the scene pass.

### 2. The Breathing Deep (organic)

**Thesis.** A colossal fungal organism hangs from the roof of a flooded cavern and breathes with the bass; melodies
climb its hanging threads as light.

| | |
|---|---|
| background | the cavern's far wall dissolving into blue-black haze; stalactite silhouettes; a few faint lichen colonies |
| midground | the organism: an inverted cap like a chandelier, its gill ring glowing underneath, hanging hyphae threads falling to a black pool |
| foreground | a wet rock lip in the lower left with small glowing caps, soft with depth of field; spores close to the lens |
| focal | the underside of the gill ring, warm, on the upper right third |
| secondaries | light pulses climbing the threads; wall colonies; the pool's faint reflection of the gills |
| atmosphere | humid haze (the fog march, low density); spores catching the gill light (particle scatter) |
| post | soft bloom, warm halation on the gills, depth of field, vignette |
| camera | looking up from the pool's edge; a slow push-in and tilt over 90 s |

**Palette.**
- dominant: teal-black `#071a1c`;
- secondary: wet umber stone `#241a12`;
- accent: bioluminescent cyan-green `#46f0c0`, on the threads and colonies;
- highlight: amber spore-gold `#ffb85c`.

Light lives only on the living things (pass 2's lesson: backlit tissue glowing from inside). The focal gill glow is
the only warm area.

**Motion.**
- very slow: the camera, and the haze drifting;
- medium: the organism's breath, the threads swaying, spores drifting;
- fast: pulses climbing the threads, the gill flash;
- extremely fast: spore glints, and the kick's contraction.

**Instrument.**

| class | signal | what the world does |
|---|---|---|
| sustained | `response.sustain`, `notes.held` | the gill ring blooms and the cavern's air warms; spores thicken slowly |
| melodic | `notes.noteOn` + `notes.lastPitch` | a pulse of light climbs the thread its pitch picks (the threads hang left to right, low to high) |
| interval | `notes.step` | a rising melody's pulses climb up; a falling melody's run down toward the pool |
| bass | `response.bass` | the organism breathes: the cap swells (`breathing`) and the pool ripples |
| kick | `response.kick` | a contraction: the cap tightens fast and relaxes on a spring, and a puff of spores leaves the gills |
| snare | `response.snare` | the gill ring flashes and releases a spore burst |
| hat | `response.hat` | spore glints |
| polyphony | `notes.polyphony` | how many threads are lit at once |
| velocity | `notes.lastVelocity` | pulse brightness |
| release | `notes.release` | a long note leaves the thread glowing, a short one does not |
| timbre | brightness, roughness | the spores go from amber to cyan; distortion sets the gills rippling |

**Build.**
- The organism: procedural lathe tubes and an SDF gill ring in polar repeat.
- Hyphae as tubes, with `pulsingVeins` and `organicPulsation` on entity owners.
- `breathing` on the cap.
- Spores as particles with scatter; the wall as an SDF with noise; the fog march.
- **Cost class: medium** (the march). Needs the per-note signals for thread placement, or a pitch bump as the
  fallback.

**Kill if** it reads as the old garden underground. It must read as a single enormous creature, not a field of
mushrooms.

**As built** (`scenes/breathing_deep.py`):
- It reads as one creature: an inverted cap whose gill ring (the hymenium) is the only warm light, eleven threads
  hanging from its rim into the haze over a black pool, a wet lip with small glowing caps in the lower left.
- Each note lights its own thread (low to high, left to right) and a pulse climbs it; a chord shimmers through all of
  them. The kick contracts the cap on a spring; the snare flashes the gill ring and sheds spores; the bass breathes
  the cap and sways the threads.
- Sustain heats the ring. Its gain is capped: dense held playing burnt it out to a white ellipse and lost the gills.
- The cavern is an SDF meshed once at load (surface nets, 128 cells a side). Raymarched every frame it cost 24 ms at
  1080p; the march now carries only the haze, at 16 steps.

### 3. Tesla Choir (energy)

**Thesis.** Twelve Tesla towers stand in a ring round a humming core, one for each pitch class; every chord is drawn
in lightning, so harmony has a shape.

| | |
|---|---|
| background | a vast dark turbine hall, ribbed walls, high windows streaked with sodium light, ozone haze |
| midground | twelve copper coil towers on a circle, ordered round the circle of fifths so a consonant chord draws a compact shape; the core at the centre with a toroidal top-load |
| foreground | cables snaking across wet concrete; a railing silhouette in the lower right |
| focal | the core's crown and the arcs leaving it |
| secondaries | tower crowns crackling; sparks; the floor's reflections of the flashes |
| atmosphere | haze, so arcs and flashes light the air; dust |
| post | bloom, a chromatic tick on discharges, grain, hard contrast |
| camera | low and wide, a slow dolly round a quarter of the ring over 100 s |

**Palette.**
- dominant: steel blue-black `#0a0e14`;
- secondary: oxidised copper `#6a3b22`;
- accent: sodium window light `#ff9a3c`, the only warm thing;
- highlight: arc violet-white `#d6c8ff`.

**Motion.**
- very slow: the dolly, and the haze;
- medium: the core's hum (bass), the coils' glow;
- fast: arcs per note;
- extremely fast: the kick's discharge, the snare's crackle, sparks.

**Instrument.**

| class | signal | what the world does |
|---|---|---|
| melodic / chords | pitch class (per-note signals) | each held note arcs from the core to its tower; a triad draws a triangle of lightning, a cluster a jagged fan |
| velocity | `notes.lastVelocity` | arc brightness and thickness |
| sustained | `notes.held`, `response.sustain` | a held note is a steady writhing arc; a staccato note is a single crack |
| bass | `response.bass` | the core charges (`chargeUp`) and hums brighter |
| kick | `response.kick` | `discharge`: bolts from the core to every tower, and the hall flashes |
| snare | `response.snare` | `electricField` crackle over the core's cage |
| hat | `response.hat` | sparks fall from the tower crowns |
| tension | `notes.tension` | dissonant chords make the arcs jagged and flickering |
| timbre | roughness, brightness | arc jaggedness; arc colour from violet to blue-white |

**Build.**
- Procedural coil towers.
- `arc` effects from the core to each tower, with routed intensity.
- `discharge`, `chargeUp` and `electricField`; haze; particles.
- **Cost class: medium.**
- **Needs pitch-class lanes** (asked of engineering, A.2). The fallback places towers by pitch across the played
  register instead of by pitch class.

**Kill if** the arcs read as random lightning rather than as the chord's shape.

**As built** (`scenes/tesla_choir.py`):
- The towers stand on the circle of fifths, and each held note arcs from the core to its pitch class's tower (the
  per-note signals of ADR-1062), so a triad draws a narrow triangle of lightning and a cluster a jagged fan.
- A held note is a steady writhing arc and a staccato note one crack. The kick discharges bolts to the ring, the
  snare crackles over the core's cage, and the hats spit sparks from the crown.
- The hall is one SDF; the haze march carries the arcs' light; the sodium windows are the only warm colour.

### 4. The Corrupted Cathedral (digital and glitch)

**Thesis.** A cathedral drawn in lines of light decays into data under the drums and rebuilds itself on held
chords.

| | |
|---|---|
| background | the apse and its rose window far down the nave, the lines dimmed by distance |
| midground | the nave's arcades in the SDF line look, bay after bay |
| foreground | two piers framing the view, very close and soft |
| focal | the rose window: brightest, centred on the axis, high |
| secondaries | bays that light along the nave by pitch; floating fragments of tracery |
| atmosphere | thin cold haze; square "data dust" motes |
| post | the mosh, block glitch, directional RGB split and scanlines, each rationed to hits; bloom |
| camera | a slow walk down the axis (one-point perspective), integrated on the rhythm's pace |

**Palette.**
- dominant: black;
- secondary: deep violet haze `#120a24`;
- accent: corruption magenta `#ff2fd0`, which appears only where something breaks;
- highlight: cold cyan-white `#bff8ff`.

**Motion.**
- very slow: the walk, and the rose window's rotation;
- medium: the nave's sway (bass), reconstruction;
- fast: bay lighting per note;
- extremely fast: corruption blocks, fracture, scanline flicker.

**Instrument.**

| class | signal | what the world does |
|---|---|---|
| sustained | `response.sustain`, `notes.held` | reconstruction: the lines sharpen, the corruption heals, the rose window completes |
| melodic | `notes.noteOn` + `notes.lastPitch` | the bay at the pitch's place along the nave flares (low notes near, high notes far) |
| bass | `response.bass` | the structure sways (the SDF warp) and the lines thicken |
| kick | `response.kick` | a structural fracture: a Voronoi displacement pulse and a screen shock |
| snare | `response.snare` | frame corruption: mosh blocks, a channel shift and a tear |
| hat | `response.hat` | scanline and pixel flicker |
| velocity | `notes.lastVelocity` | glitch intensity |
| density | `notes.density` | fragmentation: smaller blocks, more of them |
| timbre | roughness | a rough sound leaves permanent decay |

**Build.**
- A compiled SDF nave (repeat and polar repeat) with the line look (ADR-1047).
- `temporal/mosh`, plus the engineer's `post/glitch`, `split` and `display` (ADR-1064).
- **Cost class: medium.**

**Kill if** the glitch reads as an always-on filter. Menkman's rule: it must break something readable.

**As built** (`scenes/corrupted_cathedral.py`):
- The nave reads as planned: one compiled SDF of repeats and polar repeats, drawn by the line look in cold cyan, the
  rose window the brightest thing on the axis.
- **The kick no longer fractures the stone.** A Voronoi displacement over the whole nave, evaluated at every march
  step, cost most of a 92 ms SDF pass at 1080p. The kick now breaks the image instead: it splits along the nave and a
  shock runs out from the rose (ADR-1065's `post/split` and `post/shock`).
- The snare moshes and tears the frame; roughness drips the glass's light down it as sorted pixels; held chords heal
  everything (the lines sharpen, the sort recedes). The glitch stays rationed to hits, so it breaks something
  readable.
- It is the heaviest scene: the line look needs the raymarch (a meshed SDF has no edge look).

### 5. Ferrofluid Crown (abstract)

**Thesis.** A black magnetic liquid listens: each note pulls a spike from its surface, and harmony shapes the crown.

| | |
|---|---|
| background | studio darkness; one long curved softbox reflection and a thin cool rim strip |
| midground | the ferrofluid in a shallow black dish, rising into a crown of spikes round a central spiral cone (Kodama's *Morpho Towers*) |
| foreground | the dish's lacquered rim, soft with depth of field |
| focal | the tallest spike's tip catching the key highlight |
| secondaries | the ring of smaller spikes; droplets thrown off on hits |
| atmosphere | none (clean air); a trace of haze only to carry the key |
| post | macro depth of field, gentle bloom on speculars, a vignette, a near-monochrome grade |
| camera | a macro view, locked off with a slow breathing drift over 60 s |

**Palette.**
- dominant: glossy black `#020203`;
- secondary: graphite reflections;
- accent: a warm key reflection `#ffae5a` against a cool rim `#7fb6ff` (a temperature split);
- highlight: white speculars.

Colour comes only from reflected light.

**Motion.**
- very slow: the camera, and the crown's rotation;
- medium: the surface's swell (bass);
- fast: spikes rising and melting;
- extremely fast: surface shimmer (hats), a ripple ring (kick).

**Instrument.**

| class | signal | what the world does |
|---|---|---|
| melodic | `notes.noteOn` + pitch | a spike rises at the place its pitch picks round the crown |
| velocity | `notes.lastVelocity` | spike height and sharpness |
| sustained | `notes.held` | a held note's spike stays up; at note-off (`notes.release`) it melts back, slowly after a long note |
| chord | `notes.polyphony`, `chord` | the crown forms: many spikes at once, the central cone rising |
| bass | `response.bass` | the magnetic field: the whole surface heaves and the spikes lengthen together |
| kick | `response.kick` | a ripple ring races across the dish and every spike jolts |
| snare | `response.snare` | spike tips splash: black droplets |
| hat | `response.hat` | fine shimmer on the surface |
| tension | `notes.tension` | dissonance splits the spikes into thorns |
| timbre | brightness, roughness | the key warms or cools; a rough sound makes the surface granular |

**Build.**
- A compiled SDF: smooth unions of cones in a polar arrangement over a displaced plane, with glossy black surfaces.
- Rect lights for the softbox reflection; particles for droplets.
- **Cost class: medium** (SDF).
- Voice slots (asked of engineering) would give each held note its own spike. The fallback uses pitch bumps.

**Kill if** it reads as chrome blobs rather than as a material with a will.

**As built** (`scenes/ferrofluid_crown.py`):
- It reads as a material: glossy black, seen only through the long warm softbox's and the thin cool strip's
  reflections, after Kodama.
- Twelve note spikes wait below the surface round the crown, each a named SDF node raised by its own pitch place, so
  a melody pulls spikes up one after another and a held note keeps its spike up. The field's small spikes are rings
  of polar-repeated cones graded by radius, never a grid.
- The hat's shimmer is particles (glints on the skin), not a noise over the liquid: evaluated at every march step,
  that noise cost a tenth of the frame. Dissonance swells the surface instead.

### 6. Cymatic Plate (abstract and scientific)

**Thesis.** Black sand on a resonating plate: every note sings its own standing-wave figure into the sand, and a
chord is two figures at war.

| | |
|---|---|
| background | black void; one cold rim light grazing the plate from behind |
| midground | a square steel plate seen from 60 degrees above, the sand gathered on its nodal lines |
| foreground | the plate's near edge and the driver bolt at its centre, soft |
| focal | the figure's densest crossing |
| secondaries | sand jumping off the plate on hits; fine dust in the light |
| atmosphere | dust motes in the rim light (particle scatter) |
| post | crisp, a slight bloom on the sand, a near-monochrome warm-and-cold grade |
| camera | high, rotating slowly round the plate over 120 s |

**Palette.**
- dominant: matte black steel `#0b0b0c`;
- secondary: bone sand `#d9cdb6`;
- accent: the cold rim `#9cc6ff`;
- highlight: sand glints.

**Motion.**
- very slow: the camera's rotation;
- medium: the figure morphing between modes as the melody moves;
- fast: sand leaping on notes;
- extremely fast: glitter (hats).

**Instrument.**

| class | signal | what the world does |
|---|---|---|
| melodic | `notes.lastPitch` / pitch | the mode numbers (n, m): low notes draw simple figures, high notes intricate ones (Chladni) |
| chord | lowest and highest notes, `notes.range` | two modes superposed, so the figure fights itself |
| velocity | `notes.lastVelocity` | sand brightness, and how high it leaps |
| sustained | `notes.held` | a held note sharpens the figure (the nodal lines narrow); silence lets the sand scatter into noise |
| kick | `response.kick` | the whole bed leaps and settles |
| snare | `response.snare` | a burst of sand from the nodes |
| hat | `response.hat` | granular glitter |
| bass | `response.bass` | the plate flexes |
| tension | `notes.tension` | the figure jitters |

**Build.**
- A procedural plate with a material program that computes the Chladni nodal pattern.
- The mode numbers are program constants, which are routable (`op/<i>/<kind>/constant`).
- Particles for the leaping sand.
- **Cost class: light.**

**Kill if** the pattern reads as a screensaver rather than as sand on steel.

**As built** (`scenes/cymatic_plate.py`):
- The figure is a material program: Chladni modes built from the palette op's cosines, the mode numbers routed by
  pitch, so a melody re-forms the sand continuously; a chord superposes its lowest and highest notes' modes.
- The kick throws sand off the plate, the snare bursts it from the nodes, the hat glitters; a held note sharpens
  the lines.
- The key is a pool rather than a flood (an 8 to 19 degree cone), so the figure's centre is the brightest place and
  the corners sink toward the void.

### 7. Storm Cell (atmospheric, violent)

**Thesis.** A supercell walks across the night prairie: the bass winds its funnel, the snare strikes, and the melody
crawls as light inside the cloud.

| | |
|---|---|
| background | the wall cloud and anvil, dark, lit from inside by lightning |
| midground | the tornado funnel right of centre with its debris skirt; power poles receding |
| foreground | a wheat field's silhouette bending in the gusts; a fence line |
| focal | the funnel's contact with the ground |
| secondaries | a sodium farm light far left; rain curtains; in-cloud flashes |
| atmosphere | rain streaks, haze, dust |
| post | grain, a desaturated green-grey grade, exposure kicks on strikes |
| camera | low on the ground, nearly static, with a thunder nudge |

**Palette.**
- dominant: storm green-grey `#2c372f`;
- secondary: bruised violet `#3b3046`;
- accent: the sodium farm light `#ffa24a`;
- highlight: lightning blue-white `#dfe9ff`.

**Motion.**
- very slow: the cloud's rotation and the funnel's drift;
- medium: the wheat and the rain;
- fast: in-cloud flashes;
- extremely fast: strikes and thunder.

**Instrument.**

| class | signal | what the world does |
|---|---|---|
| bass | `response.bass` | the funnel's intensity, rotation and width; the wind in the wheat |
| kick | `response.kick` | thunder: a flash through the haze, a gust, a camera nudge |
| snare | `response.snare` | a lightning strike (`lightning`, a live trigger) with a ground flash |
| hat | `response.hat`, `hatRate` | rain density and streak length |
| melodic | `notes.noteOn` + pitch | light crawls inside the cloud base at the pitch's place, left to right |
| sustained | `response.sustain` | the cloud glows from within; the storm feeds |
| density | `notes.density` | the storm's chaos: turbulence and debris |
| velocity | `notes.lastVelocity` | strike brightness |

**Build.**
- The `tornado` effect, `lightning` (live triggers), rain particles with splash, procedural wheat with wind, point
  lights inside the cloud fog.
- **Cost class: heavy.** The march and the tornado; the Preview tier is required.

**Kill if** the funnel cannot be made to read as a funnel at the live tier.

**As built** (`scenes/storm_cell.py`):
- The funnel reads. It is the Effect Library tornado, dense and mostly absorbing (scattering 0.18), so it stands
  dark on the clear slot. The march carries the medium alone (`volumeMaxDistance` 0).
- The snare's strike lights the slot, not the sky: any change to the procedural sky rebuilds its lighting cube.
- The wheat is 4,400 instanced lathed stalks with a wind deformer.
- The lightning's flash is ranged short of the camera (900 m for a strike ~1 km off). With a range that held the
  camera, its light lit the funnel's medium in hard screen-tile rectangles: an engine defect, reported.
- A cloud-base ceiling (a lit plane at the cloud base) was tried and dropped: `fogSky` melts every surface past
  1.8 km into the sky behind it, so it never showed.

### 8. Salt Flat Mirage (atmospheric, lonely)

**Thesis.** On an endless salt flat at dusk the sky is the instrument: chords paint it, melodies fall through it as
meteors, and the bass makes the horizon shimmer.

| | |
|---|---|
| background | a dusk gradient sky, a thin far mountain range, a rising moon |
| midground | a mirage band where the horizon melts (`heatShimmer`), the mountains reflected in it |
| foreground | cracked salt polygons running to the horizon; a single black monolith on the left third casting a long shadow |
| focal | the monolith's silhouette against the brightest part of the sky |
| secondaries | meteors; a dust devil; the moon |
| atmosphere | horizon haze; blowing salt |
| post | halation, grain, a cool-dusk grade with a warm horizon |
| camera | an ultra-wide, almost static frame (a lonely shot); a tiny drift |

**Palette.**
- dominant: dusk lavender sky and pale salt `#8a86b3` / `#e7e1d8`;
- secondary: the rose horizon band `#e9a49a`;
- accent: the black monolith;
- highlight: meteor and moon white.

**Motion.**
- very slow: the sky's gradient and the moon;
- medium: the shimmer, blowing salt;
- fast: meteors;
- extremely fast: distant lightning, a ground ripple.

**Instrument.**

| class | signal | what the world does |
|---|---|---|
| sustained / chords | `response.sustain`, `notes.chord` | the chord paints the sky: the palette moves through dusk states, and tension sets how far it goes |
| melodic | `notes.noteOn` + pitch | a meteor falls at the pitch's place in the sky (high notes high and fast) |
| bass | `response.bass` | heat shimmer on the horizon; the air thickens |
| kick | `response.kick` | a wind ripple across the salt (a ground pulse) |
| snare | `response.snare` | lightning far behind the mountains |
| hat | `response.hat` | salt grains blowing across the flat |
| velocity | `notes.lastVelocity` | meteor brightness |
| release | `notes.release` | a long note's meteor leaves a lingering trail |

**Build.**
- A procedural ground with a Voronoi crack program; `heatShimmer`; `meteorShower` or comet-like particles;
  `lightning`; the procedural sky driven by the palette (ADR-1043).
- **Cost class: light** (no march).

**Kill if** the frame is empty rather than lonely, with nothing to look at between events.

**As built** (`tools/sonic_vfx/scenes/salt_flat.py`). The departures, most of them forced by something the engine
cannot do:
- **The sun stands behind the monolith's right edge.** The monolith is the silhouette against the brightest sky, and
  its shadow runs at the camera as a dark wedge from the frame's foot: the long shadow became the composition's
  leading line.
- **The snare flares the monolith's outline instead of distant lightning.** A bolt from a clear dusk sky reads
  wrong, and the scene had no cloud. The monolith is the one made thing on the ground, so it answers the drums:
  the snare flares its outline, and the kick sends a ring of light out from its base.
- **No moon.** A masked program's crescent still writes its whole disc to the depth prepass, so the clear colour
  showed through.
- **The salt polygons** came with ADR-1069's `voronoiEdge` (Worley F2 - F1): white ridges between cells about 1.3 m
  across, flattened onto the ground and fading out by 65 m before they could alias. The crust underfoot is darkened
  by camera distance, so the frame reads dark foreground, bright horizon band, deep zenith (the evaluator had
  measured muddy midtones).
- **The mirage:** puddles and a far sheet of standing water mirror the sky, which makes the range float on a line
  of light, and the heat shimmer wobbles that line.

## Tier B

### 9. Stellar Nursery (cosmic)

**Thesis.** Inside a pillar of gas, chords ignite newborn stars: their number is the polyphony, their colour the
pitch, and the bass is the density of the cloud.
- **Composition.**
  - Three dust pillars (placed fog banks), their tips glowing magenta.
  - A nebula wall behind (the Cosmic Ocean).
  - Dark dust lanes close to the lens.
  - Protostars (`plasma`) embedded in the tallest pillar's head, which is the focal point.
  - The camera drifts sideways, so the pillars separate in parallax.
- **Palette.**
  - dominant: teal gas `#1f6f73`;
  - secondary: rust dust `#8a4a24`;
  - accent: magenta rims `#ff4fa0`;
  - highlight: blue-white stars `#bfe0ff`.
- **Motion.** The pillars drift over minutes, the stars breathe over beats, the jets fire on notes, and shocks
  cross on kicks.
- **Instrument.**
  - polyphony: how many protostars are lit;
  - pitch: star temperature (red to white to blue);
  - note duration: the star grows;
  - bass: the gas's density and glow;
  - kick: a shock front through the gas (`shockwave`);
  - hat: twinkle;
  - sustained: ionisation glow spreads;
  - tension: the cloud churns.
- **Build.** `volumetricFog` banks, Cosmic Ocean, `plasma`, `shockwave`. **Cost class: heavy.**
- **As built** (`scenes/stellar_nursery.py`):
  - The pillars are one SDF of lobed, noise-skinned capsules, and the nebula is a matte emissive wall: the Cosmic
    Ocean was removed (ADR-441), and fog banks are one medium slot each.
  - Two magenta rim lights from above and behind replace the ionization glow.
  - A chord lights embryonic stars, each its own SDF surface: as many as the chord has voices, red for low pitches
    and blue-white for high.
  - The kick's shock is the post `shock` instrument (ADR-1065), centred on the nest.
  - **Cost class: medium.**

### 10. Abyssal Bloom (underwater)

**Thesis.** Two kilometres down, a colony of siphonophores answers the melody in pulses of cold light while marine
snow falls through the hats.
- **Composition.**
  - Black-blue water.
  - A long siphonophore chain (a tube with pulsing lights) diagonal across the frame as the subject.
  - Jellyfish bells (bubbles and plasma) at different depths.
  - Marine snow; a whale's silhouette very far off, barely there.
  - The camera descends slowly.
- **Palette.**
  - dominant: abyssal blue-black `#01060f`;
  - secondary: deep teal `#06323a`;
  - accent: magenta `#ff4fd8`;
  - highlight: cyan bioluminescence `#4ff6ff`.
- **Instrument.**
  - melody: a pulse runs along the chain to the pitch's place;
  - pitch: also the depth at which a jellyfish answers;
  - sustained: the colony glows;
  - bass: the current swells and everything drifts;
  - kick: a pressure wave (`ripple`);
  - snare: an ink-dark flash;
  - hat: marine snow sparkles;
  - polyphony: colony size.
- **Build.** Tubes with `pulsingVeins`, `bubble`, `plasma`, particles, dense absorbing fog. **Cost class: medium.**
- **As built** (`scenes/abyssal_bloom.py`):
  - The colony is 40 lathed zooids distributed along a spline, with a stem tube.
  - Their light is a band placed by world position along the stem: a program's gradient op, since
    `instanceIndex` was not reliable there.
  - The jellyfish are lathed bells with radial strand tentacles. The one at the pitch's depth answers (pitch
    places).
  - The kick's pressure wave is the post `shock` instrument.
  - Second pass, after the matrix (the first read as dots in black water): 56 bigger zooids, each trailing hanging
    palps lit by the same band program; the jellyfish larger and nearer, their bells translucent (a Fresnel rim
    program on the routed emission); brighter snow; a stronger note band and jelly answer, so the magenta accent
    is on screen when a jelly answers.

### 11. Silk Theatre (abstract)

**Thesis.** Silk ribbons draw the melody in the air of an empty theatre: pitch is height, legato is one unbroken
stroke, and the kick cracks the ribbons like whips.
- **Composition.**
  - A black stage with a polished floor and a single top spot.
  - A crimson velvet curtain falling into darkness behind.
  - The ribbons (particle trails and `trail` effects on orbiting owners) as the subject.
  - The camera orbits slowly at stage height.
- **Palette.**
  - dominant: velvet dark `#1a0306`;
  - secondary: scarlet silk `#d01a2a`;
  - accent: gold `#ffbf5a`;
  - highlight: white sheen.
- **Instrument.**
  - melody: the ribbon leader's height follows the pitch, and a new stroke starts at each phrase;
  - legato: long continuous ribbons, against staccato's short flicks;
  - velocity: ribbon width;
  - bass: the whole dance sways;
  - kick: a whip-crack (an XFORM `shake`, and a ribbon snap);
  - hat: glitter along the ribbons;
  - chord: parallel ribbons.
- **Build.** Particle ribbons, `trail`, `orbit`, `spiral`, a spot light. **Cost class: light.**
- **As built** (`scenes/silk_theatre.py`):
  - The ribbons are not particle trails. Those are capped at 32 points and their length depends on the frame rate,
    so they drew short angular sticks.
  - Instead, the silk is a stroke laid in the air: particles born where the hand is while a note sounds, left
    there, overlapping into one band that sags and fades over five seconds.
  - The kick throws red shreds and shudders the stroke; the snare throws gold shreds.
  - Second pass: the stroke read as a thick tube. The hand is now a short vertical bar (a box emitter), so the laid
    particles weave a band a hand's breadth wide, and a keyed track narrows the bar to its edge and back, so the
    band twists along its length like a ribbon. The stage went dark (thinner haze, a dimmer wash), the spot leaves
    a pool on the floor, and the rim light's source left the frame (it showed as a red dot). No note brightens the
    whole stage any more (the evaluator's "whole frame answers").

### 12. Aurora Tundra (atmospheric, cold)

**Thesis.** Over a frozen lake the aurora is the pad: chords hang curtains of light, the melody ripples them, and
the hats are diamond dust glittering in the air.
- **Composition.**
  - A low horizon with a black spruce silhouette line.
  - Pressure ridges in the lake ice as the foreground.
  - The aurora as the subject, filling the upper two thirds.
  - Stars; a camera looking up, panning slowly.
- **Palette.**
  - dominant: arctic navy `#071226`;
  - secondary: ice blue-white `#cfe6f5`;
  - accent: violet curtain tops `#b07cff`;
  - highlight: aurora green `#5cff9a`.
- **Instrument.**
  - sustained: curtain brightness;
  - chord tension: the curtain colour (green to violet, through the palette);
  - melody: ripples travelling along the curtains;
  - bass: the curtains' height and drop;
  - kick: a meteor;
  - snare: the ice cracks with light;
  - hat: diamond dust.
- **Build.** `aurora`, `stars`, `meteorShower`, ice with a crack program, particles. **Cost class: light.**
- **As built** (`scenes/aurora_tundra.py`):
  - The aurora's own spectrum response is off (driven by the spectrum it would be a visualizer); routes drive it.
  - Tension and polyphony move a four-state palette.
  - The pressure ridge was cut: box slabs read as boards. The foreground is black ice crazed with white fractures,
    which the snare lights, as on Baikal.
  - Second pass: long cracks between the ice's plates (`voronoiEdge`, ~8 m plates) join the fine crazing, and a band
    of the curtains' own colour lies on the far ice (its colour bound to the palette's low role, its gain routed by
    sustain and the kick). Spread over all the ice, that reflection read as a lit green floor: a fake reflection
    cannot follow the curtains, so it is kept to a band under the horizon.

### 13. Ember Forest (organic, after the fire)

**Thesis.** The morning after the fire: the bass is the wind breathing on the embers, the snare a flare-up in the
black trunks, the melody one ember dancing through the smoke.
- **Composition.**
  - Black trunk silhouettes in receding planes.
  - Smoke layers, the ground glowing in seams.
  - A single dancing ember with a trail as the subject.
  - The horizon's fire glow behind the last trunk plane; the camera tracks sideways.
- **Palette.**
  - dominant: soot black `#070505`;
  - secondary: smoke grey-brown `#3a3230`;
  - accent: ash white;
  - highlight: ember orange to yellow-white `#ff6a00` / `#ffe6a0`.
- **Instrument.**
  - bass: a wind gust fans the embers (particles and ground seams);
  - snare: a flare-up in a trunk (`discharge`-like burst, `glow`);
  - hat: sparks;
  - melody: the dancing ember's path (pitch is height);
  - sustained: the horizon glow builds;
  - kick: a trunk cracks and throws embers;
  - velocity: flame brightness.
- **Build.** Procedural trunks with a seam program, smoke fog banks, particles, `heatShimmer`. **Cost class: medium.**
- **As built** (`scenes/ember_forest.py`):
  - Three instanced trunk planes, ash with glowing coal seams (a program), and thin smoke with shafts.
  - The fire front is an emissive, noise-topped band behind the last trees.
  - The bass stirs the smoke's structure, never its density: a density route drowned the trunks.
  - The coal seams are broken into live lengths and dead ones (an unbroken contour read as a map's line), and the
    sustained glow lifts the sky again: ADR-1070 made a slow sky route cheap live.

### 14. Lantern Lake (atmospheric, warm)

**Thesis.** Each note releases a paper lantern from the shore of a still lake; the melody becomes a drifting
constellation, and the bass a slow swell beneath it.
- **Composition.**
  - A dusk lake (a terrain-water surface reflecting the sky).
  - A far shore of hills.
  - Lanterns rising and drifting as the subject.
  - A jetty in the foreground; the camera low over the water, pushing slowly.
- **Palette.**
  - dominant: dusk indigo `#141a3a`;
  - secondary: slate lake `#20283f`;
  - accent: the jetty's black;
  - highlight: lantern amber `#ffb050`.
- **Instrument.**
  - note: a lantern released at the pitch's place along the shore, its brightness from velocity;
  - chord: a cluster;
  - sustained: lanterns glow brighter and rise slower;
  - bass: the swell, and the wind on the lanterns;
  - kick: ripples on the water;
  - hat: fireflies;
  - release: a long note's lantern climbs higher.
- **Build.** Particles with collision and pulse, `halo`, water. **Cost class: medium.**
- **As built** (`scenes/lantern_lake.py`):
  - The engine has no planar reflection, so the reflection is built:
    - twins of the hills (turned 180 degrees about X: a procedural cannot be scaled negative), the jetty and the
      lanterns below the waterline;
    - under them, an unlit plane painted with the sky's gradient flipped (view direction, a Fresnel falloff,
      ripple bands).
  - A heat-shimmer column under the waterline wobbles the mirror world: the bass swells it and the kick ripples
    it.
  - Second pass: the shimmer alone was invisible on a smooth gradient (the matrix measured the kick silent). The
    kick now sends a ripple ring across the water from the jetty's end: the mirror program finds where the view ray
    crosses the waterline (the plane's point lifted back up the ray by the mirror's depth) and draws a band at a
    radius the kick's linear-fall envelope moves outward, fading as it spreads. The bass deepens the reflection's
    long bands; fireflies blink along the jetty instead of beside the lens.

### 15. Feedback Mirror (digital, feedback-driven)

**Thesis.** A single glyph of light inside a video feedback loop: the music turns, zooms and recolours the loop, so
the world is the instrument's own echo.
- **Composition.**
  - One luminous glyph (SDF or text) slightly off centre.
  - The feedback tunnel of its echoes receding into a logarithmic spiral (Crutchfield).
  - Negative space round it.
- **Palette.** A deep violet ground with a gold glyph. The hue rotation is controlled by the feedback's own hue
  step, never random.
- **Instrument.**
  - sustained: the feedback's decay (the echoes last);
  - melody: rotation (each step turns the spiral);
  - bass: zoom;
  - kick: a zoom punch;
  - snare: a channel swap;
  - hat: grain;
  - chord: the hue step.
- **Build.** The engineer's `temporal/feedback` (ADR-1065), SDF, text. **Cost class: light.** Waits on ADR-1065.
- **As built** (`scenes/feedback_mirror.py`):
  - A sigil in SDF (a ring, a triangle with beads, an inner ring, a core) on the right third.
  - `temporal/feedback` (ADR-1066) with 24 taps, zoom 0.972 and a slow turn, so its echoes curl into a nautilus
    spiral toward the frame's centre.
  - The interval turns the spiral, the bass deepens it, the kick punches it, and chords walk its hue from gold
    toward magenta.

### 16. Datascape (digital, data)

**Thesis.** A landscape of pure measurement, barcode ridges and numerals to the horizon, written and erased by the
music.
- **Composition.**
  - A black plane of white data strips receding to a vanishing point.
  - A low gliding camera.
  - Numerals standing like monoliths.
  - Strict monochrome with one red accent at the peaks (Ikeda).
- **Instrument.**
  - rhythm: the glide's speed (integrated);
  - notes: strips written at the pitch's place across the plane;
  - kick: a one-frame inversion (flash-limited);
  - hat: digit flicker;
  - bass: the ridges heave;
  - sustained: the strips settle into order.
- **Build.** SDF strips with the line look, text, `post/display`. **Cost class: light.**
- **As built** (`scenes/datascape.py`):
  - The strips are a material program on a heaving plane: three cosines of incommensurate periods, summed and
    thresholded high, give exact barcode lines that are never organic, and two more cosines give the dashes.
  - A note writes a bright strip at its place. Red appears only on high crests.
  - The glide is the integrated level.
  - The kick's shock and the snare's tear are post instruments (ADR-1065).

**Kill if** it reads as a user interface or a spectrum display. It is the riskiest concept in the catalog.

---

## Coverage and distinctness

| scene | space | technique | camera | palette family |
|---|---|---|---|---|
| Event Horizon | void, an object at distance | DF lens, particles | arc | black / ember |
| Breathing Deep | enclosed cavern, looking up | procedural, FXL, fog | push and tilt | teal-black / amber |
| Tesla Choir | interior hall | arcs, discharge | low dolly | steel / violet-white |
| Corrupted Cathedral | interior nave, one-point | SDF line look (nave and rose as two objects), mosh, split, sort | walk | black / cyan / magenta |
| Ferrofluid Crown | macro studio | SDF smooth unions | locked macro | black / reflections |
| Cymatic Plate | top-down plate | material program | rotation | black / bone |
| Storm Cell | prairie | tornado, lightning, rain | static, low | green-grey |
| Salt Flat Mirage | ultra-wide landscape | the palette-driven sky, Worley salt polygons, shimmer, meteors | static | lavender / salt |
| Stellar Nursery | volumetric pillars | meshed SDF pillars, a painted nebula, star spheres | parallax | teal / rust / magenta |
| Abyssal Bloom | underwater | lathed zooids and palps on a spline, a band program, rim-lit bells | descent | blue-black / cyan |
| Silk Theatre | stage | a laid particle band (twisting), a spot in haze | orbit | crimson / gold |
| Aurora Tundra | night landscape | aurora | pan up | navy / green / violet |
| Ember Forest | forest | trunks, embers | tracking | soot / ember |
| Lantern Lake | lake | particles and their twins, a built mirror with a ripple ring | push | indigo / amber |
| Feedback Mirror | abstract | temporal feedback | static | violet / gold |
| Datascape | data plane | a cosine barcode program, text, post shock and tear | glide | white on black / red |

The catalog's own measure of distinctness: no two scenes share more than one of space, technique, camera and palette
family.
