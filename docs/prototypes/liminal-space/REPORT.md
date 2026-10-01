# Liminal Euclidean World: completion report for *All You Got*

*The art agent, the night of 2026-09-30 to 2026-10-01. Written to disk by the coordinator, because the harness refused
the subagent's report write. This answers the brief's §22 list of ten items. It judges the result from the rendered
film and the analyzers' measurements, not from "it renders". The governing documents are `00-brief.md` and the
owner's addendum `01-addendum-emotion.md`, which wins where they differ.*

**The film:** `~/Desktop/av-gen-review/24-liminal-space/all-you-got-final.mp4`, from commit `60594dfa`. It is
1920x1080 at 30 fps, h264 at quality 90 with AAC (the song muxed in), and runs 253.8 s. The iterations sit beside it
(`all-you-got-v1.mp4` to `-v5.mp4`). The analyzer outputs and contact sheets are in `analysis/renders/`.

## Verdict

**What works.** It is a complete, continuous, art-directed film: one walk through seven plain, pale, impossible
places, with no cuts.
- The colour moves only on the song's section boundaries.
- The sky is withheld until a doorway frames it at "feel it grow".
- The ending resolves the loop into open air and a doorway around the sun.

Its strongest stretches do what the addendum asks: they make you feel lost in a large, quiet, beautiful place.
- the intro's stair that returns to the film's opening frame;
- the figure standing in a doorway two rooms ahead, and then gone;
- the stop at the stairhead above the fog at bar 42;
- the small room that grows until a doorway opens on the first sky;
- the ember corridor whose end never gets closer;
- the stair into the sky.

**What doesn't yet.** It is not finished work.
- The hall reads large rather than alive.
- Verse 2 and the Penrose stairwell are busier than they are moving.
- The eight thresholds of light are one device used for every change of world, and they are still faintly visible.
- The figure can only stand still.
- There are no light shafts.

**The measurements.** The analyzers find no flicker and four minor snaps (the chapter swaps). One major finding
remains, and it is a false positive.

## 1. What was researched

- **Art research (`ART-RESEARCH.md`, Phase A).** It covered:
  - Escher's impossible architecture (*Relativity*, *Belvedere*, the Penrose stair);
  - liminal spaces;
  - Hammershøi's rooms seen through doorways and Hopper's sunlight;
  - Friedrich's figure seen from behind;
  - games and films of wandering: Journey, NaissanceE, The Exit 8, Superliminal, *Last Year at Marienbad*;
  - continuous-take films (*Russian Ark*, *Rope*, *Birdman*, *1917*);
  - music videos built on one continuous move (*Star Guitar*, OK Go);
  - how sound becomes visual behaviour (Chion, Eitan and Granot, Whitney);
  - colour (GRIS's additive palette, Valdez and Mehrabian, Scriabin's two timescales).

  It ends in ten rules that every shot was checked against. Among them: "the camera is the lost person, not a drone";
  "give the wandering a beacon"; "repeat with one small change"; "colour is feeling, added and withheld"; "let silence
  be real"; "the exit is clarity, not spectacle".
- **Procedural-graphics research (`ENGINEERING.md`, by the engineering agent).** It covered:
  - domain warping;
  - SDF repetition and screws;
  - damped springs and rate integration;
  - OKLab colour blending;
  - the journey camera.

## 2. The artistic direction selected

*Someone is lost in an enormous, empty, beautiful building whose rooms connect wrongly. They keep walking, and in the
end the building opens and they walk out into the light.*

