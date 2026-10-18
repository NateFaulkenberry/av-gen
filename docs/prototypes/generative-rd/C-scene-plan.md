# RD Pins scene: plan

Owner decision, 2026-10-17: build reaction-diffusion into a full live scene, starting from the C3 "pins" look.
The research and the per-axis debate are in `C-scene-research.md`.

**Where things live:**
- Generator: `tools/rd_pins/build.py`, which writes `examples/rd-pins/`.
- Media: `~/Desktop/av-gen-review/65-rd-pins-scene/`.
- Branch: `proto/generative-rd`.

## Decisions (from the debate, settled by the proofs in §3)

| axis | pick | why, in one line |
|---|---|---|
| World and composition | **A bowl of pins:** 220 × 220 pins at 0.4 m on a dish 88 m across, its walls rising to 18 m. The camera sits on the rim with a slow 90 s drift | The far wall rises above the eye line, so the frame is all pins: no horizon, no void, no flat plane |
| Pins | **Square pins** (boxes), 180-200 across, with a depth prepass. Changed by proof Q2 | Round pins cost 2.2 times as much (90.5 ms against 40.7) and look the same at the scene's distance |
| Pin material | **Oxidised shafts, copper heads.** Head colour runs from base to pattern with B; a thin emissive ring at the head on the B ≈ 0.2 isoline | A solid region reads as a plateau of bright heads, an empty one as a low oxidised field. Neither is a void |
| Environment | **The bowl's floor reads the pattern dimly (environment to base tint), plus a coloured dim sky for reflections.** No fog, no shadows | Designed darks at about 3-8%, coloured. The fog march (7 ms) and shadows (9 ms) bought nothing here |
| Pattern | **Labyrinth (0.029, 0.057) to spots (0.030, 0.062)** through a baked regime map: a wandering pool plus fbm drift. 320² cells, 12 iterations | The owner's chosen look: "labyrinth turning to spots" is the frontier itself |
| Audio | **The music edits the regime, not B.** A kick sends a ring that flips the labyrinth to spots where it passes, with a faint glint on the heads as it passes. Held sound widens the spot pool. Bass makes a slow lift of the field. Hats brighten the rings | No saturation, so no void (the C3 void came from kick rings of B). The pattern's own growth is the animation |
| Resonance | **Not in Stage 1-2.** A Stage 3 option only if a field slot is free (proof Q5) | The scene already fills 15 of the GPU's 16 field slots; with the resonator and its reader the pins' own fields fell off the end and the pins vanished |
| MIDI | CC 1 pattern (to spots), CC 2 growth, CC 3 pin height, CC 4 audio influence, CC 5 erase or starve, CC 6 ring light; CC 9 palette, CC 10 lock; notes 36-39 hold seeds | The Resonator scenes' pattern: six handles, a palette pair, a few gestures |
| Palettes | **Six role-based states** (Copper Night, Verdigris, Garnet and Indigo, Violet and Gold, Bone and Basalt, Ember). ADR-1420 selector on the phrase pulse, ADR-1421 change fallback | Roles bind to the pin, floor, sky and light constants. Pattern generation and palette are separate |
| Index | **A new category, "Generative"**, starting with RD Pins. Phase Wall can join it; the R&D harnesses stay in Experiments | A real live scene is not an experiment. Proposal: the coordinator decides. Until then it is in Experiments |

## Stage gates

**Stage 0. Proofs** (this document, §3).
- Gate: the coordinator reviews this plan and the proof stills.

**Stage 1. World and look.**
- The bowl, box pins with a prepass, materials (Q6, the rings), floor, light, camera and the six palettes as states.
- Media: one still per palette and a 20 s autonomous clip.
- Gate: no void anywhere (darks ≥ 3% luminance, coloured), the ring legible, live ≥ Medium at 30 fps.
  The owner's "copper rings on black" reads in Copper Night.
- **Fallbacks:**
  - Rings not legible: raise the ring gain and narrow the pattern-head brightness, so it reads as rings on dark
    heads, which is closer to C3.
  - Cost too high: box pins, or 180² pins.

**Stage 2. Life and music.**
- The regime pool and drift, the kick regime ring and glint, sustain to pool, bass lift, hat ring light, seeds.
- The MIDI map, the palette selector and fallback.
- Media: Feline Footwear, Trench and Hyperborea, 20 s each, the same camera as the autonomous clip.
- Gate: the kick gesture is legible on Feline Footwear; silence stays alive (brush and drift); no reset on a beat.
- **Fallback:** if a regime ring is too slow to read, add a faint B ring at 1/3 of the C3 rate. It was proven to grow,
  and is now capped under saturation.

