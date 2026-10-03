# Research report 1: how compelling realtime VFX environments are constructed

Sonic Garden VFX expansion, deliverable 1 (art agent, 2026-10-02). The owner's brief is `../02-brief-vfx-expansion.md`
(§1). Report 3 (`03-authoredness.md`) covers why procedural scenes look procedural. The engineer's reports 2, 4 and 5
cover modulation, glitch implementation and evaluation.

## Method

- **Web research.** A research pass opened about 95 sources. They include:
  - talks: Guerrilla's Nubis, Hillaire's sky and volumetrics, Wronski's froxels, Journey's sand, the Riot and
    Blizzard VFX principles;
  - papers: Bridson's curl noise, the DNGR/*Interstellar* paper, Lomas's Cellular Forms, Spence's crossmodal
    correspondences, Crutchfield's video feedback;
  - interviews with installation artists, demosceners and music-game designers.

  The URLs are inline below. Claims taken only from a search summary are marked *(summary)*.
- **The engine.** Every technique was then checked against this engine as it exists on `proto/sonic-garden`
  (fc99580d): the code, the Effect Library's 48 kinds, the SDF compiler, the particle system, the material
  programs and the post chain. It was checked by look-development renders as well as by reading. The research pass
  assumed several things were missing that the engine already has, and those are corrected here:
  - particle ribbons and trails (ADR-040);
  - the Trail effect (ADR-703);
  - a black-hole lens with a photon ring (ADR-719);
  - material programs on SDF surfaces;
  - per-depth-band contrast and saturation (ADR-038).
- **Lessons from our own scenes.** I also drew on the Sonic Garden's three passes and the Liminal film's three.

Feasibility tags: **[R]** buildable now as data; **[E]** a small extension of an existing system; **[N]** new engine
work.

## 1. The one finding behind all the others: readability before richness

