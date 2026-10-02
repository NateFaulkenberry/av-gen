# Research report 3: why procedural scenes look procedural, and what makes them look authored

Sonic Garden VFX expansion, deliverable 3 (art agent, 2026-10-02). It answers the brief's §1 question ("what makes a
procedural scene look *authored* rather than *randomly generated*") and §17 ("procedural generation with artistic
constraints rather than random generation with artistic colors"). It ends in the generation rules the Sonic VFX
scenes follow, each with the way the kit applies it or the way the evaluator can check it.

## Method

A research pass opened about 95 sources:
- design writing: Compton, Short, Cook, Smith and Whitehead, and the PCG book;
- production talks and slides, with the text extracted from the PDFs: Horizon Zero Dawn's GPU placement, Far Cry 5,
  Ghost Recon Wildlands, Unreal's Electric Dreams PCG, and Townscaper;
- generative artists: Tyler Hobbs, Vera Molnár, Georg Nees, Inigo Quilez;
- painters and composition theory: Dow, Gurney, Payne, Block, Pyle;
- perception research: Taylor's fractals, Graham and Field's image spectra, Voss and Clarke, Berlyne, Reber's
  fluency, Ramachandran and Hirstein.

Points seen only in a search excerpt are marked *(snippet)*. A number marked *(heuristic)* is a proposed starting
value, not a published standard. The audio-reactive translation (§9) is my own; it rests on the sources but extends
them. §10 adds what our own scenes taught us.

## 0. The thesis