**Stage 3. Live and checks.**
- `--live-profile --mode live` on all three tracks.
- `review_examples`, integrity, and the two suites. Index entry.
- Gate: 30 fps at 1080p on Auto with 0-2 misses, and every check exiting 0.

## 3. Proofs (Stage 0)

Media: `~/Desktop/av-gen-review/65-rd-pins-scene/proofs/`. Feline Footwear, 30-50 s, stills at 40 s.

| # | question | result | decision |
|---|---|---|---|
| Q1 | Does the bowl fill the frame? | **Yes.** From the rim the frame is pins edge to edge, labyrinth meeting spots in the middle. A sliver of the bowl's outer floor shows at the top edge, outside the pin square (tinted by the pattern, so not a void) | Keep the bowl. Stage 1 widens the pin square or tilts the camera so the sliver goes |
| Q2 | Round against square pins, cost and look | Round 5.9M triangles. Without a prepass: 104 ms round, 75 ms box (headless Ultra GPU median). With a prepass and no per-pixel onset read: **90.5 ms round, 40.7 ms box; box at 160 across, 32.4 ms**. They are indistinguishable at the scene's distance | **Box pins, depth prepass**, 180-200 across. Stage 1 gate: live at High or better |
| Q3 | Do the darks hold colour? | **Yes.** No pixel is black: oxidised shafts and oxblood floor between the heads. The floor tint barely shows under the pins (the pins cover it); it matters at the bowl's edge | Keep the floor tint (cheap). Stage 1 checks the ≥ 3% luminance gate numerically |
| Q4 | Kick as a regime ring against kick as a ring of B: is there still a void? | **The regime ring works and makes no void:** kicks spread the spot region outward in rings from the centre, and the labyrinth heals behind them. The growth variant did not inject at all: the field budget (Q5) pushed its fields past slot 16 | **Kick edits the regime.** The faint per-pixel glint costs about 10 ms (an onset field per pixel), so it is dropped; the regime ring itself is the visible gesture |
| Q5 | Resonance sand as a regime layer | **Failed for a structural reason:** with a resonator plus its reader the scene has 17+ fields, and fields past the 16th are not uploaded. The pins' height and colour fields were dropped and the pins vanished | Not in this scene unless Stage 2 frees slots. The resonator idea stays in `recommendation.md` for a scene built around it |
| Q6 | The C3 "copper rings" | The ring band (B 0.17-0.27) is about one pin wide at 0.4 m pitch. At ×3 gain it only brightens the spots' outlines; it does not read as rings yet | **Stage 1's main art task:** heads on the pattern go darker copper, and boundary pins go bright and emissive (a wider band, 0.12-0.3). That is the C3 look, on the lit bowl |

**The field budget** is now a design constraint: 16 GPU field slots, 4 per material program.
- Stage 2 must fit the music into the slots left.
- It can replace the four MIDI seed spheres with two, and fold the brush into the seeds' compound.

## Engine

None is planned. The scene uses ADR-1450-1452 and existing kinds. If a gap appears, I tell the coordinator before
touching any shared file. ADRs would take numbers from 1453-1459.

## Log

**Stage 0 approved, 2026-10-17.** The coordinator's notes:
- performance is a hard gate (Medium or better on loud passages, misses under 1%);
- six genuinely different, colourful palettes;
- the rings;
- remove the sliver;
- category Experiments for now.

**Stage 1 (world and look), done.**
- **Changes:**
  - box pins, 190 across (0.46 m pitch), with a depth prepass and no per-pixel onset glint;
  - three MIDI seeds (the field budget);
  - rings: heads on the pattern are dark polished metal, boundary heads (B 0.11-0.29) carry a bright ring at ×2.4,
    so spots read as copper rings on dark;
  - camera at (10, 31, 31) → (−1, 0, −5): no floor sliver anywhere in the 90 s drift.
- **Darks:** at 1080p, p5 luminance is 3.7% and 2.6% of pixels fall under 3%. These are the gaps between pins at
  grazing angles; none is black.
- **Live** (`--mode live`, Auto, 30 fps, 1920 × 1080, Feline Footwear, 10 s windows of 300 frames each):
  - at 40 s: High (1632 × 918), GPU 27.9 / 30.5 ms (p50 / p95), 0 misses;
  - at 95 s (loud): Ultra (1920 × 1080), GPU 30.4 / 34.0 ms, 0 misses;
  - CPU work about 5 ms.
- **Known:** the first ~10 s after a cold start show the seed blobs before the pattern forms (a pre-roll is a
  Stage 2 item).