Every discipline surveyed says the same thing in its own words:
- game VFX (Riot, Blizzard);
- environment art (Sable, Abzu, Journey);
- film lighting (Pixar's Sudeep Rangaswamy);
- painting (Howard Pyle and James Gurney);
- installation (Henke, Ikeda).

What they say:
- **Value contrast steers the eye more than anything else.** The strongest value contrast is the focal point.
- **Each effect, and each scene, has one primary read.**
- **Detail concentrates around the subject, inside large calm areas.**

Sources:
- **Riot's VFX guide** asks for one primary shape that "should never confuse a player", with secondary and tertiary
  shapes under it. It also asks for a scale of importance: the weakest attack must not look like the ultimate
  (https://www.vfxapprentice.com/blog/10-league-of-legends-vfx-design-tips,
  https://www.leagueoflegends.com/en-us/news/dev/clarity-in-league/).
- **Gurney and Pyle:** "The fewer tones the simpler and better". Put "white against white … black against black,
  then black and white where you want the center of interest"
  (http://gurneyjourney.blogspot.com/2018/01/how-many-values.html).
- **Neil Blevins:** "large uninterrupted shapes with small condensed areas of detail" *(summary)*.
- **Sable:** "islands of content" in negative space
  (https://www.gamedeveloper.com/marketing/how-shedworks-refined-the-art-of-sable-in-pursuit-of-readability).
- **Brejon:** "An image without emphasis is like wallpaper … a good framing is more important than good lighting"
  (https://chrisbrejon.com/cg-cinematography/chapter-3-gestalt-theory/).

**For a live audio-reactive scene this is doubly true.** The music adds motion everywhere. A frame with no value
hierarchy becomes noise as soon as it moves.

## 2. Atmosphere: the cheapest depth there is

- **Fog colour is never constant.** It warms toward the sun and cools away from it. iq's fog uses separate RGB
  extinction and in-scatter, "six different coefficients" (https://iquilezles.org/articles/fog/).
- **Distance is geometric.** Each slab of air lifts the darks toward the sky colour by a fixed fraction, Gurney's
  "wedding veils" (http://gurneyjourney.blogspot.com/2013/07/atmospheric-distance-and-value.html). That is what
  separates silhouette planes.
- **Looking toward a low sun through haze, distance gets warmer,** not cooler (Gurney, reverse atmospheric
  perspective).
- **Visibility is an emotional dial.** Abzu's Matt Nava: "adjusting the distance you can see into the murk can have a
  huge effect on the player's emotional state"
  (https://80.lv/articles/interview-with-the-creative-director-of-abzu).
- **Shafts need an occluder with gaps, a medium, and a light near the frame** (GPU Gems 3 ch. 13; Ctrl-Alt-Test's
  H – Immersion, https://www.ctrl-alt-test.fr/2018/a-dive-into-the-making-of-immersion/).

**In this engine:**
- [R] One fog law (ADR-705): `scene/volumeDensity`, the froxel march with lights' `volumetric` strength, height fog
  (`scene/fogHeight*`), and Horizon Density (`scene/horizonDensity`, denser with distance).
- [R] The procedural sky (zenith, horizon, ground, haze, sun) supplies the ambient light.
- [R] Per-depth-band `contrast` and `saturation` in the scene's composition data (ADR-038). This is atmospheric
  perspective in post: a far band at contrast 0.7 and saturation 0.6 recedes even where the march is off.
- [R] Placed fog banks (the `volumetricFog` effect) put air only where the shot needs it.
- [N] Occluded god rays. The march has no shadow-map sampling (Liminal finding 3). The `lightBeam` effect is a
  lit-air cone, not a shaft.
- **Cost.** The march is the costliest pass live: 21 ms at the default tier at 1080p, 4.5 ms at Preview, with 24
  steps live. A scene marches only where air is its subject. The others use the closed-form fog and the depth bands.

## 3. Particles that read as matter, not noise

- **A structured, divergence-free motion field.** Bridson: plain Perlin velocity fields "contain many sinks ('gutters'
  where particles accumulate)"; curl noise does not
  (https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph2007-curlnoise.pdf).
- **Spawn from structure and light the particles.** Smash's goals for Fairlight's system: spawning from meshes and
  images, curl motion, and real lighting "to unify millions of particles visually"
  (https://directtovideo.wordpress.com/2009/10/06/a-thoroughly-modern-particle-system/).
- **One global wind.** Ghost of Tsushima drives grass, trees, cloth, leaves, smoke and petals off one wind field
  (https://blog.playstation.com/2021/01/12/how-stunning-visual-effects-bring-ghost-of-tsushima-to-life/).
- **Restraint.** Muddy effects come from long lifetimes, too many particles, or low contrast against the background.
  The rule of thumb is two or three colours and three to five layers *(summary of VFX guides)*. Keyser: reduce "the
  amount of time your particles are on screen".
- **Fewer, larger, slower particles carry volume;** a few fast ones carry energy.
- **Measured in our first look-development render** (Event Horizon, 2026-10-02):
  - 40,000 additive dots at 2-3 px over a black sky read as confetti, not gas.
  - Gas needs either large, soft, faint particles that overlap into a continuous glow, or long thin streaks aligned
    with the flow, not round dots.

**In this engine:**
- [R] GPU particles with:
  - point, sphere, disc, box and spline emitters, the disc oriented and elliptical;
  - curl turbulence, an attractor with an orbit force (a swirl round any point), up to four field forces, and the
    shared wind field (`windInfluence`);
  - velocity-aligned stretch (streaks);
  - ribbons and trails of up to 32 points, with width, taper and fade;
  - size, colour and opacity curves;
  - size skew (many small, few large: ADR-520's answer to uniform randomness);
  - drag correlated with size, clustering, firefly pulse and sync, and a Henyey-Greenstein scatter toward the key
    (dust that blazes only when you look into the light);
  - collision with splash rings, and a camera-carried wrap volume for weather.
- [R] `burst` and `position` are parameters, so a note-on can release particles exactly where its pitch puts them.
  Event Horizon's note-stars do this.
- [E] Spawning on an SDF or mesh surface.
- [N] Self-shadowing or froxel-lit particles. Particles are unlit, so give them emission and a scatter term instead.

## 4. VFX timing and shape: energy, shields, portals, magic

**The envelope is anticipation, climax, dissipation.**
- Climax: "maximum number of elements. Biggest Contrast, High Saturation".
- Dissipation: "Low Contrast, Low Opacity".
- "If your FX feel long, they're waaaaay too long." (https://80.lv/articles/vfx-staples-shape-color-and-motion,
  VFX Apprentice.)

The other principles:
- One dominant colour with its complement as secondary.
- "Desaturating and balancing colors can do wonders in creating richness."
- Glow should light its surroundings.

**In this engine:**
- [R] The Effect Library (ADR-702, 703, 716, 719): every kind is an instance on an owner, and every leaf is a
  routable parameter `fx/<id>/<leaf>`.
  - **Energy:** `plasma`, `energyShield` (hex or cells, rings on each hit), `forceField`, `chargeUp`,
    `discharge`, `lightning`, `arc`, `electricField`.
  - **Distortion:** `portal`, `realityTear`, `shockwave`, `ripple`, `spaceWarp`, `heatShimmer`, `gravLens` (a lens,
    or a black hole with a photon ring).
  - **Light and surface:** `halo`, `lightBeam`, `glow`, `pulse`, `rimLight`, `fresnel`, `dissolve`, `growth`,
    `breathing`, `organicPulsation`, `bioluminescence`, `pulsingVeins`.
  - **Sky:** `stars`, `aurora`, `comet`, `meteorShower`.
  - **Weather and media:** `tornado`, `vortex`, `volumetricFog`.
  - **Motion:** `orbit`, `spiral`, `float`, `shake`, `bounce`, `trail`.
  - **Particles:** `particleEmitter` presets.
- **No Sonic scene has used them before.** They are the brief's §5 "Entity Effects".
- [E, in progress] Live event triggers (the engineer's `TriggerSource::Signal`, VFX-ARCHITECTURE.md §1.3). Without
  it, Shockwave, Ripple, Lightning, Discharge and Charge-Up fire only from a file's analysis.
- [R] Envelope shapes come from route chains: attack and decay, peak-hold, spring with overshoot (ADR-1041). A
  kick's flash wants attack 0 and decay 120-250 ms. A heavy thing wants attack 300-500 ms and decay 1.2-1.5 s
  (Sonic Garden pass 1, Critic finding F003).

## 5. Cosmic environments

- **One dominant luminous body is the key light.**
- **Lensing reads when the disk's image wraps over and under the shadow.**
- **The DNGR team chose a disk that was "very anemic"** and removed what confused:
  - "the flattened left edge of the black-hole shadow, and the multiple disk images … would be too confusing for a
    mass audience";
  - they slowed the spin and omitted the Doppler asymmetry (https://arxiv.org/abs/1502.03808).
- **Cosmic skies are tinted, never flat black.** No Man's Sky's art director took "sci-fi book covers" (Chris Foss)
  over "black starfields, grey dull monolithic spacecrafts" *(summary)*.
- **Emission against absorption:** glowing gas against dark dust lanes, with star clusters for scale.

**In this engine:**
- [R] `gravLens` in black-hole mode. **This is the strongest single image the engine has made for this brief.**
  - It is a thin-lens remap of the scene behind its plane (the Refsdal point-mass mapping DNGR also used).
  - Our first Event Horizon render showed the disk's far side bent up over the shadow and under it without any
    authoring: the *Interstellar* image.
  - The photon ring, Einstein radius and horizon size are parameters a bass line can breathe.
- [R] The `stars` sky (density, magnitude slope, colour spread, twinkle, a galactic band with a dark rift).
- [R] The Cosmic Ocean nebula sky (ADR-390/450).
- [R] Plasma orbs for protostars.
- [R] `aurora`, `comet` and `meteorShower` for skies.
- [N] A density-volume raymarch for true volumetric nebulae and disks (research item 2). The placed fog bank and
  additive particles stand in.

## 6. Abstract organic: bioluminescence, cells, growth

- **Emergent forms from simple local rules, rendered simply.** Andy Lomas renders Cellular Forms as white Lambertian
  forms lit by ambient occlusion alone, or as X-ray density
  (https://andylomas.com/extra/andylomas_paper_cellular_forms_aisb50.pdf).
- **Edge-driven growth ruffles.** Floraform: "grow more at the edge"
  (https://n-e-r-v-o-u-s.com/projects/sets/floraform/).
- **Venation hierarchies** come from space colonization (Runions et al. 2007).
- **Bioluminescence is light in the dark, but readability needs a floor.**

**In this engine:**
- [R] Bioluminescence through emissive material programs, which decide *where* light lives on a surface while the
  routes decide its colour and strength (Sonic Garden pass 2, ADR-1023).
- [R] The surface effects `bioluminescence`, `pulsingVeins`, `organicPulsation`, `breathing` and `growth` on mesh and
  procedural owners.
- [R] Tubes with radius profiles, lathe profiles, a grammar and a hierarchy for branching (ADR-028/029/043).
- [R] SDF smooth unions for cell clusters.
- [N] Reaction-diffusion compute. The engineer recorded it as not built now (VFX-ARCHITECTURE.md §3.3). Video
  feedback is its mathematical cousin (Crutchfield modelled it with a reaction-diffusion PDE), so the engineer's
  temporal feedback is the practical stand-in.

## 7. Glitch, corruption, CRT, datamosh, feedback, as scene design

- **Glitch looks authored when it breaks an expectation the viewer already holds.** Menkman: artists use the
  medium's maxims "as a façade, to trick the audience into a flow of certain expectation that the artwork
  subsequently rapidly breaks out of." And "to design a glitch means to domesticate it … [it becomes] a filter that
  consists of a preset and/or a default"
  (https://amodern.net/wp-content/uploads/2016/05/2010_Original_Rosa-Menkman-Glitch-Studies-Manifesto.pdf).
  **So ration it, vary it, and make it break something readable.**
- **The source is choreographed for the corruption.** For Kanye West's "Welcome to Heartbreak" (Nabil, Ghost Town
  Media, 2009), performers were choreographed "to give us the most leeway with how the mosh effect works"
  (https://motionographer.com/2009/02/19/tintori-and-nabil-breaking-your-internets/).
- **Pixel sorting follows the image's structure.** Asendorf sorts only runs that pass a threshold
  (https://github.com/kimasendorf/ASDFPixelSort).
- **Feedback is a "space-time simulator"** whose controls are zoom, rotation, translation, luminance and contrast.
  A slight rotation gives logarithmic spirals (Crutchfield 1984,
  https://csc.ucdavis.edu/~cmg/papers/Crutchfield.PhysicaD1984.pdf).
- **Data aesthetics are maximum contrast synchronised one to one with sound.** Ikeda: black and white at "some
  hundreds of frames per second" (https://www.ryojiikeda.com/project/testpattern/).

**In this engine:**
- [R] `temporal/mosh` (ADR-1049). The Liminal film used it on its cuts.
- [R] `temporal/echo`, the `post/sweep` hue band, lens chromatic aberration and distortion.
- [R] The SDF line look (ADR-1047).
- [R] Extruded text (ADR-1046).
- [E, the engineer's ADR-1064/1065] The new post and temporal effects:
  - `post/glitch` (block displacement, tear, channel swap);
  - `post/split` (directional and spectral RGB);
  - `post/sort` (threshold max-smear);
  - `post/radial`, `post/shock`;
  - `post/display` (scanlines, mosaic, posterise, dither);
  - `temporal/feedback` (zoom, rotate, drift, hue, unrolled FIR);
  - `temporal/slit`.

## 8. Installations and projection: darkness, layers, one medium

- **Darkness is the canvas.** Henke on lasers: "the beauty is on the edges, the beauty is when you hit certain
  limits" (https://www.ableton.com/en/blog/robert-henke-lumiere-lasers-interview/).
- **Depth from translucent layers with the subject visible through them:**
  - Flying Lotus Layer 3: front scrim, performer, rear screen;
  - Eric Prydz EPIC: a 20 × 9 m Holo-Gauze;
  - Nonotak: stacked fabric.
- **Structure and content co-designed** (Amon Tobin ISAM, deadmau5 Cube V3).
  - TouchDesigner "behaves almost exactly like a modular synthesizer", and the Cube team rejected the "VJ furiously
    mashing clips" model (https://derivative.ca/community-post/made-love-touchdesigner-v99-cusersdeadmau5/60967).
- **Light as the narrative agent.** Lemercier's *Fuji* is a static drawn mountain revealed only by light,
  with "light sources driven by Paul's chords arrangements" (https://joanielemercier.com/the-making-of-fuji/).
- **One concept per piece.** Max Cooper: "these are all simple systems, but they all contain a huge amount of visual
  richness" (https://www.ableton.com/en/blog/composing-infinity-max-cooper-his-new-album/).
- **Make invisible forces visible.** Gremmler works by "extracting and amplifying" motion
  (https://clotmag.com/interviews/tobias-gremmler-morphing-the-visual-uniqueness-of-movement-and-rhythm).
- **Sound-reactive matter as sculpture.** Sachiko Kodama's ferrofluid works (*Protrude, Flow*, 2001; the *Morpho
  Towers*) are black magnetic liquid that rises into spikes with sound. This is a direct precedent for a
  scene in which notes pull spikes from a surface. It is my addition, not from the research pass.

## 9. Demoscene and shader environments

- **Direction beats technique.**
  - fr-041 *debris* won the scene.org award for best direction (https://www.pouet.net/prod.php?which=30244).
  - Elevated's camera: "Pure sin/cos cameras are too mathematical… Real cameras have weight, inertia… They shake"
    (https://iquilezles.org/articles/function2009/function2009.pdf).
  - Navis: "you always need to have a glue, avatars or something. Otherwise it will be just another demo of cubes"
    (https://6octaves.com/2015/04/interview-navis-asd.html).
  - Smash: "3 big high points" synchronised to the music (https://6octaves.com/2013/10/interview-with-demoscener-smash.html).
- **The SDF toolbox:**
  - domain repetition with per-cell hashes, so each instance varies (https://iquilezles.org/articles/sdfrepetition/);
  - smooth minimum;
  - folds, KIFS and the Mandelbox (https://en.wikipedia.org/wiki/Mandelbox);
  - Mercury's hg_sdf rule: keep the gradient at or below 1 (https://mercury.sexy/hg_sdf/);
  - iq's "painted lighting" and cosine palettes (https://iquilezles.org/articles/palettes/).
- **"Evoke, don't simulate."** "The idea is NOT to render perfect snow, but to draw something that evokes snow."

**In this engine:**
- [R] The compiled SDF (ADR-1003): CSG, smooth unions, repeat and polar repeat, screw, warp, fold and recurse,
  displacement, up to 8 surfaces per object, AO, soft shadow, rim and the line look; 96 nodes.
- [E] Per-cell hashes for repetition.
- [E] Orbit-trap colouring.

## 10. Music-reactive worlds: co-authored, not visualised

- **Co-author, don't visualise.** Panoramical's "faucets" emit sound and light together, and "our way worked even
  better than straight visualization techniques". Its intensity peaks at 75% and dips, like a filter opened and
  closed
  (https://www.gamedeveloper.com/design/game-design-deep-dive-controlling-space-and-sound-in-i-panoramical-i-).
- **Every object sings.** Kanaga on Proteus: "The ideal is to have every object making music" *(summary)*.
- **Quantise and bind.** In Rez, shot sounds fold into the track, and "the visuals snap into tighter patterns"
  (https://pspolygons.substack.com/p/what-rez-knew-about-electronic-music).
- **Smoothed signals move large things; raw transients flash.** MilkDrop's `bass` against `bass_att`: "1 is normal;
  below ~0.7 is quiet; above ~1.3 is loud"
  (http://www.geisswerks.com/milkdrop/milkdrop_preset_authoring.html).
- **Perceptual correspondences** (Spence 2011, http://www.daysyn.com/Spence_-_2011_-_Crossmodal_correspondences_A_tutorial.pdf):
  - higher pitch with higher elevation, smaller size and higher spatial frequency;
  - loudness with brightness and size;
  - rounded shapes with soft sounds and angular shapes with sharp ones ("bouba/kiki");
  - pitch does not map to contrast.
- **Strip and focus.** Thumper: "smooth perfect geometric forms … profound dread in a cosmic scale"
  (https://caneandrinse.com/thumper-interview/).

**In this engine:**
- [R] The bus has the Sonic Character (timbre, two tiers), the musical context (`notes.*`), and the beat clock.
- [E, ADR-1060-1062] The engineer's response model:
  - the classes `response.kick/snare/hat/low/onset` with envelopes;
  - the levels `bass`, `sustain`, `transient`, `flux`, `melodic`, `intensity`;
  - the per-note MIDI signals `notes.lastPitch`, `lastVelocity`, `interval`, `held`, `release`, `low`, `high`;
  - the sensitivity chain.

  Every scene maps the classes separately, so a kick and a hat never move the same thing (research 5's
  "specificity").
- [R] Spring and integrate stages (ADR-1041): a rate becomes a travel, and a change eases with overshoot.

## 11. Building a shot

- **Value families:** two or three tones in a light family and a dark family (Pyle and Gurney).
- **Lighting functions** (Rangaswamy, Pixar): direct attention (a rim makes a subject "pop"), mood, depth by lighting
  planes separately, and continuity.
  The rig vocabulary is key, fill, rim, kicker and bounce
  (https://silo.tips/download/visual-storytelling-through-lighting-sudeep-rangaswamy-pixar-animation-studios-1).
- **iq's outdoor rig:** a warm key with soft shadows and no AO; a cool sky fill from above, with AO; a dim warm bounce
  opposite. Keep albedo at or below 0.2 linear: "If you need a brighter image, make the lights brighter, not the
  materials" (https://iquilezles.org/articles/outdoorslighting/).
- **Colour scripts** (Ralph Eggleston, Pixar): "a stream of consciousness map of color, value, and visual drama"
  (https://hyperallergic.com/the-art-of-pixar-chronicle-books/).
- **Contrast and affinity** (Bruce Block, *The Visual Story*): contrast raises intensity and affinity lowers it,
  across space, line, shape, tone, colour, movement and rhythm.
- **Motion hierarchy.** It has no single canonical source. It follows from:
  - the parallax principle;
  - MilkDrop's smoothed and raw pair;
  - Elevated's camera inertia;
  - Ghost of Tsushima's single wind.

## 12. What this means for Sonic Garden: the construction method

Every Sonic VFX scene is built in this order. It is the order a VFX artist designing a shot would use.

1. **A thesis in one sentence**, naming the subject and the musical role of its parts (Sable: "the essence of what we
   wanted to make people feel before we started any work").
2. **A greyscale value design first.** Place the focal subject's brightest value and the large calm areas, then
   check a 64 × 36 thumbnail. This is a check our critic and the engineer's evaluator can automate (saliency
   concentration, dominance ratio, notan).
3. **Silhouette planes.** The foreground, the subject plane and two to four receding planes, each separated by
   value and air (depth bands, fog, or emission against darkness).
4. **The light.** One key with a direction and a source. A rim or backlight on the subject. Practical lights that
   belong to the world. No front-lit showroom (the Sonic Garden pass 1 lesson).
5. **The palette.**
   - One dominant hue family and its complement as a sub-10% accent.
   - The highest saturation and value reserved for the subject.
   - The palette written as OKLab states (ADR-1043) or as named roles, so audio moves positions along it, never raw
     RGB.
6. **The motion hierarchy.**
   - The sky and far structure move over tens of seconds to minutes.
   - The environment over bars.
   - Secondary elements over beats.
   - Flashes, glints and glitches within a frame or two.
   Each tier gets its chain timing.
7. **The instrument.** Each signal class gets one visual job that suits the world. It is written into the project's
   `sonicScene.vocabulary` so the evaluator and a reader can check it.
8. **The live budget.** Measure at the Preview tier. Only a scene whose air is its subject marches fog.
9. **Look at it, measure it, and kill it if it is weak.**

## 13. Practical construction rules (the scenes follow these)

**Composition and value**
1. One focal subject per scene. It holds the brightest value, the highest local contrast and the most saturated
   accent. No other region exceeds about 80% of its luminance at rest.
2. Two or three value families. A scene must pass a greyscale thumbnail test before any colour work.
3. Silhouette first, then value, then colour.
4. Concentrate detail in at most a third of the frame around the subject. Leave large calm shapes elsewhere.
5. Put the subject off centre, on a third or a golden section, and keep it there when the camera moves. A
   centred hero is allowed only when the scene is about symmetry: the mirror, the plate.
6. Keep albedo at or below about 0.2 linear and get brightness from lights. Expose for the shadows.
7. Separate the subject from the background with a rim, a backlight or a silhouette against the brightest region.

**Atmosphere and depth**
8. Four to six receding planes. Fog, depth bands or emission against darkness lift each one toward the background
   value by a constant fraction.
9. Fog is tinted toward the light it faces. Distance cools and desaturates, except when looking into a low sun.
10. Use visibility distance as the mood parameter: short for tension, long for release.
11. Fade fine detail with distance. Lines and contour bands do not alias in the far field.

**Colour**
12. One dominant hue family, a secondary, an accent under 10% of the frame, and a highlight reserved for the
    subject and for hits.
13. Audio moves colour along a designed path: a palette position, a temperature, a saturation, or a highlight
    brightness. It never sets raw RGB.
14. Reserve maximum contrast and saturation for the biggest musical moments, the hits and the climaxes. Use affinity
    the rest of the time.
15. Cosmic skies are tinted. Digital and data worlds may be monochrome with one accent.

**Motion and timing**
16. Every scene declares four motion tiers, and the background never moves faster than the subject.
17. Every event has an anticipation (optional, live), an impact (peak count, contrast and saturation) and a
    dissipation. The dissipation is shorter than you think: 100-250 ms for flashes, under 100 ms where notes are
    dense.
18. Visual magnitude follows musical salience. A hat never matches a kick, and a kick never matches a phrase.
19. Cameras drift on slow loops with inertia (tens of seconds). They never follow the music beat by beat, except for
    a single designed impact.

**Music mapping**
20. Smoothed signals move large things (fog, sky, scale, camera). Raw hits flash small things or the frame
    briefly.
21. Map along perceptual correspondences:
    - pitch to height, smallness and finer detail;
    - loudness to brightness and size;
    - a sharp timbre to angular forms, a soft one to round forms.
22. Each signal class has its own visual job, so the kick and the hat move different regions. A chord, a melody and
    a drum each get a different kind of response, never just more or less of the same one.
23. Bind a note to an object where possible: a star, a droplet, a lantern, an arc. Spectrum bars are the last resort.
24. Sustained energy grows, opens and warms the world. Transients flash, burst, crack and fracture it.

**Particles**
25. Particles follow a structured field (curl, orbit, wind). They spawn from structure: a note's place, a surface, a
    curve.
26. Fewer, larger, softer particles carry volume. Streaks carry speed. Dots alone read as noise.
27. Two or three colours per system, short lifetimes, and three to five layers at most.

**Glitch and feedback**
28. Glitch breaks something readable and is rationed to musical ruptures (snares, fills, drops). It is never on at
    rest.
29. Feedback runs at decay near 0.98, with small zoom and rotation, driven by smoothed signals.

**Darkness and form**
30. A scene may be more than 80% dark with one light event. Darkness is a material.
31. Hide repetition: vary every repeated element by a hashed seed in size, rotation and palette offset, and let
    the repeats recede into air.
32. Simplify physics that confuses the read (DNGR's decision). Evoke, don't simulate.
33. One concept per scene. A layer that does not improve the read or the music's mapping is removed.
