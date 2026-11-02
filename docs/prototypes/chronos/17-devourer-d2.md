# CHRONOS, The Devourer: Gate D2, the procession and a single soul consumed (Tests B and C)

Media is in `~/Desktop/av-gen-review/104-chronos/devourer-d2/`. The master composition is three-quarter, with the D1c
face.

## What was built

1. **ADR-1934, identity gating** (engine; `docs/decisions/ADR-1934-the-slit-can-be-gated-by-identity.md`). This is
   the plan's approach A.
   - A node's `"temporalSlot"` (1..16) puts its pixels in a slot ring (R8Uint) beside the colour ring. The ring is
     captured from the identifier target through a per-entity table, at the same layer and in the same frames as the
     colour capture.
   - The gated sampler reads history only where a stored texel was a gated object. So the face and the background
     never smear, however long the souls' trails are.
   - Each slot has a **trail** where the object has been, with echo spacing and fade, and a **fracture**: a random
     moment for each strip across the body, perpendicular to its travel.
   - The squeeze into the mouth is the node's own geometry.
   - Tests: `[adr1934]`, 4 unit cases and 3 GPU cases. They cover a never-smeared ungated object, a gated trail, idle
     slots changing nothing, fracture, FIR (a late start lands on the same frame), the ring released when the gate is
     off, the node key both ways, and the slot table both ways.
2. **The souls** (`build_souls.py`, MPFB CC0).
   - Eight adults of varied sex, age, build and height. Each is posed lying flat with its own limbs (arms at the
     sides, arms up, a knee bent, curled, one arm reaching, limp, turning, arms forward), and the pose is baked into a
     static mesh.
   - Each wears a loose grey-white shift, shoulders to mid-thigh. It is lofted from the torso's outer silhouette,
     slice by slice, and hangs straight down from the shoulders and chest. So it never follows the body's forms:
     neutral, never sexualised.
   - The skin is pale grey and flat; the eyes are blank.
   - About 16k triangles each.
   - Two earlier versions were rejected. A shroud offset from the skin read as a leotard. A relaxed one shrank back
     inside the body.
3. **The creature's `devour` clip** (in `build_scan_creature.py`, committed with D1c).
   - It is a 6 s loop: the jaw hangs open (34-41°) and gulps to 54-56° every 3 s.
   - The head is turned 11-14° toward the procession and the camera, and the eyes are on the incoming souls.
   - The portrait's `idle` clip is unchanged.
4. **The procession** (`build_d2.py`, which writes `devourer-d2.scene.json`, `devourer-d2-procession.json` and
   `devourer-d2-single.json`).
   - **Path and spacing.** Fifteen souls, one per slot, ride a path laid out in the master camera's screen space. It
     runs in from beyond the right edge, beside and behind the head, sweeps toward the camera in front of the neck, and
     climbs into the mouth's centre. The souls are 3 s apart, so one arrives at every gulp.
   - **Timing.** Each soul takes 20 s on the path, slowing as it nears the lips, then is pulled into the throat. While
     it is not on the path, it is parked off frame.
   - **Scale.** The souls are about 3× human size, about 5 m: hand-sized against the 33 m head, as Saturn's victim is
     in Goya.
   - **Stages.** Every stage is a pure function of the soul's place on the path, keyed on the timeline as positions,
     orientations, scales and slot parameters, so it is scrub-safe:
     - **A, intact.** Far away.
     - **B, echoes.** Two copies, 8 frames apart, closing up.
     - **C, the stretch.** A continuous trail of about 46 frames, which reads as ridged folds of the body's own
       history.
     - **D, fracture.** Strips across the body at different moments.
     - **E, the squeeze.** The node narrows into a ribbon, half as wide and nearly twice as long, and shrinks to
       nothing in the throat as the jaw gulps.
   - **The ring.** 64 layers, stride 1, a span of 60 frames.
   - **Light.** A narrow, cold spot runs down the far procession. A wider one reached the face and flattened its
     chiaroscuro.
   - **Audio.** Feline Footwear, with the drum stem as the beat. Nothing is routed to audio yet; that is D3.

## Files

| File | What |
|---|---|
| `devourer-d2-procession-three-quarter.mp4` | **Test B**: 24 s of the procession with sound |
| `devourer-d2-single-soul-three-quarter.mp4` | **Test C**: 24 s, one soul through A to E, swallowed at the gulp at 20.2 s |
| `devourer-d2-single-stage-{a,b,c,d,e}.png` | Test C at each stage (8.1, 11.5, 14.7, 17.9 and 19.8 s) |
| `devourer-d2-procession-8.2.png`, `devourer-d2-procession-17.2.png` | Test B stills |
| `devourer-d2-stages-sheet.png` | the five stages and a procession frame on one sheet |

## Performance

The offline renders ran at about 70 fps at 1080p (10.2-10.6 s for 720 frames), with the gated pass and 15 souls, and
with 0 GPU errors. The memory added is the slot ring: 960×540×64 bytes = 33 MB. Per-pass profiling is Test F, at D3.

## Honest critique

- **Works.**
  - The procession reads at a glance as single file, from the right into the mouth.
  - The face and the background stay perfectly sharp while the souls smear: the gating does its job.
  - The stretch (C) is the strongest image: a soul trailing the ridged folds of its own past, close to reference r12.
  - The squeeze into the maw at the gulp reads.
- **Weak.**
  - Stages A to C happen far down the path, where a soul is small. Test C's early stages are hard to see at the
    master framing. In D3 the stages should start later on the path, or the near approach be made longer.
  - The fracture (D) reads as fine vertical combing more than as body parts at different moments. It needs fewer,
    wider strips and a drum kick (D3).
  - The discrete copies give a ribbed comb on fast motion. It is part of the look, but the final pull into the throat
    is fast enough to make it coarse.
  - Near the mouth, a soul passes in front of the upper teeth before it enters. The mouth's interior treatment
    (accumulating, older images receding into the throat) is D3.
