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
| Pins | **Round pins:** 10-sided cylinders with a bevelled head (box as the fallback if the cost proof fails) | Round heads catch a point highlight from every direction, which is the pin-art look; rings read as rings, not steps |
| Pin material | **Oxidised shafts, copper heads.** Head colour runs from base to pattern with B; a thin emissive ring at the head on the B ≈ 0.2 isoline | A solid region reads as a plateau of bright heads, an empty one as a low oxidised field. Neither is a void |
| Environment | **The bowl's floor reads the pattern dimly (environment to base tint), plus a coloured dim sky for reflections.** No fog, no shadows | Designed darks at about 3-8%, coloured. The fog march (7 ms) and shadows (9 ms) bought nothing here |
| Pattern | **Labyrinth (0.029, 0.057) to spots (0.030, 0.062)** through a baked regime map: a wandering pool plus fbm drift. 320² cells, 12 iterations | The owner's chosen look: "labyrinth turning to spots" is the frontier itself |
| Audio | **The music edits the regime, not B.** A kick sends a ring that flips the labyrinth to spots where it passes, with a faint glint on the heads as it passes. Held sound widens the spot pool. Bass makes a slow lift of the field. Hats brighten the rings | No saturation, so no void (the C3 void came from kick rings of B). The pattern's own growth is the animation |
| Resonance | **Decided by proof Q5 (§3)** | Welcome if it reads; not at the price of the clarity of the kick gesture |
| MIDI | CC 1 pattern (to spots), CC 2 growth, CC 3 pin height, CC 4 audio influence, CC 5 erase or starve, CC 6 ring light; CC 9 palette, CC 10 lock; notes 36-39 hold seeds | The Resonator scenes' pattern: six handles, a palette pair, a few gestures |
| Palettes | **Six role-based states** (Copper Night, Verdigris, Garnet and Indigo, Violet and Gold, Bone and Basalt, Ember). ADR-1420 selector on the phrase pulse, ADR-1421 change fallback | Roles bind to the pin, floor, sky and light constants. Pattern generation and palette are separate |
| Index | **A new category, "Generative"**, starting with RD Pins. Phase Wall can join it; the R&D harnesses stay in Experiments | A real live scene is not an experiment. Proposal: the coordinator decides. Until then it is in Experiments |

## Stage gates

**Stage 0. Proofs** (this document, §3).
- Gate: the coordinator reviews this plan and the proof stills.

**Stage 1. World and look.**
- The bowl, round pins, materials, floor, light, camera and the six palettes as states.
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

These are filled in below as the proof renders land. They live in `~/Desktop/av-gen-review/65-rd-pins-scene/proofs/`.

## Engine

None is planned. The scene uses ADR-1450-1452 and existing kinds. If a gap appears, I tell the coordinator before
touching any shared file. ADRs would take numbers from 1453-1459.