- **A strange dream of an ordinary building.**
  - Plain plaster, too-tall corridors, doorways without doors, straight stairs and landings.
  - Luminous air that is paler than the walls, so depth reads as haze, not darkness.
  - No ornament, no white wire-frame edge lines, no fluorescent strips (except the loop's sodium lights), no props.
- **One continuous walk.** The camera is a wanderer at eye height: it holds, hesitates, looks out of a window and
  looks back. Each change of world is hidden in a threshold of light.
- **Four motifs:**
  - **the beacon:** a warm doorway always one room away, until the film passes through it on the fill into bar 92;
  - **the stair:** it returns to its own corridor; it goes down into fog at the first "let it go" and up into the
    sky at the second;
  - **the figure:** a small silhouette seen from behind;
  - **the sun patch:** sunlight with no sun.
- **The colour script K0-K12:** grey dawn; rose and cobalt; dusk; grey surrender; quiet teal; gold; a single ember hue
  for the loop; then daylight and the full range of colour, ending in white.

## 3. Song structure and analysis

This is `SONG-ANALYSIS.md`, made with the art agent's own tools, not AV Gen's labels.
- **Length and tempo:** 253.8 s, exactly 116 bars of 4/4. 109 BPM to bar 75, then a step to 111 BPM on the downbeat of
  bar 76.
- **Harmony:** C Dorian (Cm-Cm-Gm-F), lifting to F at bar 67 ("feel it grow"). The final loop's bass descends
  Ab-G-F-F.
- **Dynamics:** a compressed track, so the story is told by arrangement rather than level.
- **Three families of texture:**
  - the sparse family: intro, break, "is that all you", outro;
  - the C groove;
  - the F world: "feel it grow" onwards.

  "Feel it grow" previews the ending, and "is that all you" returns to the intro's texture. The film turns both into
  places.
- **The owner's §12 map holds**, with four corrections:
  - the dance section is 8 bars;
  - "let it go" is a one-bar drop (bar 42) followed by six stripped bars;
  - the tempo steps up at bar 76, not during "feel it grow";
  - the release at bar 92 is sustained, not punched.
- **The data:** the section map, vocal timings, colour keys and a 64-event timing sheet are in
  `tools/liminal/all-you-got.sections.json`.

## 4. The director plan, as built

`DIRECTOR-PLAN.md` is the full plan. As built:

| bars | section | what the viewer sees |
|---|---|---|
| 1-17 | Intro | A pale, too-tall corridor: windows full of fog, a warm doorway far away, one bar of silence and stillness. A side door leads to a stair that climbs back over the corridor and arrives, one storey up, in the film's opening frame. The same stair is taken again towards a growing light. |
| 18-25 | Dance | Out of the light on the downbeat, high on the wall of an empty hall with a rose wall. The beacon is a lit doorway high in the far wall, out of reach. A tiny figure stands on a platform beneath it. A stair from nowhere leads to a door in mid-wall. The camera descends the long stair on the axis. |
| 26-41 | Verse 1 | Dusk rooms joined by aligned doorways, one lamp in each. A dark figure stands in a doorway two rooms ahead. The camera turns through a side door, and the figure is gone. |
| 42-48 | "Let it go" | Everything stops at a stairhead over a sea of grey fog, with the figure on a ledge ahead. Then a slow descent, a look back up at the figure, and a patch of sun sliding across a landing. |
| 49-58 | Rebuild, verse 2 | A narrow passage, then bridges and flights at contradictory levels. A change of mind (up two steps and back down), then down to a lit doorway. |
| 59-66 | Bridge | The verse 1 walk repeated through the same rooms with the lamps on the other side. The figure's doorway is empty. |
| 67-75 | "Feel it grow", break | A 3 m room full of gold grows into an 18 m hall. A doorway opens on sky and a sun, for the first time. At the break the camera stands in that doorway. |
| 76-83 | "Is that all you" | The first corridor again, ember-coloured, with sodium lights passing. Its lit end recedes as fast as the camera walks, and the corridor narrows and lowers. |
| 84-91 | "Is that all?" | Four flights round a well arrive at the same landing. Slivers of sky open in the walls. On the fill, the camera walks into the beacon's doorway. |
| 92-99 | Release | Out of the light into open air. The stairwell's walls drift apart and sink, and a long stair rises into the sky. |
| 100-113 | "For your life" | Free-standing doorframes, one per call, and the figure at the top. At bar 108 the first corridor is seen far below, small and roofless. Three fragments then slide into one doorway around the sun. |
| 114-116 | Outro | The air brightens to white. |

Cut or changed from the plan, and why:
- the impossible window in verse 2 was cut for time;
- the void and the crossing stairwell share one world;
- the Penrose cracks show an emissive sky plane rather than the real sky, which stays withheld until bar 92;
- the verse 1 figure stands rather than walks, because a walking figure ignores its tint.

## 5. What was implemented

All of it is data on the engineer's infrastructure (ADR-1040 to 1044). **No engine code was changed.**

- **The generator, `tools/liminal/make_all_you_got.py` (about 1,400 lines),** writes
  `examples/liminal/all-you-got{,.scene,.rig}.json` from the section map.
  - **Seven SDF worlds,** all within the 96-node limit and all at the origin:
    - a corridor whose storeys repeat forever (a vertical screw, its seams inside the floor slabs);
    - a hall;
    - rooms seen through aligned doorways, three rows on a translation screw;
    - a void stairwell;
    - a room that grows from 3.4 m to 18 m;
    - a Penrose stairwell (a four-cell helix screw);
    - the open.
  - **Nine journey chapters,** each swapped inside a threshold of light. A threshold thickens the air sevenfold, lifts
    the palette's lightness and opens the exposure by about 1 EV.
  - **The walk is a speed profile,** with holds, hesitations and the tempo step. It is integrated into smooth
    distance keys and fitted so the camera arrives where the timing sheet says, on the beat. The music never moves
    the camera.
  - **The palette, K0-K12,** is bound to:
    - every world's surfaces;
    - the fog;
    - the sky's zenith, horizon and ground (which is the interiors' real ambient light);
    - the lamps and beacons;
    - the figures.
  - **42 sparse routes,** each scaled by a timeline source:
    - the bass makes the world breathe, through a spring into a slow warp; it is absent at bar 42, the break and the
      loop, and largest in the release;
    - the warp's flow follows the music's energy and stops in silence;
    - the voice warms the beacons;
    - the treble makes nearby walls shiver, only in bars 76-91;
    - eight authored trembles mark the phrase-end noise sweeps.
