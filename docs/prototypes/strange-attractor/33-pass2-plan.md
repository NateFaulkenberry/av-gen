# Art Pass 2: the plan (Gate R2)

Follows from `32-pass2-research.md`. The brief is `30-art-pass-2-brief.md` and the rules are in `31-…-notes.md`.
ADR range 1885–1929. Milestones M1–M5 each stop for the owner.

## 1. The journey at a glance

| # | World | Time (s), long cut | Portal out |
|---|---|---|---|
| 0 | Canyon, day: Matrix, portal 1 (unchanged) | 0–32.0 | the river's wall (exists) |
| 1 | Moon: sunlit plain → terminator → dark basin, crater domes | 32.0–79.6 | the hero crater's floor, re-sited at the basin's edge |
| 2 | Canyon, dusk: out of the east wall (exists), the three Sasquatches at a fire | 79.6–103.4 | an outer canyon wall, set back from the river |
| 3 | Mars, Cydonia: plain, mesas, three pyramids | 103.4–143.1 | a fracture in the face of the middle pyramid |
| 4 | Canyon, night: the same campsite, embers, three Sasquatches and a Gray, the parked UFO | 143.1–164.9 | under the river: a portal in the riverbed |
| 5 | Ocean: deep water, the continental shelf, the base, submersibles | 164.9–198.7 | the launch bay's opening |
| 6 | UFO interior: observer, the exam, the Gray sees us, the beam | 198.7–224.5 | ejection: a portal at the camera, torn open by the beam |
| 7 | Canyon, dawn: high aerial return, the goat ritual, the landing | 224.5–262.2 | none (the end) |

All crossings land on bar lines (121 BPM: bar 17 at 32.0 s, bars every 1.983 s).

## 2. Architecture

### 2.1 A journey graph (ADR-1885)

Pass 1 nests worlds: each composition owns its portal's destination. Pass 2 returns to the canyon three times, which
is a cycle (canyon → Moon → canyon → Mars → canyon → ocean → UFO → canyon). It cannot be a nesting.

- The root holds **named worlds** and an ordered **journey**: the portal legs, each from one world to the next, each with
  its own corruption site, exit, rotation and flight.
- The camera is one continuous pure path across all legs. Each leg's crossing is found from the previous leg's path,
  exactly as now (ADR-1883).
- **Only two worlds are ever drawn:** the current one and the one behind the next open portal. A renderer per world is
  created lazily, and released two legs after it was last needed, which bounds memory.
- The existing nesting stays valid as the two-leg case. No compat shim: Pass 1's scene is rewritten as a journey.

### 2.2 One canyon, four times of day (ADR-1886)

The canyon is **one world**, drawn from one generation (ADR-1853). It carries a schedule of looks:
- states: day, dusk, night, dawn;
- each a full lighting set: sun, sky, cirrus, fog, fills and water;
- switched at the legs' crossings (never on screen: when the canyon is visible through a portal it already wears its
  next state).

No canyon geometry is duplicated, and no second terrain is uploaded. Today the dusk world duplicates the flattened
meshes; that goes.

### 2.3 Engine features (generic, with tests)

| ADR | Feature | Why |
|---|---|---|
| 1885 | journey graph: named worlds, ordered legs, lazy renderers | a cyclic route, bounded memory |
| 1886 | world look states on a schedule | one canyon at four times of day |
| 1887 | lunar terrain: moderate curvature + designed shadow basin; earthshine | a physical terminator |
| 1888 | shell fur or alpha-card fur on skinned meshes (after the M1 test decides) | Sasquatch fur at a distance |
| 1889 | a portal at the camera (ejection) | the UFO ejection through a screen-filling tear |
| 1890 | a surface occluder for volume shadows (an underwater ceiling) | light shafts underwater |
| 1891 | observer treatment: an arrival transient of aberration, echo and edge reconstruction | brief 6.4 |
| 1892 | flicker and soft-additive beams with haze | forehead beams |

Scene-only work (no engine changes): the domes, the Mars terrain mode and pyramids, the base and launch bay, the
interior kit, the cast, the campfire, the timing, the routes.

## 3. Beat sheet (long cut, about 262 s)

