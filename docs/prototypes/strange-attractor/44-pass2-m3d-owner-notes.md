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

- **Owner addition:** the Moon portal (the crater floor to the dusk canyon) is the best one. It is the reference look
  for every other portal, including the pyramid and launch-bay passes. Show each portal side by side with it in the
  hand-back.
- **Portal 1 (canyon to Moon):** remove the rounded rocks from the portal opening (see the reference).
- **Pyramid to night canyon (`04-pyramid-to-night-canyon-92-104s.mp4`):**
  - it is missing the triangle shard particles the other portals have; add them;
  - a single frame of particles is visible as the camera passes through: an animation glitch. Fix it, and
    check every crossing frame by frame for the same one-frame pop.
- **Owner addition: the pyramid portal starts earlier** than the M3c values (glyph reach 1100 m, tear at 330 m): the
  glyphs and corruption are already visibly building as the fly-over leaves the rover and heads to the pyramids. Show
  the timing in the hand-back.
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
- **Owner addition: realistic close-up textures** (canyon camp ground and Mars sand, rock and pyramid stone):
  photoscanned PBR sets (albedo, normal, roughness, ideally height) from free, clearly licensed sources (CC0 such as
  Poly Haven or ambientCG, or the approved licence list); blended with the procedural strata at distance (close-ups
  get real detail, wide shots keep the look); tiling broken up (triplanar/stochastic blending, macro variation);
  each set logged in the asset record, kept in the gitignored assets folder, never committed; stop and send the link
  if a source needs a login. Show close-up before/after stills at the camp and on Mars.

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

- **Owner addition: the Mars camera and the rover** (this replaces the M3c circling move):
  - Come out of the Mars portal LOWER to the ground and closer to the rover.
  - One continuous fly-over: pass over the rover as it drives toward the pyramids, then carry straight on toward the
    pyramids. Do NOT orbit or spin round the rover (no behind / alongside / ahead sequence).
  - Move the rover closer to the pyramids. It is NOT the focal point of the scene; the pyramids are, and the rover is
    a beat on the way.
  - Improve the tyre tracks: real treaded ruts pressed into the sand, with raised edges and lighting that reads, not
    flat dark stripes. They should fade with distance behind the rover.

- **Owner addition: Mars terrain.** The canyon looks amazing and Mars looks crude by comparison. Rebuild Mars's
  geometry with the same procedural terrain pipeline and techniques the canyon uses (terrain generation, erosion and
  strata, the cliff and mesa treatment, rock scattering, the height cache), retuned for Mars:
  - wind-eroded mesas and buttes with layered sediment;
  - yardangs;
  - dune fields;
  - scattered basalt;
  - dusty haze.

  Don't hand-place crude primitives. Reuse rather than duplicate: if the canyon builder needs parameterising to serve
  both, do that, with an ADR if it is an engine-level change. This supersedes "another art pass over the landscape"
  above; the sand-embraces-the-pyramids and texture notes still apply on top of it. Show Mars before/after at the
  same camera beats.

- (The close-up texture addition in §2b applies to Mars's sand, rock and pyramid stone too.)

## 5. The ocean (owner additions)

- **Launch bay portal:** see §1 (no launches while it opens, the canyon exit's look, the exit high, no bay-floor
  occlusion).
- **The subs' path.** The UFOs fly OUT and UP as they leave the base, not straight up the moment they exit
  (reference: `00-owner-refs/ocean-ufo-launch-path-arrow.png`, a red arrow from the lit bay window, mostly forward and
  outward, rising at a shallow angle to the upper right).
  - Each launch leaves on that kind of shallow climbing line, still fast.
  - Returns mirror it on a descending shallow line.
  - Fast and purposeful.
- **The camera.** The underwater camera rocks at the beginning. Find the cause: an orientation or roll ease, a look-at
  target swapping, the glide's hand-off out of the river portal, or sway or bob noise.
  - Make the move smooth from the portal exit through the whole scene: no rocking, wobble or roll oscillation.
  - Report per-frame camera roll, pitch and yaw rate over the ocean section, before and after.
  - Show the clip.
- **Wildlife.** The M3c frames show essentially none. Add:
  - schools of fish that move as a flock (seek-safe, deterministic), scattering as a UFO whizzes past;
  - larger single fish cruising through;
  - life suited to the depth (rays, jellyfish with a soft glow, crabs or other bottom dwellers);
  - richer kelp, corals and sponges.

  Use free, clearly licensed models (CC0 or the approved list), logged and never committed; ask for the link if a
  source needs a login. Procedural is fine where it reads well. Everything is animated: fins and tails move, no
  sliding statues. Show stills and a clip.
- **The seafloor.** Remove the "basins" (`00-owner-refs/ocean-basins-to-remove.png`): the flat-topped, vertically
  striated box-like mesa walls on the seafloor and the bright flat strip between them.
  - Replace them with seafloor built with the canyon's procedural terrain approach: rocky reef outcrops; ledges and
    boulders with eroded, organic shapes; a sloping drop-off or trench into the distance; sediment drifts against the
    rocks.
  - No vertical streak banding, no extruded slabs, no hard flat plateaus.
  - It hosts the kelp, corals and wildlife.
  - Show before/after at the same camera beats.

## Rules

- **Branch:** same branch and worktree (`scene/strange-attractor`, `../av-gen-attractor`), on e1c11e47.
- **ADRs:** continue from 1902 within 1885-1929.
- **Media:** `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3d/`, numbered folders, exact file names.
- **One stop at the end of M3d** with: every crossing clip (with frame-by-frame evidence of no one-frame
  pops), the Sasquatch area before/after, the animated Earth corona clip and flicker metric, Mars clips and
  stills with the sand references you matched, side by side.
- All standing rules in `31-art-pass-2-coordinator-notes.md` apply.