- **Media:** `~/Desktop/av-gen-review/65-rd-pins-scene/stage1/`.

**After Stage 1 (the coordinator's notes, done):**
- **The regime keeps moving.** A soft 30 m linear front sweeps back and forth across the bowl over 96 s, with an
  orbiting pool of spots and an fbm wiggle on the line.
- **The labyrinth family is coral, not maze.** Spots survive in maze territory, so the frame had gone to one field of
  rings and stayed there. Coral stripes invade spots, so the two families trade ground both ways. In silence, over
  96 s, the labyrinth grows to about half the frame by 36 s and the spots reclaim it by 60-96 s; the boundary is in
  frame throughout.
- **Pre-roll.** The scene opens on a formed pattern: 3000 numpy steps of the same model, written as authored
  values (ADR-1453, 1.6 MB with compact arrays).

**The palettes stage, done.**
- Six role-based states (ADR-1043), bound to:
  - the pin and floor programs;
  - the key and rim lights (colour and intensity);
  - the sky and fog;
  - the pins' metal and roughness.
- The ADR-1420 selector advances on the phrase pulse in the order 0-4-3-1-5-2, with the ADR-1421 change fallback;
  CC 9 selects and CC 10 locks (Stage 2).
- Stills: `palettes/contact-sheet.png`.

| palette | darkest 5% (sRGB luminance) | pixels under 3% |
|---|---|---|
| Copper Night | 2.8% | 6.0% |
| Verdigris Reef | 4.8% | 1.6% |
| Garnet and Indigo | 3.2% | 4.1% |
| Violet and Gold | 4.0% | 2.2% |
| Electric Lagoon | 5.7% | 0.7% |
| Rose Quartz | 6.1% | 0.3% |

The remainder under 3% are the slots between pins at grazing angles, coloured, not black.

**Stage 2 (audio and MIDI), done.**
- **The instrument** (`instrument()` in `tools/rd_pins/build.py`). The music edits the regime map and the light, never
  B:

  | input | target | effect |
  |---|---|---|
  | kick | the onset ring in the baked regime (depth = Audio Influence) | Spots spread outward in rings from the centre, and the labyrinth heals behind them |
  | held sound | the spot pool +0.6 (3 s attack, 6 s release) | The spot region widens |
  | bass | pin height (effector 2) +3 | A slow swell of the field |
  | hats | ring light ×(1 + 1.5·hat) | The rings brighten |
  | snare | the wandering brush +2 | A burst of new growth where the brush is |

- **MIDI:**

  | input | control |
  |---|---|
  | CC 1 | pattern (toward spots) |
  | CC 2 | seeding |
  | CC 3 | pin height |
  | CC 4 | audio influence |
  | CC 5 | erase (feed falls; the pattern collapses and regrows on release) |
  | CC 6 | ring light |
  | CC 9 | palette (select one of six) |
  | CC 10 | lock |
  | notes 36-38 | seeds (velocity is the rate; the release sets how long they keep seeding) |

  As before, the controller routes are wired but untested with hardware; renders flag them live-only.
- **Media:** `~/Desktop/av-gen-review/65-rd-pins-scene/stage2/`, the same camera throughout: silent, Feline 40-60 s,
  Trench 60-80 s, Hyperborea 20-40 s. Kicks visibly spread the spot field (Feline against silent), and the palette
  journey advances on Trench (to Electric Lagoon).
- **Live** (`--mode live`, Auto, 30 fps, 1920 × 1080):

  | track and start | window | Auto level | GPU p50 / p95 | misses |
  |---|---|---|---|---|
  | Feline 40 s | 10 s | Medium | 28.1 / 49.2 ms | 5 / 300 (1.7%) |
  | Feline 40 s, rerun | 20 s | High | 29.3 / 32.1 ms | 0 / 601 |
  | Feline 40 s, second rerun | 20 s | High | 28.6 / 31.5 ms | 0 / 602 |
  | Feline 95 s | 10 s | High | 30.3 / 32.6 ms | 0 / 301 |
  | Trench 60 s | 10 s | High | 28.7 / 32.8 ms | 1 / 301 |
  | Hyperborea 20 s | 10 s | High | 29.4 / 32.2 ms | 0 / 301 |

  The first Feline run's misses did not reproduce in either rerun; it is likely Auto settling, under contention from
  other agents' GPU work between runs. CPU work is about 6 ms.
- **The index.** "RD Pins" (`examples/rd-pins/`) is in Experiments, live. The R&D harness it grew from is renamed
  "RD Pins (R&D)" and hidden.
