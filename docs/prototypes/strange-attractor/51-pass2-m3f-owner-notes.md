# Art Pass 2, M3f: the owner's notes on M3e

The owner is reviewing M3e (7fe07cae; full render at
`~/Desktop/av-gen-review/101-strange-attractor/pass2-m3e/10-full/strange-attractor-m3e-full.mp4`). Nothing is
merged. This file collects their notes as they arrive, so it grows.

## 1. Keep

- **The night lighting.** The owner says the M3e night camp lighting is "MUCH better". Keep it. Do not push it
  darker or cooler. (The coordinator's own M3e read, that it looked flat and like dusk, is overruled.)

## 2. The night camp layout

- **Move the craft.** It currently sits behind the Sasquatches. Move it to the other side of the characters,
  near the river, so it is closer to the alien and not behind the Sasquatches. The group reads as: the
  Sasquatches round the fire, the Gray facing them, and his craft beside him by the water.
  - Ground it properly: it must not hover through the bank or sit in the river, unless it is clearly landed
    on the shore.
  - Re-check the night pass-by camera path, so it doesn't clip the craft and the craft reads as part of the
    group as the camera passes.
  - Re-check the lighting: the craft's lights now fall on the alien's side.
- **Give the craft a beat-pulse light animation.** Its lights (rim, window band, underbody glow) pulse on the
  beat, using the scene's existing beat signal. Make it a tasteful pulse, not a strobe, and keep it seek-safe.
  The spill onto the ground and the alien pulses with it. Keep the night look the owner likes.
  - If the hero UFO v1 is integrated later (it adds 48 rim light cells, 4 navigation lights and an underbody
    ring), carry the pulse over to those lights.

## 2a. The night river streaks

- At about 112 s (see `pass2-m3e/05-night/112s-m3d-left-m3e-right.png`), the night river shows white streak bands
  and vertical streak artifacts that were not in M3d. They are likely a side effect of the M3e lighting changes
  (AgX, moon bias and cascades, reflections of the sky or moon). Find the cause and fix it without changing the
  night look the owner likes. Show the river at 108, 112 and 114 s, M3e against M3f, as stills and a short clip.

## 2b. The ocean (open items carried from M3e)

- **Camera wobble over the reef.** M3e brought in a small new wobble on the ocean path over the reshaped
  reef, which the critic flagged. Make the move smooth from the river portal exit to the sea exit, with no
  rocking, roll oscillation or wobble. Report per-frame roll, pitch and yaw rate over the ocean section, M3e
  against M3f.
- **The white-out before the sea exit.** About 0.4 s, at 146.0–146.4 s, just before the crossing. It was
  already in M3d. Find the cause (exposure or auto-exposure, a bright volume or fog pass, the portal surface,
  or the camera entering geometry) and fix it at the root. Show the crossing frame by frame, M3e against M3f.

## Delivery

- **Once every M3f change is done, render the full video with sound** to
  `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3f/10-full/strange-attractor-m3f-full.mp4`, using the same
  command as the M3e full render. Do this after the hero assets the coordinator releases for integration are in.

## 3. Hero assets (the hero agent, scene/sa-hero; for later integration)

- The owner's alien verdict: either alien works. The hero agent picks the one that is easier to animate,
  with ties going to the Sketchfab alien tinted grey, and builds the talking and gesturing idle (H2).
- The seated Sasquatch must not clip through its stone. The hero agent is fixing this in the v1 Sasquatch.
- The campfire, UFO and Sasquatch v1 assets are drop-ins, described in the hero-agent hand-back
  (`hero-02-steps.md` on scene/sa-hero). Integrate them only when the coordinator says so.
- **Released for integration** (the coordinator, 2026-11-03; the hero agent's list, scene/sa-hero a86276fd).
  The assets are in the shared gitignored `assets/strange-attractor/hero/`.
  1. **Alien:** keep the Gray's node position, rotation 54.7 and scale 0.68. Set the asset to
     `hero/alien-grey-h2.glb` and the animation to `{"state": "talk", "speed": 1.0}`.
  2. **Sasquatches:** `hero/sasquatch/sasquatch-{a,b,c}-v1.glb` (c is the seated one, with the raised seat).
     **The seat stone:** the hero agent's stool-sized `seat-rock` ([75.081, 1.71, 1352.631], scale
     [0.27, 0.81, 0.85]) stops the clipping but reads as a small stone beside a squatting figure. Use instead
     a broader, flat-topped rock that the Sasquatch visibly sits on: top at about 0.46–0.48 m, its front face
     about 0.30 m behind the feet, wider than the hips. Verify there is no clipping over the whole 24 s loop
     with `examples/strange-attractor/hero/check_contacts.py` on scene/sa-hero. Lift the crouching B by 1.3 cm
     (fur dips into the ground at the toes).
  3. **Campfire:** `hero/campfire/campfire-v1.glb` with the pit's transform (rotation [0,20,0], scale 0.85) and
     `animation.state: "burn"`. Turn off the `campfire-flames` particles. Keep a reduced `campfire-core` glow so
     the "light in the air" survives. Raise the fire light above the flame tips. Keep the night look.
  4. **UFO:** `hero/ufo/ufo-v1.glb` is a drop-in replacement at the craft's new shore position. Carry the beat
     pulse onto its rim cells, navigation lights and underbody ring.
