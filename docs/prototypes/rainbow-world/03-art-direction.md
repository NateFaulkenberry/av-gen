# Rainbow World 03: proposed art direction

Gate 1, §4 and §15.3-4 of the brief. This document is a proposal, and every decision in it lists the evidence for it
and what remains uncertain. The experiments it cites (E1-E3) are in [05-gate1-experiments.md](05-gate1-experiments.md).
The images are in `~/Desktop/av-gen-review/74-rainbow-world/gate1/`.

## 1. What the references ask for

From the board (`01-reference-board.png`):

- **R1 (cloud-ring vista).** Clouds are the largest shapes in the frame. They are warm where the sun hits and
  blue-violet in shade. Rolling hills recede in layers into haze. Saturated flowers appear only in the foreground,
  and the rainbow is framed by other forms.
- **R2 (candy double rainbow).** Rainbows can be thick, nested and solid-looking. The far slopes may carry spectral
  banding.
- **R3 (meadow valley).** The rainbow is vivid *against a dark backdrop*. A low warm sun rakes across the rolling
  hills, the lit hills are yellow-green and the shade is teal-green, and small tree clusters sit on the ridgelines.
- **R4 (waterfall gorge).** Distinct tree types are recognisable by silhouette, for example the weeper and the
  blossom tree. Rock pinnacles stand as vertical landmarks, mist separates the planes, and moss-covered boulders
  carry the supporting detail.
- **The clip.** A full-frame fisheye, several gravities, landmarks that anchor each roll, and no cuts
  ([01-clip-analysis.md](01-clip-analysis.md)).

Measured colour (`02-palette-board.png`, k-means):
- The references share a **cobalt-to-cyan sky**, **olive and yellow-green meadows**, **warm peach light** and
  **blue-violet shadow**.
- R2 is the outlier: it is magenta-dominated.

## 2. Identity in one sentence

**"The Rainbow Knot": a lush meadow world shaped as one immense, closed, twisting band of land that loops over and
under itself through an all-round sky of sculpted clouds, stitched together by luminous rainbow arches. You travel
along its surface, and wherever you are, "down" is the ground under you and the rest of the world hangs overhead.**

Painterly, not cartoon:
- Forms are big and designed. Colour moves in gradients.
- Shadows are soft, cool and keep their colour.
- Surface breakup is restrained.
- Rainbows are the only fully saturated thing in the frame, together with small flower accents.

## 3. The world form: approaches debated

The question is how to get the clip's continuous traversal with changing gravity, a frame that is always full, and no
visible edge to the world, all inside a live budget.