1. **Procedural output looks procedural when its variation fails in one of four ways.**
   - **It is mathematical rather than perceptual.** Kate Compton: "I can easily generate 10,000 bowls of plain
     oatmeal … *mathematically speaking* they will all be completely unique. But the user will likely just see *a
     lot of oatmeal*… Perceptual uniqueness is the real metric"
     (https://galaxykate0.tumblr.com/post/139774965871/so-you-want-to-build-a-generator).
   - **It is stationary.** "Every place on the map 'feels' the same"
     (https://www.redblobgames.com/maps/terrain-from-noise/).
   - **It is free of context.** Nothing about where a thing sits is explained by water, gravity, wear, a path or a
     meaning.
   - **It is unranked.** Nothing wins in size, value, saturation, detail or motion.
2. **Authored work makes a few large decisions deliberately and lets randomness act only on small, shaped ones.**
   Oskar Stålberg: "I want the larger shapes to feel very predictable, but the smaller shapes are allowed to vary
   quite a bit"
   (https://www.gamedeveloper.com/game-platforms/how-townscaper-works-a-story-four-games-in-the-making).
3. **The industrial pipelines that look authored share one design.** Horizon Zero Dawn, Far Cry 5, Ghost Recon
   Wildlands, Electric Dreams, Townscaper and Spelunky all combine:
   - hand-authored assets and chunks;
   - authored rules;
   - fields that carry meaning (erosion, flow, slope, distance to water and roads, explicit edge zones);
   - even, minimum-distance point patterns.

   None of them scatters raw noise.
4. **Every visual channel needs a hierarchy with one winner, the focal subject, and some rests.** The sources are
   Dow's subordination, Miyazaki's *ma*, the 70/30 detail rule and Riot's "scale of importance".
5. **In an audio-reactive scene, oatmeal is the whole frame pulsing at once.** The remedies:
   - the reaction belongs to the subject;
   - its size matches the musical importance of the event;
   - musical structure drives the large changes, and the raw signal only small ones;
   - every reaction has an impact and a dissipation.

## 1. The failure modes, from the people who build generators

- **Perceptual differentiation is not perceptual uniqueness.** Compton separates content that is merely "not
  identical to the last" from content with a memorable character.
  - "The most reliable generators are the ones where you can concretely describe constraints."
  - Parametric methods let "you … see something 'new', but never something surprising."
  - In her GDC talk *(summarised by Game Developer)*:
    - "Barnacling": medium things are placed next to big things;
    - curating the random distribution through "category hierarchies";
    - whitelisting the seeds of "good" Spore planets.
- **Variation must be connected to something.** Emily Short: if variation is purely decorative, "the player will
  soon realize that it is decorative and start looking past it"
  (https://emshort.blog/2016/09/21/bowls-of-oatmeal-and-text-generation/). **For us, that something is the music.**
  Visual variation that encodes nothing gets tuned out; variation the audience can trace to the sound registers.
- **Judge the whole output space, not its best outputs.**
  - Michael Cook: a big generative space has "more good content … But it also usually has more junk"
    (http://www.possibilityspace.org/tutorial-generative-possibility-space/).
  - Smith and Whitehead's expressive-range analysis: plot many outputs on chosen metrics to "reveal biases in the
    generator" (https://users.soe.ucsc.edu/~ejw/papers/smith-pcg-2010.pdf; PCG book ch. 12,
    http://pcgbook.com/chapter12.pdf).
  - For a live instrument the output space is every input class the owner might play. **The test matrix (§21) is our
    expressive-range analysis.**
- **No Man's Sky's lesson.** Planets "only superficially different than one another. This planet is red. That planet
  is blue" (https://www.vice.com/en/article/nz7d8q/no-mans-sky-review). Swapping hues and slider values on one
  generator produces oatmeal. Distinctiveness needs categorical, structural differences: silhouettes, compositions,
  value keys, motifs. **This is exactly the owner's complaint about four visualizers, and why the catalog's 16 scenes
  differ in space, technique, camera and palette rather than in parameters.**

## 2. How authored-looking pipelines work

- **Spelunky** first builds a guaranteed solution path through a 4×4 grid, then fills each room from hand-authored
  templates chosen by the room's role (http://tinysubversions.com/spelunkyGen/). The structure is authored and
  guaranteed, and randomness only decorates it.
- **Townscaper** uses irregular relaxed quad grids (no grid look) and Wave Function Collapse over hand-authored
  modules. "What's important is what happens when things change … what happens like at the corner of a house." In
  Bad North, a failure restarts the generation; in Townscaper, it is "allowed to fail silently".
- **Horizon Zero Dawn** (Jaap van Muijden, GDC 2017, from the slides:
  https://www.guerrilla-games.com/media/News/Files/GDC2017_VanMuijden_GPUBasedProceduralPlacementInHorizonZeroDawn.pdf):
  - The stated goals: "Quick iterations, Large variety, Believable look, Art Directable, Data driven, Deterministic,
    Locally stable".
  - An "Ecotope" (an environment identity) drives assets, distribution, colourisation, weather, effects, sound and
    wildlife at once.
  - "WorldData" maps are "All Generated, All Paintable": erosion wear, flow and deposition, cavity, water flow,
    roads, and colour variance.
  - Painted density decodes into edge zones ("Inner Forest / Forest Edge / Sparse Trees").
  - Placement dithers against an even, distance-maximising point pattern.
  - The proof: "Users are making screenshots of our output!"
- **Far Cry 5** (Etienne Carrier, GDC 2018): vegetation follows occlusion, flow, slope, curvature, illumination and
  wind, and "species with the highest accumulated viability at a location 'wins'". Grass leans with the shore's
  slope, and colour shifts near water (https://christianjmills.com/posts/procedural-tools-far-cry-5-notes/).
- **Ghost Recon Wildlands:** towns place "key landmark structures first and then fill out the rest" with more than
  70 rules (https://80.lv/articles/procedural-world-building-in-ghost-recon-wildlands).
- **Dwarf Fortress** simulates elevation, rainfall and drainage, and biomes emerge from them.
- **Minecraft 1.18** passes three noise fields through hand-authored splines, because raw fBm "everywhere kind of
  feels the same" (https://dawnosaur.substack.com/p/how-minecraft-generates-worlds-you).

**The common pattern:**
- landmarks first;
- authored categories and transfer curves;
- fields that encode cause;
- even point patterns thinned by those fields;
- transitions designed explicitly;
- randomness last and small.

## 3. Generative art: shaped randomness and curation

- **Distributions.** Tyler Hobbs:
  - uniform randomness "tend[s] to form clumps or runs far more frequently than an untrained person would guess";
  - Gaussian is for "approximately the same, but with some outliers";
  - power laws give "a balance of different sized objects … very few large objects"
    (https://www.tylerxhobbs.com/words/probability-distributions-for-algorithmic-artists).
- **Colour.**
  - Random colours per element give "terrible results", and a random palette pick is only "somewhat better". Use
    gradients, clumping, inheritance, and colour zones you can see "when you squint"
    (https://www.tylerxhobbs.com/words/color-arrangment-in-generative-art).
  - Weighted palettes, e.g. "a 70% chance of choosing white, a 20% chance of choosing blue, and a 10% chance of
    choosing red"; tiny hue jitter for an "organic feeling"
    (https://www.tylerxhobbs.com/words/working-with-color-in-generative-art).
- **A core motif never breaks.** QQL: "we didn't want that variety to ever break or obscure the core visual
  language" (https://www.tylerxhobbs.com/words/the-design-philosophy-of-the-qql-algorithm).
- **Disorder is a small budget, and a gradient.**
  - Vera Molnár's "1% de désordre" disrupts an orderly grid (https://www.rightclicksave.com/article/an-interview-with-vera-molnar).
  - Georg Nees's *Schotter* increases rotation and displacement row by row
    (https://www.artsnova.com/Nees_Schotter_Tutorial.html).
- **Shaped signals.** Quilez's shaping functions are envelopes for "music or animation": the exponential impulse
  "grows fast, decays slowly" (https://iquilezles.org/articles/functions/). Nature decomposes into "few big shapes …
  and a larger number of medium size shapes" (https://iquilezles.org/articles/fbm/).

## 4. Composition: value first, subordination, asymmetric balance, rests

- **Notan.** "The harmony resulting from the combination of dark and light spaces." Design in two values first.
  Dow's principles are opposition, transition, subordination, repetition and symmetry. Subordination is "elements
  thematically related to a single, dominating one" (https://stephenberryart.com/blog/2019/1/17/a-reading-guide-to-dows-composition-pt-1).
- **Gurney:**
  - shapewelding: "simple shapes are easy to recognize and remember";
  - a three-value study;
  - soft edges eased into, but "too much of this 'fiberfill treatment' can take the backbone out of a picture"
    (http://gurneyjourney.blogspot.com/2007/10/shape-welding.html,
    http://gurneyjourney.blogspot.com/2011/08/transitioning-edges.html).
- **Payne's design stems:** the steelyard (a large mass balanced by a small one far out), the S-curve, radiating
  lines, the tunnel. The steelyard avoids "lifeless symmetries" *(snippet)*.
- **Rule of thirds.** None of the composition sources lead with grid placement; they lead with value structure,
  subordination and asymmetric balance. **Thirds are a tie-breaker.**
- **Ma.** Miyazaki: "It's called ma. Emptiness. It's there intentionally… If you just have non-stop action with no
  breathing space at all, it's just busyness … If you just have constant tension at 80 degrees all the time you just
  get numb" (https://nofilmschool.com/2013/09/master-of-ma-director-hayao-miyazaki-set-to-retire).
- **Lynch's landmark:** the best nodes are "unique in some way and at the same time … intensify some surrounding
  characteristic". That is a good definition of a focal subject: the peak of the scene's own motif.
- **Common fate** (Gestalt): elements moving the same way at the same rate are seen as one object. **This is why a
  frame that pulses everywhere on the kick reads as one flashing rectangle.**

## 5. Environment art: detail, values, landmarks, light

- **70/30.** "30% of the space should contain 70% of the detail." Lit areas carry detail, and shadowed ones are
  simpler (Felix Leyendecker, https://80.lv/articles/environment-design-modularity-color-composition).
- **Values first.** "If your values are wrong then no matter what you do with your colours, the piece will fall
  apart" (https://www.exp-points.com/environment-artist-fundamentals).
- **Clusters, scale and restraint** (https://book.leveldesignbook.com/process/env-art):
  - make "clusters of related details" as an "asymmetrical fractal structure";
  - "START BIG";
  - "Don't guitar solo a dumpster".
- **The weenie.** Disney: "People won't go down a long corridor unless there's something promising at the end"
  (https://en.wikipedia.org/wiki/Weenie_(design)). Journey's mountain is visible from the first shot.
- **Ghost of Tsushima** "chose the dominant foliage in each area and significantly exaggerated its presence": peak
  shift applied to a region.
- **Shape language:** circles are friendly, squares stable, triangles dangerous or energetic. Combine a dominant shape
  with a secondary one.
- **Light and silhouette.** INSIDE uses "a soft, glowing backlight to silhouette characters", "dramatically bright
  slashes of light … long dark shadows", restrained colour, and haze for scale
  (https://www.pixelfondue.com/blog/2017/9/11/inside-inspiration-in-minimalist-lighting-and-composition). Three-point
  lighting keeps the fill at "up to half" the key.

## 6. Motion: hierarchy, staggering, harmonic rates

- **Disney's principles.**
  - Staging: "completely and unmistakably clear".
  - Follow-through and overlap: parts move "at different rates".
  - Slow in and slow out; arcs.
  - Secondary action supports the main action "without diverting attention"
    (https://en.wikipedia.org/wiki/Twelve_basic_principles_of_animation).
- **The VFX rhythm.**
  - The impact is "maximum number of elements. Biggest Contrast, High Saturation".
  - Dissipation is short, "Low Contrast, Low Opacity" (https://80.lv/articles/vfx-staples-shape-color-and-motion).
  - Riot: a weak attack must not look like an ultimate.
- **Stagger.**
  - SVGator: offsets in "50–100ms increments"; "offset gaps over 150ms or total sequences over 2 seconds feel
    sluggish".
  - Microsoft's Fluent: important elements get "more prominent movements and longer durations", and less significant
    ones move together in synchronised groups (https://fluent2.microsoft.design/motion).
- **Visual music.**
  - Fischinger: "My films are no illustrations of music." Music was "an architectural ground plan".
  - John Whitney: "If one element were set to move at a given rate, the next element might be moved two times that
    rate … So long as all elements obey a rule of direction and rate … pattern configurations form and reform. This
    is harmonic resonance" (quoted in https://jbum.com/papers/whitney_paper.pdf).
  - Saul Bass designed to "the counts".
  - The caution: Mickey Mousing, matching every movement to the music, is "out of favor … because of overuse"
    (https://en.wikipedia.org/wiki/Mickey_Mousing).

## 7. Colour: gamut, dominance, perceptual blending

- **Gurney's gamut masks:** "think not only which colors are included … but also which colors are left out"
  (http://gurneyjourney.blogspot.com/2008/01/color-wheel-masking-part-1.html).
- **Weighted dominance.** Hobbs's 70/20/10 and Fidenza's 50/25/10. Riot: "a healthy level of contrast only works if
  there is a dominant color". "Higher color saturation draws more focus." Avoid 0% and 100%.
- **Perceptual blending (Oklab).** In HSV "yellow, magenta and cyan appear much lighter than red and blue". Blending
  white into blue in CIELAB, CIELUV or HSV shifts the hue toward purple. Oklab keeps the hue
  (https://bottosson.github.io/posts/oklab/). The engine's palette blends in OKLab (ADR-1043).
- **A temperature axis motivated by light.** Teal-and-orange works because it separates the subject's temperature
  from the background's; the critique is that it goes wrong when the light does not motivate it
  (https://stephenfollows.com/p/is-the-teal-and-orange-look-over-in-movies).
- **Albers:** "a color is almost never seen as it really is". Judge colour in context, at final size, under the final
  light.

## 8. Perception: what reads as designed

- **Fractal dimension.**
  - Statistical fractals are preferred at D 1.3-1.5, the most common range in nature (Taylor,
    https://cpb-us-e1.wpmucdn.com/blogs.uoregon.edu/dist/e/12535/files/2016/02/Fractal-Fluency-Chapter-1mjdxj5.pdf).
  - Exact, symmetric fractals are preferred at higher D (Bies et al. 2016,
    https://www.frontiersin.org/journals/human-neuroscience/articles/10.3389/fnhum.2016.00210/pdf).
  - **So organic structure sits at moderate complexity, and an iconic symmetric subject (a mandala, a cathedral's
    rose window) may be denser.**
- **Spectral slope.** The amplitude spectrum falls at about −1.2 for paintings and −1.4 for natural scenes, and
  painters apply an "artist's gamma" (https://people.hws.edu/graham/Graham-Spatial_Vision07.pdf). This is a QA band
  for frames.
- **1/f motion.** Voss and Clarke: 1/f-driven music was judged pleasing, white noise "excessively random" and 1/f²
  "overly structured" (https://www.osti.gov/biblio/5333174). So ambient drift should be smooth, multi-scale and
  never white jitter.
- **Berlyne's inverted U** (complexity, novelty and uncertainty have an optimum) and **processing fluency** (Reber et
  al. 2004: figural goodness, figure–ground contrast and symmetry raise pleasure, especially "if … fluent
  processing comes as a surprise", https://pages.ucsd.edu/~pwinkiel/reber-schwarz-winkielman-beauty-PSPR-2004.pdf).
  The design implication is a readable subject plus one unexpected element.
- **Ramachandran and Hirstein:**
  - Isolation: art appeals most when it produces "heightened activity in a single dimension".
  - The generic viewpoint: the brain abhors "suspicious coincidences", such as tangents and accidental alignments.
  - Contrast extraction: cells respond to edges, not to homogeneous fields.
  - (https://www.dgp.toronto.edu/~hertzman/courses/csc2521/fall_2007/ramachandran-science-art.pdf)
- **Uniform random placement shows clumps and holes.** Poisson-disc sampling gives "substantially more detail and
  less noise" (https://bost.ocks.org/mike/algorithms/). A perfect grid is "overly stiff" (Hobbs). **Natural placement
  is even sampling inside clusters, inside gradients.**

## 9. The translation to audio-reactive scenes (the part no source gives directly)

The sources are about still frames and games. A live audio-visual scene adds time and music, and so it adds its own
procedural tells. These are inferences, grounded in the sources above and tested against our own renders (§10).

1. **The whole-frame pulse is audio oatmeal.** A frame whose every region brightens on the beat reads as one object
   (common fate), a visualizer. Our first Critic pass on the Sonic Garden measured exactly this: 98% of regions
   responding to onsets, 12% of mean luma (finding F001). **The reaction belongs to the subject and its designated
   responders. The background answers only to slow, structural change.**
2. **Every feature moving everything is unranked motion.** If the kick drives scale, hue, brightness and the camera,
   nothing about it is learnable. **One feature, one visual dimension per element** (Ramachandran's isolation, Short's
   legibility). The kick and the hat move different regions.
3. **Equal-magnitude reactions are equal-size oatmeal.** A hat that flashes like a kick has no hierarchy. Visual
   magnitude follows musical salience:
   - hats drive tertiary sparkle;
   - snares drive secondary responders;
   - kicks, bass and lead notes drive the subject;
   - phrases and sections change the composition, palette or camera.
4. **A world that mirrors the raw signal is Mickey Mousing.** Structure moves large things: sustained energy, the
   musical context (density, chord, legato), the identity of the sound. The signal moves small ones. Fischinger's
   "architectural ground plan" is our slow tier, and the hits are our ornament.
5. **Constant intensity is numbness.** Rests in the music must become rests in the picture (*ma*), and the scene's
   peak contrast is spent on its peaks. A live scene with no MIDI and no sound is a composed, quiet world (the
   waiting world), not a frozen one.
6. **Linear time is as procedural as uniform space.** Envelopes are asymmetric: fast attack, exponential release.
   Ambient motion is a slow multi-scale drift, and periodic elements run at harmonic ratios (Whitney), so the world
   has a pulse of its own between the notes.
7. **Context-free reaction is the audio form of context-free placement.** A note should happen *somewhere that
   means something*: where its pitch places it, on the thing that sings that register, in the world's own gesture
   (a swell in a garden, a ring in an observatory). Pass 1 of the Sonic Garden: "MIDI says a note happened, the sound
   decides its form."

## 10. What our own scenes taught us (Sonic Garden passes 1-2, the live pass, Liminal passes 1-3)

- **Primitives that read as primitives are the first tell.** The owner called pass 1 "visually primitive":
  - a ball at the centre of every world;
  - flat single-colour surfaces;
  - front light;
  - invisible fog.

  Pass 2 fixed it by giving surfaces their own structure (material programs that decide where light lives), each
  world its own hero and skyline, and light a direction and a source.
- **A centred hero among a uniform scatter is still the default.** The pass 2 stills show a subject at the centre with
  similar-sized elements round it. That is the unranked, stationary scatter. Every new scene places its subject off
  centre with an explicit size hierarchy.
- **Uniform edge glow is the line look's oatmeal.** The Procedural Space presets lit every crease of every column
  equally. The Liminal film read as authored when the lines were rationed: rooms dark except their own lamps, lines on
  the objects that mattered.
- **Glitch is authored when it is scheduled.** Liminal pass 3's corruptions measured "unmistakable" only once each
  landed on its beat and broke a clean, readable image (C10, C14).
- **Particles as dots read as noise** (Event Horizon look development, 2026-10-02).

## A) The tells, ranked, each with its fix (adapted to Sonic VFX)

| # | the tell | the fix | how the kit or the evaluator holds it |
|---|---|---|---|
| 1 | no focal hierarchy | one subject with the peak value contrast, chroma, edge hardness and distinctive motion | the scene's `composition.focalPoints`, and `sonicScene.composition.focal`; evaluator: saliency concentration and dominance ratio |
| 2 | uniform detail | 70% of the detail in 30% of the frame, round the subject | evaluator: detail concentration per tile |
| 3 | everything one size | hero : medium : small classes, about 1 : 3-5 : 10-30, or power-law sizes | particle `sizeSkew`, scale classes in the scene modules |
| 4 | uniform random placement | even sampling inside authored clusters and gradients | placements written as authored lists and rings; particle `clusterCount` |
| 5 | context-free placement | place by cause: debris where flow deposits it, glow where life is | scene modules place by role, never by scatter alone |
| 6 | stationary statistics | calm and busy regions; global structure first | composition depth bands; landmarks first |
| 7 | a random hue per object | a weighted palette in a gamut; colour from space and role | `sonicScene.palette` roles; palette states (ADR-1043) |
| 8 | saturation everywhere | peak chroma only on the subject and accents | depth-band saturation; evaluator: focal chroma share |
| 9 | flat lighting, no value plan | three value bands, one key, a rim on the subject | each scene places its key and rim in the world |
| 10 | weak figure–ground | silhouette against the brightest region; hard edges at the focus, lost edges at the edges | backlight or rim per scene; depth of field where it serves |
| 11 | everything moves at once | four motion tiers; staggered, grouped background motion | `sonicScene.motion`; chain presets per tier |
| 12 | linear or jittery motion | eased, multi-scale drift; springs with overshoot | camera loops; `springHz` (ADR-1041) |
| 13 | constant intensity | rests, and a peak saved for the peaks | the waiting world; sustain-gated growth |
| 14 | mirror symmetry of the layout | steelyard balance; symmetry only for an iconic subject | off-centre framing (`arc_camera` keeps the subject on a third) |
| 15 | suspicious coincidences | no tangents, no exact centring, no horizon at 50% | camera and placement review per scene |
| 16 | visible instancing | variants, hashed seeds, slope and wind alignment | variation seeds; rotation offsets per band |
| 17 | variation without identity | categorical differences between scenes | the catalog's distinctness table |
| 18 | ignored transitions | contact, barnacles, junctions | ground contact and clusters in the modules |
| 19 | total order or total chaos | a small disorder budget, as a gradient | noise amounts by role |
| 20 | uniform material response, emissive everywhere | differentiate roughness, value, chroma; emissive for meaning, at most about 5% of the frame at rest *(heuristic)* | material programs decide where light lives |
| 21 | reactions that linger | short, low-contrast dissipation | HIT and SNAP chain presets |
| 22 | detail without process | evidence of the forces that made the scene | the thesis names the forces (breath, gravity, current) |
| 23 | lazy framing | masses cross the frame edge; a design stem | each scene's camera crops its large forms |
| 24 | muddy gradients | interpolate in OKLab | the palette system (ADR-1043) |
| 25 | Mickey-Mousing every feature | structure to large changes, signal to small ones, scaled by importance | the per-scene instrument tables |
| 26 | the whole-frame pulse (audio oatmeal) | reactions on the subject and its responders | evaluator: share of audio-driven motion inside the focal mask |
| 27 | one feature moving everything | one feature, one dimension per element | each instrument row names one job |

## B) Practical generation rules (the Sonic VFX scenes follow these)

**The big decisions are authored**
1. **Write the thesis before any geometry.** It names the subject and the musical role of its parts.
2. **Choose exactly one focal subject.** Rank everything else as secondary or tertiary in the scene's design
   record. *Check:* one entry in `focalPoints` at weight 1; the evaluator's saliency peak overlaps it in at least 70%
   of sampled frames *(heuristic)*.
3. **Design the value plan first.** One value band covers at least about half the frame, and the subject sits at the
   strongest contrast. *Check:* a 3-level notan of a 64×36 thumbnail.
4. **Place the landmark first, then the secondaries, then the filler.** Filler never competes with the landmark in
   size or value.
5. **Balance asymmetrically.** The subject is off centre (a third or a golden section) and a smaller counter-mass sits
   farther out. A centred, symmetric layout is allowed only when symmetry is the scene's subject. *Check:* the mass
   centroid is off centre, and the left/right luminance masses differ by at least 15% *(heuristic)*.
6. **Avoid suspicious coincidences.** No horizon at 50%, no silhouette tangent to another, no hero within 2% of a
   centre line *(heuristic)*.
7. **Build depth as three to five planes.** Each farther plane has lower contrast and chroma, nearer the background
   value. *Check:* per-plane contrast and chroma fall with depth (the depth bands, ADR-038).

**The small decisions are shaped**
8. **Never place from uniform random.** Use authored rings, curves and clusters, with hashed jitter. Particles spawn
   from structure: a note's place, a surface, a curve.
9. **Use heavy-tailed sizes.** About 1 hero : 3-5 medium : 10-30 small per cluster, or power-law particle sizes
   (`sizeSkew` above 2).
10. **Spend disorder on organic filler, not on structure.** About 1-5% deviation on architecture, more on growth and
    debris, as a gradient away from the subject.
11. **Keep one motif and repeat it at three scales:** the subject, its secondaries, its surface (a spiral in the
    disk, its dust and its streaks; an arch in the nave, its windows and its tracery).
12. **Keep randomness deterministic and locally stable.** Seeded per element, so a live change morphs rather than
    reshuffles.

**Colour**
13. **Constrain each scene to a palette with roles:** dominant about 60-70% of the frame, secondary 20-30%, accent
    under 10%, highlight reserved for the subject and the hits.
14. **Never a random hue per object.** Colour comes from space and role, with per-element jitter under about 5
    degrees of hue.
15. **Reserve peak chroma for the subject.** *Check:* at least 70% of the most chromatic 5% of pixels lie near the
    focal point; the background's median chroma is at most half the subject's *(heuristic)*.
16. **Give each scene one temperature axis motivated by its light.** A warm key against cool shade, or the subject's
    temperature opposite its background's.
17. **Audio moves colour only along designed paths:** a palette position or saturation in OKLab, a light's
    temperature, a highlight's brightness. Lightness is held while hue moves, so a hue change never flickers.

**Light and material**
18. **One key direction per scene, fill at most half the key, and a rim or backlight on the subject.** Pools and
    slashes of light make the focal spot in a muted field.
19. **Differentiate materials** in at least two of roughness, value, chroma and emission. Emission is for meaning, on
    at most about 5% of the frame at rest.

**Motion**
20. **Declare four motion tiers per scene.** Adjacent tiers differ by at least about three times in timescale, and
    the background never moves faster than the subject.
21. **Ease everything.** Asymmetric envelopes with a fast attack and an exponential release; springs for heavy things.
    Linear only for rotation.
22. **Stagger group motion** in 50-100 ms steps from the event's origin, with the whole cascade under about 1-2 s.
    Background groups move in sync, as one slow mass.
23. **Drive ambient drift with smooth multi-scale motion** (camera loops of tens of seconds, slow noise), never white
    jitter.
24. **Shape every event as an impact and a dissipation.** The impact frame holds the event's peak contrast; the
    visual energy falls below about 10% within 0.25-1 s, scaled by the event's importance *(heuristic)*.
25. **Build rests.** When the music thins, the scene thins. The waiting world before the first note is composed,
    dim and still alive.

**Audio-reactive specifics**
26. **The reaction belongs to the subject and one or two designated responders.** The background answers only to
    slow, structural change. *Check:* at least 70% of the audio-driven motion energy lies in the focal region
    *(heuristic)*, measured by the engineer's evaluator.
27. **Scale the reaction to musical importance:**
    - hats: tertiary sparkle;
    - snares: secondary responders;
    - kick, bass and lead notes: the subject;
    - phrases and sections: composition, palette, camera.
28. **One feature, one dimension per element.** The kick never drives the hat's job.
29. **Structure drives the large changes, the signal the small ones.** Sustained energy, the musical context and the
    sound's identity move the world. Hits ornament it.
30. **A note happens somewhere that means something:** at its pitch's place, on the part that sings its register, in
    the world's own gesture.
31. **Map timbre to shape language:** a bright, rough or transient-rich sound to angular, fragmented forms; a soft,
    sustained sound to round, continuous ones (bouba/kiki).
32. **Different input classes must look different in kind, not only in amount.** A pad grows and warms. A melody
    places and travels. A bass moves mass. A drum flashes, bursts and cracks. *Check:* the test matrix (§21) records
    the expected and observed behaviour per class per scene.

**Process**
33. **Run the expressive-range check before calling a scene done.** Render it through every input class, measure
    (focal share, value structure, chroma share, motion inside the focal region), and look at the contact sheets.
34. **Kill a weak scene rather than polish it.** One concept per scene. A layer that does not improve the read or the
    music's mapping is removed.
