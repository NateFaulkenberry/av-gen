# Art Pass 2, M3d: the owner's notes on M3c

The owner reviewed M3c (e1c11e47) on 2026-11-02. Nothing is merged; build M3d on top of e1c11e47. This is
still an **art-direction checkpoint**: per-scene clips and stills, no full sequence, suites deferred until
the owner approves the art (as for M3c).

Owner screenshots (read-only; never commit them) are in
`~/Desktop/av-gen-review/101-strange-attractor/pass2-m3d/00-owner-refs/`:
- `portal1-opening-rocks-to-remove.png`: the first portal out of the canyon (canyon to Moon). The boxed
  rocks are the two "burger" shaped rounded stones seen inside the opening. (The coordinator misread one of
  them as a hand in the M3c review; it is these rocks.)
- `sasquatch-area-black-tunnel.png`: a black tunnel-like shape in the Sasquatch hangout area.
- `sasquatch-area-floating-rocks.png`: the Sasquatch area at dusk; rounded rocks and shrubs float over the
  river and bank, and the bank geometry is broken.

## 1. Portals

- **Portal 1 (canyon to Moon):** remove the rounded rocks from the portal opening (see the reference).
- **Pyramid to night canyon (`04-pyramid-to-night-canyon-92-104s.mp4`):**
  - it is missing the triangle shard particles the other portals have; add them;
  - a single frame of particles is visible as the camera passes through: an animation glitch. Fix it, and
    check every crossing frame by frame for the same one-frame pop.
- **Launch bay to dawn canyon (`06-launch-bay-to-dawn-canyon-139-150s.mp4`): another art pass.**
  - No UFO may fly out of the bay while the portal is opening.
  - It is missing effects that were restored on the original canyon exit portal; bring it to the same look.
  - Pick an exit higher up in the canyon so the camera does not clip through rock, shrubs or any other
    geometry. No clipping anywhere on the path.
  - The half-second where the bay floor stands in front of the canyon glimpse (noted in M3c) must go too.

## 2. The canyon: the Sasquatch hangout area

- Fix the geometry:
  - remove the black tunnel shape;
  - fix the floating rocks (and the floating shrubs on them): every rock and shrub sits on the ground;
  - check the bank and river edge geometry in every shot of this area.

## 2b. The night camp: "looks just terrible"

The owner on the night camp (the Gray, the campfire and the Sasquatches by the river): **"this looks just
terrible - very CG - poor lighting, bad placement of stuff in the scene."** Their screenshot (not saved to
disk; described here) shows:
- rounded "burger" rocks floating in mid-air against the bank, and shrubs floating on the slope;
- the bank is one huge smooth featureless purple slope filling half the frame;
- the Gray stands stiffly in front of a rock, with flat, unshadowed lighting;
- the Sasquatches are a dark hairy mass, one perched on a floating rock slab;
- the campfire's log teepee and stone ring look like toy props; ember dots spray across the sky;
- nothing is grounded: no contact shadows, no fire light falling on the ground, characters or rocks.

This needs a real rebuild, done like a VFX lighting and set-dressing artist would, not more tweaks:
- **Research first** (`research-and-plan-first` rule): reference night campfire photography and film
  stills (firelight falloff, rim from moonlight, smoke, faces lit from below), and plan the set, the
  lighting and the framing before building. Show the plan and a still before the full pass.
- **Placement:** every rock, shrub and character on the ground, with contact and scale that make sense;
  compose the set deliberately (the group round the fire, the Gray facing them, the craft behind), with
  real detail in the bank instead of a bare slope.
- **Lighting:** the fire as the key with strong warm falloff on the ground and on the figures and
  shadows cast away from it; cool moonlight rim and fill; the craft as a light source; darkness that
  frames the group rather than flat purple everywhere.
- **Materials and props:** a believable fire (real flame and smoke, embers that rise and die near the
  fire), believable logs and stones, Sasquatch fur that reads with highlights from the fire.
- Show the before/after at the same camera beats, and stills per beat.

## 3. The Moon

- **The Earth's pixel corona (option c, the pick) must be animated**: the pixel blocks move, drift, flicker
  in deliberately (beat-pulsed, with the datamosh kept). No shimmer or aliasing; report the flicker metric.

## 4. Mars

- **The sand round the pyramids:** it should embrace the pyramids smoothly, not look like a massive clump of
  clay round their bases. Research images of the Great Pyramid covered in sand (historic photographs of the
  buried Sphinx and Giza, and artistic recreations of buried pyramids) and match an artistic recreation:
  sand sloping up the faces and filling the courses, wind-shaped, blending into the dunes, with fine ripple
  texture.
- **Better textures:** for the pyramid stone and for all of Mars if possible (sand, rocks, cliffs; the M3c
  notes: rocks near the rover read as dark lumps, the tracks are flat stripes, the cliffs have vertical
  streaks). Use a proper sand material with ripples. Free, clearly licensed textures are fine (log them in
  the asset record; never commit them; ask if a download needs a login).

## Rules

- **Branch:** same branch and worktree (`scene/strange-attractor`, `../av-gen-attractor`), on e1c11e47.
- **ADRs:** continue from 1902 within 1885-1929.
- **Media:** `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3d/`, numbered folders, exact file names.
- **One stop at the end of M3d** with: every crossing clip (with frame-by-frame evidence of no one-frame
  pops), the Sasquatch area before/after, the animated Earth corona clip and flicker metric, Mars clips and
  stills with the sand references you matched, side by side.
- All standing rules in `31-art-pass-2-coordinator-notes.md` apply.