| | Approach | For | Against | Verdict |
|---|---|---|---|---|
| A | **A closed twisting band of land** (a hand-shaped knot-like loop; meadow on both broad faces, designed cliffs at the rims; camera up = local surface normal) | Gravity changes come from real geometry, which is what the clip does. Strands crossing overhead put land upside down in the sky, our version of the clip's walls and stairs. It is one continuous surface, so there are no cuts. Seek-exact (the camera is a function of the path). It exports as ordinary meshes. | The rims are a visible edge unless they are designed as landforms. One sun lights only one face at a time. The design is novel and needs proving. | **Recommended.** Proven in grey-box by E2 |
| B | Manifold Garden world wrapping (a world cell repeated in 3D) | Fills every direction by construction. Precedent exists, and AV Gen has the ADR-1042 journey with screw repetition. | Natural land repeated reads as tiles. Hundreds of copies cost a lot. | **Adopted as a supporting layer only**: distant, hazed "echoes" of the knot fill the deep sky |
| C | Inside a cylinder or sphere (an O'Neill habitat) | Land fills the frame everywhere; gravity rolls with the camera. | Loses the blue sky that the brief lists as foundational. It reads as a tunnel or bowl, which the owner forbids. | Rejected |
| D | Vertex "world-curvature" bend of a conventional landscape | Cheap and familiar. | Rubbery and view-dependent. It breaks shadows and AO and is not spatially coherent (§14 Motion), and a strong bend becomes a tunnel. | Rejected as primary; may be a local accent later |
| E | Separate floating islands with gravity switching between them | Mario-Galaxy precedent. | Islands have undersides and edges by definition, and the empty sky between small islands is dead space. | Folded into A: the band's crossings play the islands' role |

**Evidence (E2 grey-box, `09-E2-knot-world-greybox.png`):**
- Hugging the surface with a 180-degree fisheye does produce land overhead (s=0.09) and land rising into a wall
  ahead as the band twists (s=0.33 and 0.41).
- This is the clip's spatial behaviour with no world deformation and no spinning.

**What E2 also showed is wrong, and will drive the Stage 4 design:**
1. The band is too narrow and the knot too open, leaving 40-60% of the frame sky. The real band needs to be
   **3-4x wider relative to the gaps** (about 250-400 m wide on a 1.5-2 km loop), with tighter crossings.
2. The plain grey rims read as the edge of a road. Rims must be **landforms**: grassy ridges that roll over into
   cliffs with hanging gardens, root curtains and trees growing sideways. One face then continues into the other.
3. One frame went black because the camera was inside a cloud blob. Cloud keep-outs round the path are mandatory.

**Owner rules checked against approach A:**
- **No dead space.** The sky is a designed, deep, all-round sky with cloud masses, strands of the knot and echoes in
  every direction. Never black, never empty.
- **No visible edge of the world.** The world is a closed surface. Its rims are cliffs that lead to the other face.
  Nothing ends in a cut.
- **No vortex, funnel or tunnel hero.** The strands are broad land, never tubes. The camera never flies down the
  axis of a ring, and rainbow arches are passed under in profile, not used as portals.
- **Real terrain.** Every surface is sculpted meadow.

**Uncertain:**
- U2: whether the knot reads as a place or as an abstract sculpture.
- U3: whether the shaded faces read as shade or as dullness.

Both are tested in the Stage 3 and 4 experiments.

## 4. Orientation cues (legibility while rolling)

These are the rules that let a viewer feel which way is up when the horizon is gone:

1. **The sky has a vertical gradient that means something.** World up is a deep cobalt zenith. World down is a
   luminous **sea of clouds** lit warm from below by the sun's bounce. These are world-space cues and never move.
2. **The sun has one fixed direction.** Shadows stay consistent across the whole knot, so light is the anchor while
   gravity turns.
3. **Everything that grows points away from its own ground.** Trees, grass and flowers are the gravity compass of each
   strand, which is the role the clip's figures and stairs play.
4. **Landmarks recur.** Each loop of the knot passes the same three or four hero landmarks from different sides:
   the great tree, the twin arch and the rock spire. The viewer builds a map as in the clip.

## 5. Palette and colour

`02-palette-board.png` has the swatches with linear values.

| Group | Rule | Evidence |
|---|---|---|
| Sky | Cobalt zenith (#1C4BB8) to mid blue (#4C9BE8) to haze (#A8D8F0). A faint violet high-sky tint for depth. | Every reference except R2 is cobalt to cyan (k-means) |
| Greens | **Seven greens, never one.** Sunlit yellow-green (#A6CF3A), meadow (#56A62E), deep grass (#2F7D35), cool shadow teal-green (#1F5E58), olive moss, lit and core canopy. Hue shifts toward yellow in light and toward teal in shadow. | R3 lit/shade split; R1 olive meadows |
| Earth and stone | Warm sandstone in light, cool slate in shadow. Used sparingly as rims, outcrops and spires. | R4 pinnacles; R2 rock teeth |
| Light | Warm key (#FFE2AE). Green bounce under canopies. Blue fill in shade (#3B5196). | R1/R3 warm-cool split |
| Rainbow | Six spectral stops, fully saturated, **emissive and focal only**. | Brief §4.2 |
| Accents | Magenta, coral, lilac and buttercup flowers: under 3% of any frame, mostly near the camera. | R1 foreground band |

**Saturation budget.** The rainbows and the flowers are the only colours at maximum chroma. Everything else sits
at 60-80% of its possible chroma, so the rainbows win.

E3 showed that AgX desaturates bright emission, and that a bow over a pale sky turns pastel. **Rainbows are
therefore placed against darker backdrops**: cloud shade, the deep zenith, shaded hillsides (R3's lesson). Their
emission is kept under AgX's desaturation knee. Chroma comes from contrast, not from brightness.

**Looks (MIDI CC 9 selects, CC 10 locks; ADR-1420 palette selector, blended in OKLab).** Proposed states:

1. **Bright Noon.** The default.
2. **Golden Hour.** Warm, long shadows; R3.
3. **After the Storm.** Slate cloud masses, wet saturated greens, the most vivid rainbows; R3's backdrop taken
   further.
4. **Lavender Dusk.** Violet sky, peach clouds, rainbows glowing brighter; R1's cloud palette.
5. **Prism.** Far slopes take on spectral banding; R2's idea, in a restrained form.

Each state is a set of colours for these roles: sky, cloud light, cloud shade, sun, fill, haze, grass lit, grass
shade, canopy, rainbow gain and flower gain.

## 6. Shape language

- **Terrain.** Broad, designed swells with a clear rhythm: big, medium, small. They are long and wave-like along the
  band and asymmetric, with soft crests and occasional steeper shoulders. **No noise as the primary form.**
- **Rims.** Rolling-over ridges, cliff bands of layered stone, hanging root curtains, and waterfalls only if the
  budget allows (deferred).
- **Trees.** A family of four designed silhouettes. Each one must be recognisable at 200 m by silhouette alone.
  1. **The Great Tree.** A hero landmark with a broad layered canopy in 5-7 masses: a "Ghibli bubble" canopy on a
     twisting trunk.
  2. **The Weeper.** A willow-like curtain, from R4.
  3. **The Blossom Puff.** A pink or lilac canopy, used sparingly, from R4.
  4. **The Ridge Pine.** A tall narrow cone for vertical rhythm.

  Each has 2-3 variants by proportion, not by noise.
- **Rocks.** Rounded, softened boulders with a clear top plane (where moss grows), and a few tall spires as
  landmarks.
- **Clouds.** Cumulus masses of 3-6 major lobes with a flat-ish base and a towering crown. They are layered from
  big through medium to small along the edges. **Designed silhouettes, authored as lobes.**
- **Rainbows.** Arcs with **thickness** (about 6-12 m across a 120-300 m arc). They come in three forms:
  - a single great arch that bridges two strands;
  - nested twin arcs (primary and secondary);
  - short "rainbow falls" that pour off a rim into the cloud sea (optional, decided in Stage 2).

## 7. Materials

The target is painterly PBR: real lighting, with surfaces that carry designed gradients.

- **Ground.** The base colour is a painted gradient: a slope- and curvature-driven lit-green to teal-shadow ramp,
  baked from Blender or computed in a material program (`OklabRamp`, `CurvatureMask`, `HeightBlend`). A low-frequency
  colour breakup sits on top (patches of olive and yellow). Roughness is high (0.85-0.95) with no specular sheen.
- **Grass.** Clumps of blades with **normals bent toward the clump's dome**, so a field shades as soft rolling
  volume and not as noise. The tips are lighter. Clumps sway with one global wind.
- **Foliage.** Leaf cards with normals transferred from the canopy hull, and alpha cutout. The back-lit glow is
  approximated (see the technical assessment). Colour is a ramp from the core to the outer leaves.
- **Bark and stone.** CC0 PBR textures (ambientCG or Poly Haven) as a **breakup** layer only, multiplied into
  designed colour. Detail is kept at the scale the camera sees.
- **Rainbow.** Emissive, soft-edged and translucent. A spectral ramp across the width, a brighter inner skirt, a
  dark gap and a faint secondary. **No surface specular, no hard edges.**
- **Clouds.** Volume or lobes, with a two-tone ambient (cool blue-violet below and in the core, warm on top), a
  silver lining and warm bounce from the cloud sea below.

## 8. Lighting and atmosphere

- **The key** is a warm sun at about 35-45 degrees to the knot's main plane. The direction is fixed in the world
  (the orientation anchor).
- **Ambient** is sky and cloud-sea light: cool from above, warm from below. On the shaded faces of the band this
  gives *cool shade with colour*, never black. The shade faces become a designed second mood (dappled, glowing
  flowers, rainbow light pools) rather than a failure of light. That is uncertainty U3.
- **Rainbow light.** Coloured light spills onto the land under each arch (E3d).
- **Atmosphere** is a distance haze toward the horizon-haze colour. The other strands overhead fade to sky blue with
  distance, so depth reads in every direction. Light shafts only where a cloud edge crosses the sun, and only if
  they are cheap.
- **Post.** Mild bloom on emissive only. AgX. No heavy grading. **Lighting is not rescued in post.**

## 9. Motion language

Translated from the clip ([01-clip-analysis.md](01-clip-analysis.md) §6):

- The camera travels along the band's surface at 2-6 m above it, with up taken from the surface's local normal, so
  gravity turns as the band twists.
- About every 20-40 s comes a **sequence** like the clip:
  1. a landmark is acquired;
  2. the camera holds on it through a roll;
  3. it pitches over a rim or a crossing onto a strand of different gravity;
  4. it resolves upright;
  5. it pulls back through an arch.

  Between sequences the camera cruises calmly.
- Speed is steady with soft accelerations, decelerating into each resolution.
- **The camera is a pure function of the transport position (seek equals play).** Audio never integrates into camera
  speed. Audio may add only bounded offsets that are functions of current signals: lift, a little FOV breath, a lean.

## 10. Audio direction (later stages, after the silent baseline is approved)

| Element | Signal (existing vocabulary) | Behaviour |
|---|---|---|
| Rainbow brightness and chroma | `response.level` / `response.sustain` (ADR-1510 adaptive) | Bounded swell, slow release |
| Light travelling along the arches | `response.kick` | One soft pulse runs along each arch from foot to foot |
| Rainbow shimmer and sparkle | `response.hat` | Fine shimmer in the spectral bands; pollen glints |
| Cloud inner glow | `response.snare` | A soft, slow glow inside a cloud mass, never a flash |
| Wind (grass and canopies) | `response.bass` (slow) | Gust strength |
| Look | `beat.phrasePulse`, or the change detector (ADR-1421) | Advance the palette state unless CC 10 locks it |

MIDI follows the house layout. **CC 9 selects the look and CC 10 locks it.** The other CCs are proposed in
04 §7 and agreed with the coordinator before Stage 7.