- **A CPU test,** `[liminal][journey][allyougot]`. It walks every chapter, as far as its walk goes, against the
  world's true distance field, at the state the camera meets it in, and requires the camera never to need the
  collision guard. It passes.
- **A workflow that caught silent failures:**
  - a GPU-free load check, which must show 0 warnings, because a scene that fails to load renders the default orb
    scene without complaint;
  - stills, single-frame checks and contact sheets;
  - frame-by-frame tracking of the sun disc, which found the jitter.

## 6. The quality analyzer

- **What it is:** the engineer's `tools/liminal_critic.py`.
  - `temporal` measures the §16 checks from pixels and the song: unanswered build-ups, snapping and flicker, colour
    moving too fast or churning, and whether quiet sections are quieter than their neighbours.
  - `inputs` hands the Creative Critic an SDF project, with the addendum's emotional questions.
- **Both were run on every render.** What each finding changed:
  - snaps at the swaps led to thresholds made of thick bright air;
  - "let it go" being too close to its neighbours (8 %, then 13-14 %) led to a dimmer, emptier stairhead and a slower
    look back (15.1 % in the final);
  - the unanswered build at bars 5-8 led to the walk picking up on the bass pulse;
  - clipped thresholds led to haze instead of white;
  - the wobble led to the world-offset jitter being found and removed.
