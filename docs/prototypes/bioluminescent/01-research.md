# Bioluminescent Environment: art research

Brief §3 and §4. The reference board is 45 images (`01c-reference-index.md`, with sources, licences and a light/lesson
note per image), and the synthesis is `01b-reference-synthesis.md`. Review copy, with the annotated board image:
`~/Desktop/av-gen-review/40-bioluminescent/cp1-reference-board/`.

This file is what the research changed in the plan. It is not a summary of the board.

## 1. What real bioluminescence does that "glowing forest" art does not

| Observation (board refs) | Consequence for the Environment |
|---|---|
| Darkness fills 60-95% of every real frame (01, 02, 07, 10, 12, 13, 20) | The rest state is mostly black. Emission is the exception, and the music earns the light |
| Emitters do not light their surroundings. Fungi leave the log black (01-03), glowworms leave the limestone dark (10). The only spill is a reflection on wet surfaces (04, 12) | At rest, organisms glow without lighting the world. Collective illumination of walls, river and haze is reserved for the drop: the one place the piece breaks realism, so it lands (§2 below) |
| The gamut is narrow: marine blue 470-490 nm, glowworm 487, fungal green 520-530, firefly yellow-green 560, red only as a rare accent (623 nm, 705 nm) | A blue-cyan world with rare warm accents. Violet and magenta do not emit in nature. Here they come only from fluorescence and iridescence (§3) |
| Emitters stay saturated and do not clip to white, except brief intense marine flashes (22) | Chroma retention stays on. Near-white is the signature of a flash, so the drop's peak and nothing else |
| Glow has internal structure: gills, segments, rows, lattices (04, 12, 25, 42) | Emission is masked per organ: photophore rows on crinoid arms, polyps on fans, beads on siphonophore chains. No flat-filled glowing shapes |
| Emitters line up along features: wave crests, ceiling cracks, wood grain, drainage lines (10, 13, 14, 16) | Placement follows the geography (river banks, wall ledges, gullies), and the propagation field follows it too |
| Clusters are size-graded: a few large bodies trailing into many specks (02) | Every species has a size ladder, and the micro scale (polyps, plankton) is where the count lives |
| Marine and air life flash on disturbance and decay in 0.1-3 s (13-15, 22). Fungi breathe slowly | Two behaviour classes: **disturbance responders** (plankton, polyps, pens: fast flash, refractory) and **slow breathers** (crinoids, fans: minutes-scale cycles). The music is the disturbance |
| Sea pens carry waves of light along themselves when touched (Pennatulacea; marine biology literature) | A real-world precedent for propagation inside one organism, and a micro-scale echo of the macro wave |
| At distance, emitters collapse into a haze of points (03, 09) | Distance representation is physically honest: far organisms become points, then a glow in the haze. That LOD *is* the look, not a compromise |

## 2. How film departs from reality, and what to take

- **Avatar (44).** It inverts reality: emitters cover the frame, light the characters, and a cyan ambient removes
  black. It gains readability and spectacle and loses mystery.
  - *Take:* rim-lit translucency, a strict hierarchy of emitter scales, a strict palette.
  - *Refuse:* the ambient that removes black, violet as a primary emission, the theme-park evenness.
- **Annihilation (45).** The strangeness is in the air, not in glowing objects.
  - *Take:* the atmosphere as a carrier of colour. In the drop the haze itself catches the wave.
- **Haeckel (40-43).** Morphology: siphonophore colonies, radiolarian lattices, medusae.
  - *Take:* form vocabulary for the species (chains of modules, feathered arms, lattices). Not light.
- **Rousseau (39).** Silhouette density and layered planes of vegetation.
  - *Take:* layering.

**The rule that comes out of it: realism at rest, spectacle by consent.** The world is dark, true to life,
and mostly unlit while nothing is happening. The music's build organises light along features. Only at the drop
does collective light flood the canyon, so the "holy shit" is a change of regime, not a brighter version of the
same picture.

## 3. Colour

Palette relationships, not a list of colours.

| Role | Colour | Where it comes from |
|---|---|---|
| Ambient | blue-black, `~(0.004, 0.006, 0.014)` | moonlight scattered into the canyon, dim |
| Primary emission | dinoflagellate blue to cyan (470-490 nm) | plankton, polyp mats, sea pens, crinoid photophores |
| Secondary emission | turquoise / cyan-green (495-520 nm) | lower canopy and some ground cover. Kept away from pure green, so it never reads as "neon forest" |
| Rare warm accent | amber (560-590 nm) | lanterns: few, the brightest per unit, warmth at a human scale |
| Rarest accent | deep red (620+ nm) | embers in wall crevices: barely visible, so most viewers never consciously see it |
| Fluorescence / iridescence | violet and magenta | sea-fan polyps **fluoresce** magenta only while the blue wave passes over them. Comb rows diffract a moving spectrum. Neither is a light source |
| Peak | near-white cyan | the flash cores at the drop, and nowhere else |

The magenta-under-blue rule is physically motivated (blue excitation, longer-wavelength emission), and it gives the
brief's "others open or change colour" a reason. A fan does not glow magenta on its own; the wave turns it magenta.

## 4. Composition and camera (brief §4.C, §4.D)

The research sources here are the board's atmosphere images (33-38) and established cinematography of dense
environments: nature documentaries' slider and drone moves through forests and reefs, and the long tracking flights
through canopies in cinematic environments.
- **Three planes, always.**
  - Foreground organisms frame the shot: pens and whips within 1-4 m, out of focus at the edges.
  - The midground holds the event: a wave front, a crinoid crown, the river catching light.
  - The background is the canyon's reach dissolving into blue haze. The canyon guarantees a background in every
    direction, so there is no edge of the world and no dead space.
- **Moves that work in dense environments:**
  - low passes just above the river under the canopy (parallax from the stalks);
  - rising reveals from the floor to the crowns, and from the crowns to the canyon's full length;
  - lateral tracking along a wall of fans (strong parallax);
  - slow orbits around an event;
  - macro push-ins on a pen with a wave running up it.
- **Rapid movement reads only with near parallax.** A fast flight needs the stalks and pens close to the lens,
  otherwise speed reads as nothing.
- **Energy, not beats.**
  - Quiet passages: long lenses, slow drift, small motion.
  - Builds: increasing forward speed.
  - The drop: traversal, a dive from the crowns to the river along the wave.
  - Cuts or moves land on phrases, never on every kick.

## 5. Density and perceived complexity (brief §7)

The scale ladder of the Rift, with the representation each scale wants (to be verified by measurement, §6 of the
brief):

| Scale | Organisms | Count in view (target) | Representation |
|---|---|---|---|
| sub-cm | plankton in the river, airborne spores | 10^5-10^6 | GPU particles / points |
| cm | polyps on mats, polyps on fans, photophores | 10^6+ (perceived) | points on host geometry (emitters), not tessellated beads |
| 0.3-4 m | pens, whips, lanterns, fans | 10^4-10^5 | instanced meshes with LOD, vertex sway |
| 20-45 m | crinoid giants | 10^2 | instanced meshes, hero detail |
| 150 m+ | canyon walls, reaches | 1 | terrain, haze |
| km | the canyon fading out | — | haze and distant points |

CP1 measured the general renderer drawing the cm scale as 20-triangle icospheres: 8.6 M submitted triangles at
24 k visible instances. That cost (`03-architecture.md`) is the first argument for a different representation at
the small scales.