| Time (s) | Beat | Camera | Notes |
|---|---|---|---|
| 0–32 | canyon day, Matrix, portal 1 | as approved | no regression; before/after of 0–16 s at every milestone |
| 32–44 | Moon: full sun, the crossing's reveal | glide, low | the sun high behind us; Earth ahead |
| 44–56 | toward the terminator | glide over the ridge | rims only lit; shadows reach toward us |
| 56–70 | across into the dark basin | descend into shadow | earthshine; first dome glow behind a rim |
| 70–78 | the domes | slow pass, the domes at 1–3 km | warm spill on crater walls |
| 78–79.6 | the crater portal | drop into the floor (as now) | the portal re-sited at the basin's edge, lit by a dome |
| 79.6–92 | dusk canyon: out of the east wall | as now, low over the water | the dusk light approved at C2 |
| 92–98 | the Sasquatches at the fire | pass by at 80–120 m | a gravel bar under the far wall, about 6 s readable |
| 98–103.4 | the outer wall opens | climb toward a set-back wall | seam → tear; Mars seen through it |
| 103.4–115 | Mars: establish | out of a mesa wall onto a plain | haze, a butterscotch sky |
| 115–132 | the pyramids grow | long approach | scale reveal against the mesas |
| 132–143.1 | the pyramid portal | toward the middle pyramid's face | stone fractures, the Matrix language |
| 143.1–150 | night canyon: the same campsite | out of an outer wall, descend | the embers are seen first |
| 150–158 | the gathering and the UFO | arc past at a distance | three Sasquatches, the Gray, the parked saucer |
| 158–164.9 | the river dive | over the water, down into the river | a riverbed portal glows under the surface |
| 164.9–176 | ocean: the deep and the shelf | slow descent along the cliff | shafts, particulate |
| 176–190 | the base | approach | lit architecture, the launch bay, two or three craft moving |
| 190–198.7 | the launch bay portal | into the bay's mouth | the interior seen through it |
| 198.7–207 | UFO interior: arrival | still, high in a corner | the observer transient decays |
| 207–216 | the exam | almost still | Nordics, Grays, the man on the gurney |
| 216–222 | the Gray sees us | very slight push | the head turns, eye contact, the triangle lights |
| 222–224.5 | the beam and the ejection | the image tears | mosh + geometric corruption |
| 224.5–238 | dawn: high over the canyon | aerial glide | first warm light on the rims, a mirrored river |
| 238–250 | the goat ritual | fly past at a distance | two Sasquatches, the triangles, the beams, the goat |
| 250–262 | the landing | descend to a ledge or a bar, settle | the end; nothing more happens |

**A 210 s cut** (if the music stays as it is):

| World | Long | Short |
|---|---|---|
| Moon | 47.6 s | 36 s |
| dusk | 23.8 s | 18 s |
| Mars | 39.7 s | 28 s |
| night | 21.8 s | 18 s |
| ocean | 33.8 s | 24 s |
| UFO | 25.8 s | 22 s |
| dawn | 37.7 s | 32 s |

Every environment loses about a quarter. The brief says not to rush, so the long cut is my recommendation.

## 4. Music (owner decision 2)

Feline Footwear is **210.5 s**; the long cut is about 262 s.

- **A. Extend Feline Footwear** to about 262 s from its own stems (drums, bass, synth): repeat one 16- or 32-bar section,
  placed where a portal crossing hides the seam. One track, the whole journey. Recommended, if the owner is happy for
  his track to be re-arranged.
- **B. Cut the sequence to 210 s** (the short cut above).
- **C. Two tracks.** Feline Footwear to the UFO, then another of the owner's tracks from the ejection. The violent
  transition is a natural musical break.
- **D. Another, longer track** chosen by the owner.

## 5. The cast (owner decision 1 covers the gaps)

