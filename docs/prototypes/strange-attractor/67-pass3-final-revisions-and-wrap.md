# Pass 3: the owner's final revisions, and the wrap (2026-11-08)

The owner reviewed the full review render (`pass3-env/14-camera-paths-full/`, 1f88520c). These are the last
revisions for now. After them, Strange Attractor is a wrap: a final 1080p render, then the project goes into the app's
offline examples as **Strange Attractor (demo)**, and the branch merges to main.

The owner, verbatim:

> canyon intro - colors appear a little more washed out now in the intro - maybe we can restore some of the contrast.
> other than the underwater scene maybe we can improve color contrast on all other scenes, make them a bit more vivid
>
> add a post processing effect to add some suble bloom, use some post processing effects artistically in each scene
> if desired to add to the overall ambience of the scene
>
> canyon night scene - fix the camera so as it comes out of the portal it doesnt look to the right - keep the
> characters more ceneterd in the shot - too much time is spent staring at a canyon wall in this scene - move the
> entrance portal further down the river so we have more time to see characters as we pass by them, and then move
> the exit portal down the canyon wall, to the wall behind where the characters are - use this wall so we don't have
> to spend a bunch of time moving the camera up a canyon wall
>
> mars - try to find a more natural way to make the piles of sand around the pyramids fit with the textures of the
> mars surface - right now it looks like two entirely different textures / surfaces
>
> strange attractor night scene - entrance portal clips through the canyon geometry as we pass through - fix this
> and start the exit portal animation sooner - place the portal more centered in the river and use a diving motion
> similar to the moon camera animation to go up and into the portal
>
> underwater scene: remove the octopus
>
> give me a final render you can keep it 1080p

**How the leg names map** (the coordinator's reading):
- **"Canyon night scene"** is the **dusk leg**, about 47.9–69.6 s: the Moon arrival, the camp, the glyph-cliff portal
  to Mars.
- **"Strange attractor night scene"** is the **night leg**, about 93.5–121.2 s: the Mars arrival, the river, the river
  portal to the ocean.

## 1. Colour: contrast back, more vivid (every world except the ocean)

- **Canyon intro:** it reads more washed out now than before. Find when it lost contrast (compare 09-current-full and
  14-camera-paths-full with the M3f and M3g full renders at 0–30 s) and restore it.
- **Every world except the ocean** (canyon day, Moon, dusk, Mars, night, dawn): more colour contrast and a bit more
  vivid. Use per-world grading inside AgX (saturation, contrast, the look). AgX stays for the whole film.
- **Leave the ocean as it is** (E is closed).
- Check every crossing with the luma guard so no new contrast step appears at a portal (54- §2).

## 2. Post: subtle bloom, plus per-world ambience

- Add a **subtle bloom** across the film.
- **Use post effects artistically per world where they help the ambience**, at the agent's judgement. For example:
  - a gentle glow on the campfire and the glyphs at dusk and night;
  - a soft haze or light veil on Mars;
  - a crisp, cold Moon;
  - vignette, grain, halation.
- Keep it subtle. Blend any per-world settings over the crossings so there is no visible step.
- Before/after stills per world.

## 3. Dusk (the owner's "canyon night scene")

- **The arrival out of the Moon portal must not look to the right.** Keep the look on the line, with the characters
  near the centre of frame as they are passed.
- **The entrance portal moves further down the river,** so there is more time to see the characters as the camera
  passes them.
- **The exit portal (to Mars) moves down to the canyon wall behind the characters,** so the camera does not spend
  time climbing a canyon wall.
  - Less time staring at a canyon wall overall.
  - The portal keeps its glyph opening and its animation.
- The leg's start and end times stay, and so does the Mars arrival on the far side.
- This overrides "portals stay" for the dusk leg, at the owner's instruction.

## 4. Mars: the sand piles fit the ground

- The sand piles around the pyramids read as a different surface from the Mars ground. Make them one surface: the
  same material family, colour, ripple and grain scale, and roughness, blended by height or slope with noise-broken
  edges.
- This is the M3g §3 note, still not solved. Before/after at the owner's M3g screenshot camera and at 2–3 path beats.
- **The hero agent does this** (it owns the Mars world files) on a fresh branch from 1f88520c. The SA agent merges it.

## 5. Night (the owner's "strange attractor night scene")

- **The entrance portal clips through the canyon geometry** as the camera passes through it. This is the black
  rock with white fringes at about 93.5–105 s. Fix it: clear the portal's volume of rock, or move the opening off
  the rock.
- **The exit (river) portal:**
  - start its opening animation sooner;
  - put it more centred in the river;
  - use a diving motion like the Moon's camera into its portal: rise up, then dive into the portal.
- The leg's start and end times stay. The camp stays on the left, passed with no camera turn.
- The ocean arrival matches with no visible step.

## 6. Ocean: remove the octopus

- Remove the octopi (octopus-v1) from the ocean: the nodes, tracks and assets references.
- Nothing else in the ocean changes.

## 7. The coordinator's additions (outstanding items folded into this round)

- **The cream patch on the rock top** in the left foreground at 0 s (in 09- and 14-). Fix it.
- **Night, the far bank's unlit rock:** it is black with white fringes on the right at about 95–105 s. It is probably
  the portal clipping in §5; confirm it is gone.
- **Docs:** 60- §6 (the Moon exception) and 57- (H-4, and the parked A-1..A-4) updated to the final state.
  - Record in 57- that the hero agent's Phases C and D beyond §4 are not done, and are parked with the wrap.
  - Record that Phase B (scene/sa-hero a61ebfbf) is rejected and not merged.

## 8. The final render, then the wrap

1. **Final render**, 1080p, 30 fps, supersample 2, with sound, to
   `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/10-full/strange-attractor-final.mp4`, plus a contact sheet
   every 5 s.
   - Look at the contact sheet before handing back, and check the crossings with ±0.5 s stills.
   - **Stop and hand back to the coordinator here**, before the suites.
2. **Then, on the coordinator's word:**
   - **Add Strange Attractor to the app's offline examples** as **"Strange Attractor (demo)"**. It is not a live
     scene.
     - Follow how the other offline examples are listed, and the tests that enforce the index.
     - The scene's third-party assets are gitignored and never committed. Make a clean checkout without them behave
       the way the other asset-dependent examples do, and don't fail the suites.
     - This replaces the "scenes stay out of examples/index.json" rule for this scene only.
   - **Rebase onto origin/main** if main has moved.
   - **Run both suites to exit 0:**
     - `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/ci/run-suite.sh gpu`
     - `tools/gpu-lock.sh tools/ci/run-suite.sh cpu-assets`
   - Hand back for the merge. The coordinator merges to main (`--ff-only`) and pushes.
   - The merge note mentions the `AVGEN_GPU_LOCK_POLL` addition to `tools/gpu-lock.sh`.

No gore. Never commit third-party assets. All GPU work goes through `tools/gpu-lock.sh`. Use no compat shims; cut the
octopus outright (ADR-441).
