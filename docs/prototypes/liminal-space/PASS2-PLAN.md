# All You Got, art pass 2: the director's plan

*The art agent, 2026-10-01. The owner's brief for this pass is `02-art-pass-2.md`. It governs, and it overrides
the earlier documents where they differ. In particular, large empty spaces are forbidden, the look returns to
luminous line-drawn geometry on dark, and the audio reactivity must be undeniable.*

*Every time in this plan uses the owner's bar numbering: owner bar N is analysis bar N+1, and bar 0 is the
count-in.*
- **The grid, as data:** `tools/liminal/pass2_grid.py` writes `tools/liminal/all-you-got.pass2.json`. That file
  holds the tempo map, the sections, all 18 BIG CLAPs in seconds, 30 other timed events and 274 timed lyric
  entries.
- **The measuring tools:** `tools/liminal/pass2_av.py` asks "does the visual response read?" of the pixels.
  `tools/liminal/critic_pass2.py` runs the Creative Critic against this brief.*

---

## 0. The idea in one paragraph

> **A primitive simulation boots up out of black and builds a home out of light.** It draws small rooms in
> glowing lines and furnishes them with the things a life collects: a couch, a lamp, a bed, a table set for one,
> paintings, plants, a globe, and a mannequin sitting where a person would. We wander that home alone at night,
> and the song's words are written on its walls. Pressure builds until the rooms glitch. Then the home grows like
> a tree into the sky. We sit with its objects and ask "is that all you got?". The rooms start to dance, and a
> spectrum sweep throws the walls open onto a line-drawn landscape that celebrates. Then the simulation crashes
> in colour, dawn floods it, and it collapses back into the single point of light it came from.

The arc, in the owner's words (§14 and §20): **winding up → loneliness → tension → growth → reflection →
celebration → release → resolution.**

| owner bars | section | feeling | the visual idea | the rule the music changes |
|---|---|---|---|---|
| 0 | count-in | waiting | a cursor blinks on each beat in the black | — |
| 1-16 | intro | winding up, mutating | **the world compiles**: a point becomes a cube, cells divide, a lattice organises, the lattice becomes the frame of a house | every quarter note is one step of evolution; the bass adds a second, faster clock |
| 17-24 | first release | impact, then settling | **we crash into the house**: small dense furnished rooms | the beat makes the furniture land, bounce and light |
| 25-40 | verse 1 | loneliness | **the night apartment**: one room every 4 bars, words on the walls | the pulse lives in the light and the objects, not the camera |
| 41 | pause | falling off, stop | **black**: LET IT GO falls through the void | everything stops but the words |
| 42-49 | let it go | release, interruption | **the corridor of words**: LET on one wall, IT on another, GO on a third, passing on the eighths | the camera's speed is set so the words arrive on their beats |
| 50-65 | verse 2 | tension | **the apartment breaks**: the same rooms, denser, glitching | every 2 bars a BIG CLAP corrupts the simulation differently |
| 66-74 | bridge 1 | growth, elevation | **the house grows into a tree of rooms**, and the camera rises through it into the sky | each GROW blooms a new room |
| 75-82 | bridge 2 | reflection | **the room of objects**: one room, the mannequin, the house's things spinning and glowing | each eighth note turns an object; each word lights one |
| 83-90 | bridge 3 | celebration begins | **the house dances**: warm rooms, a light-up floor, furniture hopping, the camera gliding | the quarter note lives in the floor and the furniture, never the camera |
| 90.3-4 | the sweep | — | **a spectrum wave washes everything** into the final chorus | — |
| 91-114 | final chorus | release, celebration | **the open**: a line-drawn landscape, the house's rooms floating as lanterns, a staircase into the sky, the summit, the crash | the quarter note is a feel (colour, lanterns), not a pulse |
| 115-end | ending | resolution | **dawn**: the sun rises on the mannequin on the hill, the world washes to white, breaks into data and collapses into the cursor | the music has stopped, so the world stops |

---

## 1. Phase 1: what pass 1 left us

### The strongest scenes

These are the discoveries to keep. What carries over is the idea, not the pale look.
1. **The mannequin seen from behind** in a doorway two rooms ahead, then gone (verse 1). The owner singled it out.
   Pass 2 keeps it as a recurring motif and gives it a body made of the same lines as everything else.
2. **The ember loop corridor**, whose sodium ceiling lights pass overhead on every other beat. It is the only
   pass 1 shot where the music was *in the space* (Star Guitar). Pass 2 uses that device on purpose: in "let it
   go" the camera's speed is set so that the words arrive on their beats.
3. **The room that grows** at "feel it grow". Growth as a space opening up worked. Pass 2 makes it bigger and
   more literal: the house grows into a tree of rooms.
4. **Repetition as a tool.** The corridor came back, and the opening frame came back. Verse 2 revisits verse 1's
   rooms, corrupted.
5. **Two things the brief now asks for, which pass 1 already had:** the staircase motif, and the withheld sky
   (the open air was saved for the end).

### The weakest scenes

Each fails the new brief directly.
1. **The great hall** (bars 17-24): a 40 m empty room. It is exactly the §2 complaint.
2. **The long corridors and the void stairwell** (intro, "let it go", verse 2): long hall, big empty room, long
   hall.
3. **The open** (the final chorus): pale fog with doorframes floating in it. It is barren and disconnected, and
   at 94 % pale it is the opposite of "explosive colour".
4. **The thresholds of light**: one transition device used nine times, always the same.
5. **Reactivity**: pass 1 decided "subtle by design". Measured now (below), it is not perceptible.

### What is reusable