| Role | Plan | Source and licence | Polycount | Rig | Still | Fallback |
|---|---|---|---|---|---|---|
| Suited man | MPFB human, CC0 suit with shirt and tie, posed lying and strapped | MakeHuman/MPFB, CC0 | 65k | 163 bones | `01-cast/mpfb-suited-man-turntable.png` | none needed |
| Nordic humanoids (×2–3) | MPFB tall, symmetrical; restyle: pale skin, blond tint, simple pale garments (recoloured MPFB suits or a sculpted robe) | MPFB, CC0 | 48–59k | 163 | `mpfb-nordic-female/male-turntable.png` | — |
| Gray aliens (×3) | MPFB slim base, cranium enlarged, almond eyes, grey skin; forehead triangle | MPFB, CC0 + original build | 39k | 163 | `mpfb-gray-base-turntable.png`; `02-proofs/07` | a Sketchfab CC-BY Gray (needs an account) |
| Sasquatch (×3) | MPFB heavy base sculpted to an ape body and head, 2.3 m; fur per §4.4 of the research | MPFB, CC0 + original build | 39k body (+ fur) | 163 | `mpfb-sasquatch-base-turntable.png`; `02-proofs/05` | a Sketchfab CC-BY Sasquatch (needs an account) |
| Goat | owner decision 1: a CC-BY realistic goat from Sketchfab or Sloyd, or the library goat restyled | Sketchfab/Sloyd CC-BY, or the farm pack (purchased) | 2k (library) | yes | `library-farm-pack-goat-turntable.png` | a Blender build (large art task) |
| UFO (parked) and submersibles | flexunit saucer + emissive windows, underside lights, a weathered material | `assets/imported/ufo.gltf`, CC0 | 4.2k | no | `library-flexunit-ufo-turntable.png` | a Blender saucer build |
| Gurney and props | built in Blender (`02-proofs/06`) | original | small | no | `02-proofs/06` | Sketchfab CC-BY stretcher (needs an account) |
| Pyramids, domes, base, interior | built in Blender (procedural scripts, committed) | original | — | no | `02-proofs/02`, `06` | — |
| Surfaces (sand, rock, metal) | Poly Haven / AmbientCG | CC0 | — | — | — | — |

Animation: the Universal Animation Library (Quaternius, CC0) retargeted to the MPFB rig for idles and small gestures,
or MPFB's own pose library. The Gray's head turn is a hand-keyed bone animation.

## 6. Milestones (brief §11, with the stops)

Each milestone delivers stills, short motion tests, the sequence so far with sound, the flicker metric, before/after
of 0–16 s, and both suites at exit 0.

- **M1: Moon extension and the first encounter.** ADR-1885/1886/1887; the terminator and basin; the domes; the crater
  portal re-sited; the Sasquatch build and fur test (ADR-1888); the campfire; the dusk leg with the encounter; the
  canyon as one world with states.
- **M2: Mars.** The Mars terrain mode, mesas and haze; three pyramids; the pyramid portal; the return to the night
  canyon (leg only).
- **M3: Night and ocean.** The night state; the gathering with the Gray and the UFO; the riverbed portal; the ocean
  shelf and shafts (ADR-1890); the base and bay; submersibles; the bay portal.
- **M4: UFO interior.** The interior kit; the Nordics, Grays and the man; the observer treatment (ADR-1891); the head
  turn, the triangle and the beam (ADR-1892); the ejection (ADR-1889).
- **M5: Dawn and the end.** The dawn state; the high return; the two Sasquatches with triangles and beams; the goat
  (rotation, glow, local mosh, disappear/reconstruct); the flyby and the landing; the full render.

## 7. Risks

| Risk | Plan |
|---|---|
| Fur flicker at a distance | test card fur against shell fur in M1; TAA and alpha-to-coverage; measure on video |
| Memory with eight worlds | the journey graph draws at most two and frees the rest |
| The Sasquatch reads as a man in a suit | silhouette first: head, arms, mass; judged at 100 m in fire light before anything else |
| The goat's realism | owner decision 1 |
| Seams across four canyon states | states switch only when the canyon is not on screen |
| Running time and music | owner decision 2 |
| Ejection mosh too violent for flicker sensitivity | it is the brief's one explicit exception; keep it to about 2 s, measured |

## 8. Decisions for the owner

1. **Sketchfab account** (or Sloyd) for CC-BY downloads: a realistic goat, and fallbacks for the Gray, the Sasquatch
   and a gurney. Otherwise the goat is the library goat restyled and everything else is built from MPFB.
2. **Music:** A (extend Feline Footwear from its stems, recommended), B (cut to 210 s), C (a second track from the
   ejection), or D (a different track).
3. **The terminator's scale:** a moderately curved Moon (radius about 150–200 km, a closer horizon than the real
   1737 km) plus a designed shadow basin. The real Moon cannot show a terminator crossing within a 40 s flight.
4. **The crater portal moves** to the dark basin's edge, so the domes are seen before it. The floor opening stays as
   approved.