- **No analyzer code was added in Phase B.** Two problems remain:
  - the Critic's audio-reactivity analyzer errors on these inputs (`AttributeError: 'str' object has no attribute
    'get'`), so every job is PARTIAL;
  - the temporal breakdown rule matches words in section names, which produces the one major finding left (on
    Verse 1).
- **Neither tool can judge loneliness.** The addendum's emotional questions are answered by viewing, in the scorecard
  below.

| measure | v1 | final |
|---|---|---|
| temporal: snaps | 7 (1 major) | 4, all minor |
| temporal: unanswered build at bars 5-8 | major | gone |
| temporal: "let it go" against its neighbours | 8 % (major) | 15.1 % (passes) |
| temporal: flicker | 0 | 0 |
| temporal: colour findings | 5 minor | 3 minor |
| Critic issues (high / medium) | 66 (11 / 35) | 58 (7 / 29) |
| Critic scores: technical / lighting / motion | 0.55 / 0.69 / 0.67 | 0.65 / 0.80 / 0.68 |

## 7. Render location

`~/Desktop/av-gen-review/24-liminal-space/all-you-got-final.mp4`, 1920x1080, 30 fps, 253.8 s, with the song. The
iterations are beside it, and the test renders are in `tests/`. A full render takes about 12.5 minutes at 10.1 fps.

To reproduce it, run `python3 tools/liminal/make_all_you_got.py`, then:
`tools/gpu-lock.sh ./build/release/src/avgen --headless --project examples/liminal/all-you-got.json --render out.mp4 --range 0:253.79 --size 1920x1080 --fps 30 --codec h264 --quality 90 --particle-warmup 120`

## 8. LIVE mode

Not started; the addendum makes it a separate deliverable. What transfers (`DIRECTOR-PLAN.md` §15):
- the worlds, as walkable cells;
- bass breathing through the same spring;
- the walk's pace from the live energy, through `integrate`;
- the beacon driven by the voice;
- the palette on MIDI controls.

What doesn't transfer is the story: the withheld sky, the loop and the exit.

## 9. Known limitations

- **One transition device.** Every change of world is a threshold of light, and four still show as small jumps.
- **The figure can't walk.** A moving figure ignores its tint and renders as a pale mannequin. Up close the figure
  always reads as a mannequin, so it is kept distant.
- **No light shafts,** because the fog has no occlusion.
- **Weaker stretches:**
  - the hall reads large rather than alive;
  - verse 2 is busy;
  - the Penrose climb is fast;
  - the intro's turn through the arrival door is quick;
  - the pale palette can look washed out.
- **Reactivity is subtle by design.** The music-to-picture correlation measures about zero (-0.05), because the
  loudest bars are deliberately the calmest image.
- **No MIDI.**
- **The Critic's camera-wobble flags (22):** 8 are the swaps; the rest are small or on low-texture fog.

**Engine and analyzer gaps found tonight** (none fixed in Phase B; for routing):
1. **Large world offsets (1-6 km) jitter the image,** up to about 20 px at 1080p (float precision in the camera or
   reprojection path). Worked around by keeping every world at the origin.
2. **The node tint doesn't reach a figure whose position is keyed or journey-anchored.**
3. **Fog has no occlusion,** so there are no light shafts. This would need shadow-map sampling in the volume march.
4. **Journey paths always close towards the screw's far image.** An open-path option would be cleaner.
5. **A particle node outside any chapter drew in another world.** Listing it in its chapter fixed it.
6. **The SDF soft shadow made grain on cut door jambs,** so it was removed.
7. **The open world sits at the 96-node limit.**
8. **The rig's ambient light does nothing without an ambient-role light.** Interiors are lit by the procedural sky,
   whose colours the palette drives. Worth a line in the art guide.
9. **The temporal analyzer's breakdown rule matches words in section names.** An explicit breakdown flag in the
   section map would fix it.
10. **The Critic's audio-reactivity analyzer error.**

## 10. Recommended next steps

1. **The owner reviews the film.**
2. **Engine:**
   - fix or document the large-offset jitter;
   - add fog occlusion for light shafts;
   - make the tint work on moving figures;
   - add an open-ended journey path.
3. **Art:**
   - more than one kind of transition, for example a dark doorway or fog alone;
   - a warmer, livelier hall;
   - the verse 2 impossible window;
   - a slower door turn and Penrose climb;
   - a walking figure once its tint works.
4. **Analyzers:**
   - an explicit breakdown flag in the section map;
   - the Critic's audio-reactivity fix;
   - let the analysis know where the authored thresholds are.
5. **LIVE:** build it on two of the worlds.

## Scorecard: §21, judged from the film

| criterion | verdict |
|---|---|
| 1 a coherent world | **yes:** one material and light language across seven places |
| 2 a journey through impossible space | **yes:** the stair returning to its own corridor, the stair from nowhere, Penrose, the growing room, the receding end, the corridor seen from above |
| 3 Escher and liminal, not imitation | **yes** |
| 4 continuous movement | **yes**, with deliberate holds; no cuts |
| 5 smooth evolution | **yes:** 0 flicker; snaps only at the swaps |
| 6 colour with the music | **yes:** on section boundaries; 3 minor colour findings |
| 7 distinct audio behaviour | **partial:** subtle by design |
| 8 intentional audio and MIDI | **audio yes; MIDI not used** |
| 9 objects belong | **partial:** the figure works only as a distant silhouette |
| 10 quiet gives room | **yes:** bar 42 is 15 % calmer than its neighbours, the break 26 % |
| 11 transitions are meaningful | **yes:** place, palette and pace change on the bar |
| 12 the climax is earned | **largely:** withheld sky, beacon passed through, stair up instead of down; it reads as calm clarity rather than euphoria, as the addendum allows |
| 13 the analyzer finds no major breakdown | **one major left,** and it is a false positive |
| 14 extractable to LIVE | **the vocabulary is parameters;** LIVE was not built |

**The addendum's test** (feeling, or demonstration?): the corridor rhyme, the doorway figure, the stairhead, the
growing room, the ember loop and the stair into the sky make you feel something. The Penrose climb and the crossing
stairwell lean towards demonstration.