- **The infrastructure, all data:**
  - the journey camera with chapters (worlds swap at a distance, so cuts are free);
  - timeline tracks with per-key interpolation (`step` makes an exact cut) and `add`/`multiply` track modes
    (a pulse layered on a base);
  - the palette block (OKLab states, role bindings, `palette/position|value|saturation`);
  - routes with `spring` and `integrate`;
  - surfaces (8 per SDF object);
  - the SDF vocabulary (`tools/liminal_sdf.py`);
  - the post chain: bloom, `grade/hueShift`, `lens/chromaticAberration`, `lens/distortion`, halation, vignette.
- **The look's own tool:** the SDF edge term (`look/edge/intensity|width|color`). Dark fill plus bright edges is
  the original POC's language. Pass 1 deliberately muted it to a pencil line. Pass 2 turns it back up.
- **The generator's machinery** (`make_all_you_got.py`): the walk integrator and fitter, the path spline, the key
  writers, the node-count guard, the clearance test.
- **The analyzers:** `liminal_critic.py temporal` and the Critic, now with pass 2 inputs.

### Existing systems against the brief's §19 list

| system | in pass 1 | for pass 2 |
|---|---|---|
| entity effects | warp breathing, near-field tremble | keyed node transforms, surface emission and `enabled` flicker; data-mosh is the engineer's |
| audio reactivity | spring routes from bass, mid and treble | **beat-grid keys** (exact, authored per section); the engineer's beat envelopes when they land |
| camera | the journey (walk, gaze, bob, sway, FOV, look-at) | the same, plus breathing (keyed now, the engineer's additive offset later) |
| lyrics / text | **none** | the engineer's spatial typography; fallback is an SDF neon-stroke font (§11) |
| transitions | thresholds of light only | cuts on BIG CLAPs, black frames, colour wipes, the spectrum sweep (the engineer's) |

---

## 2. Phase 2: the Critic and the measurements on pass 1

**Inputs.** The pass 1 final film (`all-you-got-final.mp4`), judged against this brief:
- `critic_pass2.py` rewrote the Critic's intent with §12's questions, the owner's sections and camera language,
  and the avoid list (large empty rooms, endless halls, Glowmere and so on);
- it gave the Critic the BIG CLAPs and the quarter and eighth grids as timed events.

**A bug fixed on the way.** `liminal_critic.py` wrote the scene's `modulation` as a dict. The Critic's
reactivity analyzer iterated it as a list, which is why it failed in pass 1 ("'str' object has no attribute
'get'"). The wrapper writes a list, and the analyzer now runs. The result is job `job_1a0f80d8694ad5bee`,
**complete**.

### What the Critic measured

- **Audio reactivity: "Audio-driven routes produce no measurable picture response."**
  - The event-locked average over 268 events is z = 0.2.
  - Zero screen cells respond.
  - This is the owner's complaint, measured.
- **Camera shake and wobble:** 22 medium findings (the chapter swaps), plus 4 low.
- **Clipped highlights:** 7 high and 2 medium (the thresholds and the sun).
- **Repeated compositions:** 11 shots repeat the composition of the shot before.
- **Shots that keep going** after they stop showing anything new: 6. That is the empty-room problem as the
  Critic sees it.
- **Its dimension scores** (composition 1.0, environment 1.0, creative intent 1.0) are measurement-based. They
  cannot see "barren", "line-drawn" or "Glowmere", so §12's art questions are answered by viewing (below).

### What `pass2_av.py` measured

It measures from the pixels whether the picture moves with the owner's grid.
- **Quarter-note lock: none.**
  - In every section, the event-locked swing of brightness, lines and change over the quarter-note cycle is
    +0 to +8 dB, with a locked share of 0.01-0.10.
  - Pure noise reads about +4 dB and a share under 0.1. A pulse a viewer feels reads above +12 dB with a share
    above 0.3.
- **BIG CLAPs:** 7 of 18 "unmistakable" (z ≥ 3), and all 7 are accidents: chapter swaps and thresholds that
  happen to fall on a clap.
  - 8 do not read at all: 20.4, 36.4, 45.4, 53.4, 55.4, 59.4, 61.4, 63.4.
  - The GOT? at 78.2.5 does not read either.
- **Transitions:** the pause bar (3.9x) and bar 75 (3.9x) are the only section boundaries with a clear change.
  - "let it go" (0.2x), the bridge transition (0.2x) and the final chorus (0.2x) have none.

### §12 answered by viewing the contact sheets (`analysis/renders/final-sheet-1..3.png`)

- **Scene quality.**
  - Visually interesting? Only in places: the doorway figure and the ember corridor.
  - Barren? **Yes, almost everywhere.** No room contains an object except the figure.
  - Is geometry connected logically? Yes in the interiors. No in the open, where doorframes float.
- **Art direction.**
  - Primitive simulated universe? **No.** It reads as pale architectural visualisation, close to generic 3D.
  - Glowmere? No.
  - Palette? Pale and washed out: grey, lavender, ember, sky. It is never dark, so it can never "suddenly
    become extremely colourful".
- **Audio reactivity.** Quarter notes not perceptible; eighth notes not perceptible; BIG CLAPs not obvious;
  transitions only on the chapter swaps.
- **Camera.**
  - Purposeful? Yes, as a walker.
  - Does it vary by section? Too little: one walk everywhere.
  - Breathing? None.
- **Composition.**
  - One-point corridor perspectives repeat. The focal point is usually a distant doorway.
  - There is rarely a foreground, and there are no objects to look at.
- **Entity effects.** Almost none (a tremble nobody can see). The world never feels alive.

**Opportunities the critique names.** They are the plan below:
- furnish every room;
- make the line the material;
- write the words on the walls;
- give every musical event an authored response;
- give each BIG CLAP its own punctuation;
- vary the camera by section;
- end on colour, not fog.

---

## 3. The look: luminous line-drawn geometry on dark

- **The material is the line.**
  - Surfaces are near-black fill (albedo 0.015-0.04), so planes still read faintly.
  - Every crease and edge glows (the SDF edge term), with bloom so lines halo slightly.
  - Lines are thin and bright, with intensity 3-8, keyed per beat where the music asks.
- **Primitive by design.** Everything is a box, cylinder, cone, sphere, capsule or torus, built from the same
  vocabulary as the walls:
  - the furniture;
  - the plants (pot and cone leaves);
  - the trees (cone or sphere on a cylinder);
  - the rocks (rotated boxes);
  - the mannequin (capsules and spheres).

  That is "early primitive 3D", and it keeps one visual identity across interiors, exteriors, objects, figures
  and type.
- **Colour is where the light comes from.**
  - The edge colour per object is bound to a palette role: walls, furniture, accent.
  - Emissive surfaces are the warm or saturated accents: lamp shades, windows, the TV screen, paintings' canvases.
  - The fog is dark (near-black, tinted per section), so distance fades lines into darkness.
- **Density.** Every room is 3-6 m and holds at least 6 recognisable objects:
  - floor detail (boards or tiles, as grooves that draw as lines);
  - a window with mullions and curtains;
  - at least one painting or frame;
  - a light source (a lamp);
  - a "strange object" (a globe, a clock, a TV, a hanging mobile).
- **Not Glowmere.** No green as a default, no bioluminescent organics, no soft fantasy fog. Green appears at most
  as one accent.

### The palettes (dark; sRGB hex, linear in the project)

`V` is the void and fog, `F` the fill, `L` the lines and `A` the accents.

| key | owner bars | V | L (walls / furniture) | A | note |
|---|---|---|---|---|---|
| P0 boot | 0-8 | `#000000` | white → magenta `#FF2BD6` / cyan `#22E6FF` alternating | electric blue `#3B5BFF` | the splash is all three |
| P1 compile | 9-16 | `#05030C` | cyan / violet `#8A5CFF`, gold `#FFC23D` arriving | magenta | hues step faster with the riser |
| P2 all you got | 17-24 | `#04061A` | ice cyan `#7FF3FF` / hot magenta `#FF3FA4` | amber lamps `#FFB347` | saturated after the impact, settling |
| P3 night | 25-32 | `#020512` | ice blue `#9CC8FF` / pale cyan | warm amber `#FFB15C` (lamps, windows) | loneliness: the cold room, the one warm lamp |
| P4 ember | 33-40 | `#0E0204` | crimson `#FF3B3B` / orange `#FF8A3D` | pale gold `#FFE08A` | the deliberate turn: warmth that is not comfort |
| P5 void | 41 | `#000000` | — | white type, then hot pink `#FF4FD8` | the quick shift of tone |
| P6 violet | 42-49 | `#07020F` | violet `#B07CFF` / lavender | hot pink type `#FF4FD8` | the words are the brightest thing |
| P7 tension | 50-65 | `#010406` | teal `#22F0D0` against magenta `#FF2E9A`, swapping at the claps | acid yellow `#E8FF3A` (once) | split complementary; stronger changes |
| P8 growth | 66-74 | indigo `#0A0830` → plum `#2A0C33` → rose `#4A1530` (rising) | gold `#FFD27A` / white | peach `#FFB08A` | warming and brightening as we rise |
| P9 jewels | 75-82 | `#05020A` | dim white for the room | one jewel per object: ruby, sapphire, amber, white, rose, aqua | reflection |
| P10 dance | 83-90 | maroon `#140406` | orange `#FF7A2E` / pink `#FF4F8B` / gold | turquoise `#2EE6D6` sparks | warm, euphoric |
| P11 open | 91-106 | night-sky blue `#030820` | every hue: hills step through the wheel on the quarter | warm lanterns `#FFC86B` | celebration |
| P12 summit | 107-114 | `#08041A` | full spectrum, maximum chroma | white | the build and the crash |
| P13 dawn | 115-end | rose-gold horizon `#FF9E7A` over deep blue, then white | warm white | the sun `#FFE3B0` | wash out, then black |

---

## 4. The timeline (the owner's numbering, exact)

109 BPM from 0.00 s (a bar is 2.2018 s, a beat 0.5505 s). At owner bar 75 (165.1376 s) it steps to 111 BPM (a
bar is 2.1622 s, a beat 0.5405 s). Owner bar N, beat b is at `N x 2.2018 + (b-1) x 0.5505` before bar 75, and
at `165.1376 + (N-75) x 2.1622 + (b-1) x 0.5405` from it.

| section | owner bars | analysis bars | start (s) | end (s) | BPM |
|---|---|---|---|---|---|
| Count-in (skipped) | 0 | 1 | 0.00 | 2.20 | 109 |
| Intro, bars 1-4 | 1-4 | 2-5 | 2.20 | 11.01 | 109 |
| Intro, bars 5-8 | 5-8 | 6-9 | 11.01 | 19.82 | 109 |
| Intro, bars 9-16 | 9-16 | 10-17 | 19.82 | 37.43 | 109 |
| First major release | 17-24 | 18-25 | 37.43 | 55.05 | 109 |
| Verse 1, bars 1-8 | 25-32 | 26-33 | 55.05 | 72.66 | 109 |
| Verse 1, bars 9-16 | 33-40 | 34-41 | 72.66 | 90.28 | 109 |
| The pause bar | 41 | 42 | 90.28 | 92.48 | 109 |
| LET IT GO | 42-49 | 43-50 | 92.48 | 110.09 | 109 |
| Verse 2 | 50-65 | 51-66 | 110.09 | 145.32 | 109 |
| Bridge transition | 66 | 67 | 145.32 | 147.52 | 109 |
| Bridge 1, bars 1-6 | 67-72 | 68-73 | 147.52 | 160.73 | 109 |
| Bridge 1, bars 7-8 | 73-74 | 74-75 | 160.73 | 165.14 | 109 |
| Bridge 2 | 75-82 | 76-83 | 165.14 | 182.43 | 111 |
| Bridge 3, dance | 83-90 | 84-91 | 182.43 | 199.73 | 111 |
| Final chorus, bars 1-8 | 91-98 | 92-99 | 199.73 | 217.03 | 111 |
| Final chorus, bars 9-16 | 99-106 | 100-107 | 217.03 | 234.33 | 111 |
| Final chorus, bars 17-22 | 107-112 | 108-113 | 234.33 | 247.30 | 111 |
| Final chorus, bars 23-24 | 113-114 | 114-115 | 247.30 | 251.62 | 111 |
| Ending | 115 | 116 | 251.62 | 253.79 | 111 |

The film runs to **258.0 s**, about 4 s past the song's last sample, so that dawn, the wash-out and the
collapse into the cursor have time. The audio is silence there.

---

## 5. Every BIG CLAP and major event, with its punctuation

No two are the same effect. Each is chosen for where it falls in the story. "z" is the target response
`pass2_av.py` must measure: z ≥ 3 on at least two of brightness, lines, change, structure and colour.

| id | owner bar.beat | time (s) | the punctuation | why here |
|---|---|---|---|---|
| splash | 1.1 | 2.202 | **Out of black.** The cursor erupts into a cube with a burst of rays: a full-frame magenta → cyan flash, bloom 4x, chromatic split. | The world begins. |
| impact | 17.1 | 37.431 | **MAJOR IMPACT.** We crash through the house's window. A white flash cuts inside the living room, and every object drops into place from above with an overshoot (a 3-frame settle). The room's lines start at 3x and decay over a bar. | "All you got": the groove arrives and so does the home. |
| C01 | 20.4 | 45.688 | **Colour explosion.** The room's lines run through the whole wheel in 250 ms (`grade/hueShift` 0→360) and land on a new accent. The floor lamp bursts (emission 12x). | The first clap in the house: colour. |
| C02 | 24.4 | 54.495 | **Geometric explosion, then a cut.** The living room's four walls blow outward 1.5 m into the black, the furniture lifts, and a cut lands in the bedroom as everything settles. | The release ends; the verse begins elsewhere. |
| C03 | 28.4 | 63.303 | **Light explosion.** The bedside lamp flares and floods the room white (exposure +2.5 EV for a beat). Inside the flash we cut to the living room. | Loneliness made bright for one beat. |
| C04 | 32.4 | 72.110 | **The palette turn.** A colour wipe crosses the room from the lamp outward, ice blue → crimson, over one beat. The world stays crimson. | The owner's "bars 9-16, a different vibe": made on the beat. |
| C05 | 36.4 | 80.917 | **Levitation.** Plates, cups, chairs and the clock jump 0.6 m on the clap and hang. They drift down over the next bar. | The world is slightly broken: objects behaving incorrectly. |
| C06 | 40.4 | 89.725 | **Collapse.** The hallway pinches inward (walls to the centre line) and the lines flare to white. | "Let it go": then black. |
| pause | 41.1-41.4 | 90.275 | **Black.** LET IT GO falls through the void, twice. The second LET IT GO is hot pink: the quick shift of tone. | The song loses momentum. |
| C07 | 45.4 | 100.734 | **Typographic explosion.** Every LET / IT / GO in the room flashes at once and flies off its wall, spinning outward. | The words become the event. |
| C08 | 49.4 | 109.541 | **Camera transition.** A whip-zoom (FOV 40 → 100 in 3 frames), a one-frame negative, and a cut into verse 2's apartment. | The rebuild ends: a hard re-entry. |
| C09 | 51.4 | 113.945 | **Data-mosh.** The frame freezes and smears for an eighth, then snaps. The globe, stopped on "the world stops turning", restarts violently. | The first break of the simulation. |
| C10 | 53.4 | 118.349 | **Positional corruption.** Every object jumps sideways by a different amount and snaps back over a beat. | Broken positions. |
| C11 | 55.4 | 122.752 | **Colour corruption.** A full channel split, a hue jump of 180°, and the lamp pumps like a heart. | "...and my heart keep pumping". |
| C12 | 57.4 | 127.156 | **Stretch.** The room stretches vertically 2x (the ceiling flies up) and snaps back. | Impossible motion. |
| C13 | 59.4 | 131.560 | **The mannequin.** A flash, and the mannequin is suddenly standing in the room, close, facing away. Gone at the next clap. | Unexpected, so strange. |
| C14 | 61.4 | 135.963 | **Duplication.** The room tiles sideways forever for one beat: the simulation repeating itself. | "How little do I know?" |
| C15 | 63.4 | 140.367 | **Floor drop.** The floor falls 1 m and every line goes white for a beat. | Losing the ground before the lift. |
| C16 | 65.4 | 144.771 | **Lift-off.** The roof rips upward and the walls stretch toward the sky. | It hands over to the bridge. |
| fills | 66.3 / 66.4 | 146.422 / 146.973 | The ceiling splits (66.3), then flies off into the sky (66.4). Two hits. | The snare fills. |
| C17 | 78.2.5 | 172.435 | **GOT?** A colourful sparkle explosion: every object bursts into a different jewel colour, with a particle burst from each. | The owner's punctuation. |
| C18 | 82.4 | 181.894 | **Implosion to black.** Every object falls into the mannequin's chair, then one beat of black until the dance. | "IS THAT ALL YOU..." unfinished. |
| sweep | 90.3 → 91.1 | 198.651 → 199.732 | **The spectrum sweep.** A rainbow band washes across all geometry over beats 3-4 and rises to white. The bass fill on 90.4 (199.192 s) kicks the camera through the last door into the open. | The owner's "one of the strongest transitions". |
| build | 112.3 / 112.4 | 246.219 / 246.760 | Everything contracts toward the summit's centre in two jolts. | The snare fills prepare the crash. |
| crash | 113.1 | 247.300 | **HUGE FINAL CRASH.** A supernova: every object in the film flies outward in full colour, light at maximum, then rings out over two bars as drifting, fading debris. | The final punctuation. |

---

## 6. Section by section

### Count-in (bar 0, 0.00-2.20 s)
Black. A small white cursor (a 6 cm glowing cube, far away) blinks on each beat: 0.1, 0.2, 0.3, 0.4. It is the
seed. It is the last thing seen in the film.

### Intro A, bars 1-4 (2.20-11.01 s): the world compiles
- **1.1 SPLASH** (above). The seed becomes a cube 1 m across, shooting a crown of 12 rays.
- **Every quarter note is one step of evolution.** The 16 beats are an authored sequence:
  - expand, shrink, regrow;
  - rotate 45°;
  - divide (1 → 2 → 4 → 8 sub-cubes);
  - orbit;
  - a wire frame appearing around it.
- **The music:** bars 1 and 3 open with a bright stab. Those beats get the biggest steps (the frame appears; the
  cells divide).
- **Camera:** locked off, centred. It breathes on every quarter: FOV −3° and 6 cm forward on the beat, eased back
  by the next. That is one of the brief's listed quarter responses, used here where nothing else moves the
  camera.
- **Colour:** P0. Each beat steps the line hue (magenta, cyan, white, electric blue), with a flash of edge
  intensity 2x on the beat decaying over the beat.

### Intro B, bars 5-8 (11.01-19.82 s): the bass enters
- **The eighth-note layer.** A ring of 8 small cubes orbits the structure. It steps one eighth of a turn on every
  eighth note, a clock hand, and its lines flicker on the off-beats.
- **The quarter layer** carries on: the structure pulses and grows.
- **The sustained bass note** is a slow rise: the structure grows taller through the bar.
- **Colour:** P0 with the electric blue added. Two hues alternate on the eighths.
- **Camera:** breathing, and a slow orbit begins (yaw 30° over the 4 bars).

### Intro C, bars 9-16 (19.82-37.43 s): the riser
- **The structure organises.** The lattice of cells lines up into floors and walls: the frame of a small house,
  with a pitched roof, windows and a door. It is drawn line by line on the eighths, faster each bar, like a
  wireframe being rendered. Furniture outlines appear inside it.
- **The vocal chop** ("all you got", once a bar from bar 13): the house's windows flash with it.
- **Camera:** a push toward the house that accelerates with the riser, aiming at the living-room window.
- **16.4, the one-beat gap:** everything freezes and the lines dim to 20 % for exactly one beat.
- **Colour:** P1. Hues step faster, and gold arrives at bar 13.

### First release, bars 17-24 (37.43-55.05 s): "All you got"
- **17.1 MAJOR IMPACT** (above): we are in the living room.
- **The living room**, about 5 x 4 m:
  - a couch with cushions; a coffee table with a cup;
  - a TV on a low cabinet, showing ALL YOU GOT;
  - a floor lamp and a rug;
  - a bookshelf with books;
  - a window with curtains and stars outside;
  - two paintings;
  - a plant;
  - the mannequin seated on the couch, back to camera: its first appearance, glimpsed.
- **On the wall:** a framed sampler (a home-sweet-home embroidery) that reads ALL YOU GOT. It is the title,
  made into a wall decoration.
- **The walk:** bars 17-18, a fast dolly through the room into the kitchen doorway. Bars 19-24 settle into a
  slower move through the kitchen and back.
- **The quarter pulse** drives four things:
  - the furniture hops (couch cushions and books, 2-4 cm on the beat);
  - the lamp's light (intensity 1.5x on the beat);
  - the line brightness (1.6x);
  - a colour step on beat 1 of each bar.

  It is strong in bars 17-18 and settles to half by bar 21 ("the section mellows").
- **C01 and C02:** above.

### Verse 1, bars 25-32 (55.05-72.66 s): the night apartment (P3)
**Room 1, the bedroom (bars 25-28):**
- a bed with a headboard, pillows and a blanket;
- a nightstand with a lamp;
- a window with the night sky and curtains;
- a wardrobe and a chair;
- a painting;
- the mannequin standing in the corner, facing the wall.

The camera drifts in through the door and holds on the bed wall. HOW LITTLE DO I KNOW? is written large on the
wall above the bed (55.3 s). Its two repeats appear on the ceiling and the window.

**Room 2, the living room again** (bars 29-32, after the C03 light flash):
- the TV shows CAN YOU TELL ME IT'S FINE THOUGH? (68.4 s);
- BREATHE AND GROW is on the plant pot's label (61.5 s, in room 1's last bar).

**The pulse:**
- the lamps breathe on the quarter (intensity ±25 %);
- the clock's second hand ticks on the quarter;
- the TV's static flickers on the eighths;
- the curtains sway on the bar.

The camera does not pulse. It is a slow dolly with holds, about 0.4 m/s.

**C03 and C04:** above.

### Verse 1, bars 33-40 (72.66-90.28 s): the turn to ember (P4)
**Room 3, the kitchen and dining room (bars 33-36):**
- a table set for one (plate, cup, fork), with two chairs;
- a hanging lamp over the table that swings on the half-bar;
- a fridge and a counter with a kettle;
- a wall clock;
- a window.

COME ON, TELL ME WHAT YOU WANNA is scattered over the cupboard doors, one word per door (72.4 s). MAYBE CAUSE A
LITTLE DRAMA is on the fridge (76.3 s). IF YOU FEEL IT, SAY IT, LET IT SHOW is in the window glass (78.5 s).

**C05 (levitation)** is in this room.

**Room 4, the hallway and stair (bars 37-40):**
- a narrow passage with coat hooks, a mirror and a small table with a phone;
- a short stair up to a door.

CAN YOU TELL ME IT'S FINE THOUGH? is in the mirror (81.1 s), and GET A LITTLE PEACE OF MIND is on the passage
wall (85.1 s). IT'S STEPS IN A PROCESS, LET IT GO is on the stair, one word per riser (87.5 s), so the camera
climbs the sentence.

**C06** collapses the hallway into the pause.

### The pause bar, 41 (90.28-92.48 s)
- Black. No geometry and no fog.
- LET (41.1), IT (41.1.5), GO (41.2) appear in white, large, in 3D, slightly turned. They tumble away into depth
  ("falling off").
- On beat 3 the second LET IT GO arrives in hot pink, and it stops dead at 41.4 ("abrupt stop").
- In the last eighth the violet of the next section seeps in: the quick shift of tone.
- The camera is perfectly still.

### LET IT GO, bars 42-49 (92.48-110.09 s): the corridor of words (P6)
- **The space:** a run of four small connected rooms. They are the apartment's back rooms: a study with a desk
  and a globe, a bathroom with a tub and a mirror, a laundry room, a box room full of stacked boxes. They are
  joined by short passages, and the camera dollies through them at about 1.6 m/s.
- **The words are placed along the path** so that LET, IT and GO pass the camera on their beats (1, 1.5, 2 and
  3, 3.5, 4). At 1.6 m/s an eighth note is 0.44 m.
  - Each repetition uses different surfaces: LET on the left wall, IT on the floor, GO on the ceiling; then LET
    on a door, IT on a mirror, GO across a window.
  - The sizes vary from 0.3 m to 2 m, and the layouts are quirky (tilted, stacked, wrapping a corner).
  - Each word lights the moment its beat lands (a stamp from 0 to full in 2 frames), holds, and dims as the
    camera passes.
- **The quarter pulse returns:** line brightness on the beat, and camera breathing as a small forward nudge on
  each beat. The kick and bass are exposed here, the strongest quarter pulse in the song.
- **Bars 48-49, the rebuild:** the words stop, the rooms drain to outline only, and the hats build as a flicker
  that accelerates.
- **C07 and C08:** above.

### Verse 2, bars 50-65 (110.09-145.32 s): the apartment breaks (P7)
- **The same rooms as verse 1**, revisited in a new order: living room, bedroom, kitchen, study. They are denser
  (doubled objects: two clocks, a second TV, stacked chairs) and stranger: furniture rotating slowly by itself,
  paintings crooked.
- **The rooms and their lines:**
  - DO YOU WANNA HAVE FUN? is on the TV, in the living room (50-53).
  - The globe on the study desk has been spinning since verse 1. It stops dead on "and the world stops turning"
    (113.1 s, bar 51.1). C09 restarts it.
  - "As the fires keep burning" (111.4 s) lights a fireplace in the living room: flames as flickering line
    cones on the eighths.
  - HEART KEEP PUMPING is on the bedroom wall, and the lamp pumps (C11).
  - TELL ME YOU'RE THE ONE is in the bedroom mirror.
  - HOW LITTLE DO I KNOW? comes x4 in four places in the kitchen and study (bars 58-61).
- **The words are more frantic:** smaller, more of them, overlapping, appearing on the eighths.
- **The pulse:**
  - quarter notes on the lines and lamps, stronger than in verse 1;
  - eighth notes on small objects (the clock, the fire, the TV);
  - the furniture's own slow rotation, faster each 4 bars.
- **Camera:** the verse pace, but restless. Quicker pans that discover the mannequin. A small handheld drift
  (sway). Two short reverse moves.
- **The 8 BIG CLAPs:** C09-C16, each a different corruption (above). They escalate toward the lift-off.

### Bridge transition, bar 66 (145.32-147.52 s)
- An impact on 66.1: FEEL IT GROW begins (FEEL 66.1, IT 66.1.5, GROW 66.2).
- The camera tilts up toward the ceiling.
- The snare fills split the ceiling (66.3) and throw it into the sky (66.4). Above is a starry indigo sky, the
  first time we see out.

### Bridge 1, bars 67-72 (147.52-160.73 s): the house grows into a tree of rooms (P8)
- **The camera rises** up the trunk, a vertical shaft. Around it, small furnished rooms grow out of the trunk in
  a spiral like branches: a helix of rooms, each smaller than the last.
- **Every GROW** (beats 2 and 4) **blooms one room**, from nothing to full size with an overshoot. Its window
  lights, and FEEL IT GROW is written on its wall as it opens.
- **The chorus joins** at bar 67: more rooms per beat, warmer.
- **The quarter pulse is not the driver here;** growth is.
  - The camera rises continuously, slowly accelerating, with a slow spiral around the trunk.
  - The sky warms from indigo to plum to rose as we climb.
- **Bars 71-72, the reveal:** the camera pulls out and sees the whole tree of rooms standing on a line-drawn
  hill, glowing against the sky.

### Bridge 1, bars 73-74 (160.73-165.14 s): the landing
- The chorus drops out and the drums pause.
- The camera eases down toward one room near the crown, a small room with a lit window, and floats in through
  its window.
- The tree's lights dim, and the synths ring out as long glows on the lines.

### Bridge 2, bars 75-82 (165.14-182.43 s): the room of objects (P9)
- **One room, nearly dark.** The mannequin sits on a chair at its centre, back to camera.
- **Around it, at different heights:** the house's objects on pedestals and floating, each lit in its own jewel
  colour:
  - the lamp, the globe and the clock;
  - the TV, the plant and a cup;
  - a picture frame, a book and a chair.
- **The camera** orbits the mannequin very slowly (90° over the 8 bars). There is no travel.
- **The eighth-note thump** turns every object one eighth of a turn on every eighth note: mechanical, like
  clockwork.
- **Each word lights one object:** THAT on 1, ALL on 1.5, YOU on 2, with IS on the 4.5 before. The words IS
  THAT ALL YOU are written on the four walls, one word per wall, and the orbit reveals them in turn.
- **The pulse stops on 82.3** and every object freezes.
- **C17 and C18:** above.

### Bridge 3, bars 83-90 (182.43-199.73 s): the house dances (P10)
- **Warm rooms flow past:** the living room, the kitchen and the bedroom, turned into a dance floor.
  - The floors are grids of tiles that light in patterns on the quarter: a checkerboard, a ring, diagonals.
  - Chairs hop on the quarter. Lamps swing on the half-bar. Paintings spin on their nails on beat 1. Books fan
    open on the eighths.
- **The camera glides:** long, smooth curves through the rooms, around corners and over the furniture. No camera
  pulse at all, as the brief asks.
- **The words:** IS THAT ALL? bounces across the floor tiles each bar. On bars 84 and 88, IS THAT ALL YOU GOT? is
  written across a whole wall.
- **90.3-4, the sweep** (above): the camera is lined up on a doorway full of white light, and the bass fill
  throws it through.

### Final chorus, bars 91-98 (199.73-217.03 s): the open (P11)
- **The landscape.** We come out on a line-drawn hillside at night, a primitive 3D landscape:
  - a grid-lined terrain of rolling hills to a horizon;
  - cone and sphere trees, boxy rocks and tufts of grass;
  - line-drawn mountains far away;
  - a sky of stars.

  The house's rooms float in the sky like lanterns, warm and slowly turning.
- **The camera soars** low over the hills and sweeps up.
- **LET IT GO, twice a bar.** The words are written across the landscape: giant letters lying on the hillsides,
  stamped as the camera passes over, and spelled by lanterns in the sky.
- **The quarter note is a feel, not a pulse.** The hills' lines step through the colour wheel on the quarter
  (one hue per beat, smoothly). The lanterns blink on the beat. The grass sways.

### Final chorus, bars 99-106 (217.03-234.33 s): steps in a process
- **A staircase rises** from the hilltop into the sky. Its steps are the rooms' floors, each step a lit tile.
- **The camera climbs it with the music.** IT'S JUST STEPS IN A PROCESS is written one word per riser. FOR YOUR
  LIFE is across the sky above the top step.
- The lantern-rooms gather around the stair.

### Final chorus, bars 107-112 (234.33-247.30 s): the summit
- **The top of the stair is a platform above the clouds**, where everything converges:
  - the tree of rooms, far off;
  - the floating rooms in a ring;
  - the landscape below.
- **The LET IT GO chants** spell themselves in the ring of lanterns.
- **Fireworks of geometry** (bursting cubes) on beats 2 and 4.
- **112.3 / 112.4:** everything is pulled toward the centre in two jolts.

### Final chorus, bars 113-114 (247.30-251.62 s): the crash
- 113.1 HUGE FINAL CRASH (above), ringing out across both bars.
- The debris slows, glowing; the colours fade from full chroma to warm.

### Ending (bar 115 and on, 251.62-258.0 s): dawn, wash-out, collapse
- We are on the hill. The horizon line glows rose-gold, and the sun rises: a line-drawn disc with a soft halo.
- The mannequin stands on the hilltop facing the sunrise, back to camera. It is the Friedrich figure pass 1
  found, kept for the end.
- **Light floods,** and the lines wash out to white (253-255 s).
- **The simulation breaks down:** data-mosh blocks and the geometry's lines dissolving from the edges inward.
- Everything collapses into the cursor: a 6 cm cube, centred in black. It blinks twice, as at the start, and
  goes out at 258.0 s.

---

## 7. The sound-to-picture vocabulary

| musical event | where | the visual response | camera? |
|---|---|---|---|
| quarter note | intro, release, verses, let it go, bridge 3, chorus | intro: a geometry step. Release: furniture hop, lamp, lines. Verses: lamps breathe, the clock ticks. Let it go: lines and a camera nudge. Dance: floor tiles and hopping chairs. Chorus: hue steps and lanterns. | intro: breathe (FOV + push). Let it go: a nudge. Elsewhere no. |
| eighth note | intro 5-16, let it go (the words), verse 2 small objects, bridge 2 | intro: the orbiting ring and flicker. Let it go: the words arrive. Verse 2: clock, fire, TV. Bridge 2: objects turn. | no |
| BIG CLAP | §5 | a different punctuation each time | sometimes (C08 whip, crash) |
| bass entry | 5.1 | the second clock (the ring) appears | — |
| riser | 9-16 | drawing speed, push speed, hue speed | push |
| vocal chop "all you got" | 13-24 | the windows flash (13-16); the TV and sampler (17-24) | — |
| voice lines | verses | words appear on surfaces at the line's start | — |
| silence | 41 (pause), 16.4 (gap), 73-74 (drums out), 82.3 (pulse stops), the tail | stillness: black, or frozen objects | still |
| key lift to F | 66 | the roof opens to the sky | tilt up |
| tempo step to 111 | 75 | the room of objects begins | orbit |
| the fill and sweep | 90.3-4 | the spectrum sweep | through the door |

**Camera breathing** (quarter-note FOV and push) is used **only** in intro A-B and "let it go". Bridge 3 and the
final chorus never pulse the camera. That is the brief's §10, §26 and §28.

---

## 8. The world kit (what is built, as data)

- **Props** (`tools/liminal/props.py`, primitives only):
  - seating and storage: couch, armchair, chair, table, desk, bed, nightstand, wardrobe, bookshelf with books,
    TV and cabinet;
  - lamps: floor lamp, table lamp, hanging lamp;
  - wall and floor pieces: window with mullions and curtains, painting, framed sampler, rug, mirror;
  - plants: pot with leaves;
  - strange objects: clock, globe, fridge, counter, kettle, plate, cup, boxes, fireplace;
  - the stair.
- **Rooms** (`tools/liminal/rooms.py`):
  - a room shell with doors and windows;
  - a floor of boards or tiles drawn as grooves;
  - each room's furniture placed by hand, in one or two SDF objects per room (the 96-node limit).
- **The mannequin** (an SDF figure from capsules and spheres): seated, standing, facing away, and a head turn.
- **Outdoors:**
  - grid-lined rolling terrain;
  - cone and sphere trees, boxy rocks and grass tufts;
  - pyramid mountains, the sun, stars;
  - the floating rooms and the staircase.
- **The seed and the house frame** for the intro.
- **The tree of rooms** (a helix screw of room modules) for bridge 1.

Worlds sit at the origin (pass 1's jitter lesson). Each chapter shows only its own objects.

---

## 9. Needs from engineering, and the fallbacks

These were sent to the coordinator at 11:35. The engineer builds the reusable systems; the art wires them.

| need | fallback if it does not land |
|---|---|
| **Spatial text from data**, perspective-correct, lit and fogged; ideally luminous strokes | an SDF neon-stroke font generated as data (capsule strokes, one SDF object per word) |
| **Beat envelopes** for the quarter, eighth and clap grids | dense timeline keys on the grid (exact, already possible) |
| **Camera breathing** (an additive journey offset) | keyed FOV, height and distance offsets |
| **Data-mosh / colour corruption** | chromatic aberration, `hueShift`, distortion keys; positional jitter by keys |
| **Spectrum sweep** | a `grade/hueShift` sweep plus an exposure rise |
| **Line width compensated for distance** | thinner world-space `edgeWidth` per room size |

---

## 10. How this pass is judged before the owner sees it

`pass2_av.py` on every render, with these targets:
- **The quarter-note lock:** above +12 dB with a locked share above 0.3 on at least one of brightness, lines or
  change, in intro A-C, the release, both verses, "let it go", verse 2 and bridge 3. In the final chorus a feel
  rather than a pulse: lines or colour only.
- **The eighth-note lock** in intro B-C, "let it go" and bridge 2.
- **Every BIG CLAP:** z ≥ 3 on at least two measures.
- **Every section boundary:** at least 2x the change around it.

Then the Critic (`critic_pass2.py`), whose reactivity analyzer must find a response, and a viewing against §12's
questions with contact sheets.

Two failures to kill on sight:
- any room that reads empty in a still frame;
- any pulse that is a generic scale multiplier on everything.

---

## 11. As built: where the film departs from the plan, and why

- **Every word is placed where the camera looks.**
  - The plan named surfaces ("LET on the left wall"). Built that way, a camera reproducer found 72 of 256 words
    off screen, behind the camera or facing away at the moment they appear.
  - Each word now has a hand-chosen screen position. A view ray at that instant lays the word on the wall,
    floor, ceiling, terrain or open air it meets, sized by distance.
  - The generator reports any word not seen: 0 of 254.
- **The words have a typeface per section:**
  - Menlo on screens;
  - Didot on the lonely verse 1 walls;
  - LET in Futura, IT in Didot italic, GO in Impact;
  - DIN Condensed in verse 2;
  - American Typewriter in bridge 2;
  - Marker Felt in the dance;
  - Futura and Rockwell outside.
- **The engineer's systems carry the score:**
  - the beat grid's pulses and per-clap channels (ADR-1045);
  - text as extruded glyphs (1046);
  - the line look in screen pixels (1047);
  - camera breathing (1048);
  - the data mosh (1049);
  - the spectrum sweep (1050).
- **Bridge 2's mannequin turns its head a quarter turn on GOT?** It happens once in the film, and it is the
  only time a figure moves.
- **Fireworks** (particle bursts) mark the summit's 2 and 4, the GOT? sparkle and the crash.
- **The release ends on the downbeat of verse 1** (25.1, where the voice enters), not on C02. C02 blows the
  walls out over its beat first.
