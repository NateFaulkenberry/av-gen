# Art Pass 2, M3b: the owner's notes on M3

The owner reviewed M3 on 2026-11-02: "the strange attractor scene is heading in a good direction".
These are their notes, grouped by scene, with the coordinator's working rules at the end. M3
(e99e6b93) is not merged yet; build M3b on top of it.

Owner reference images (read-only; do not commit them) are in
`~/Desktop/av-gen-review/101-strange-attractor/pass2-m3b/00-owner-refs/`:
- `canyon-framing-circled-riverbed-sides.png`: the owner's earlier screenshot. The two circled
  areas are the sides of the riverbed they had in mind, and the arrow marks the direction of view.
- `moon-portal-opening-rim.png`: the canyon-to-Moon portal opening, for the rim particle note.

## Scope changes

- **Cut the UFO interior** from this pass (brief §6.3 onward). After the ocean, the journey
  **returns to the canyon**. Choose the time of day and the portal site that suit the ending. The
  launch bay's membrane can still be the portal, but it now leads home instead of into the UFO.
  The sequence must end cleanly, with no unauthored or blown-out frames.
- The owner will give more direction later. Do not start M4 or M5 work.

## 1. The canyon: a scale proof of concept

The owner feels the canyon still doesn't read well in perspective. They wanted the framing in the
circled screenshot, where those circled areas are the two sides of the riverbed, at a scale that
matches the characters. They expect this may need a new, smaller-scale landscape.

- **Keep the current canyon as it is.** Build a separate POC canyon at the owner's scale and
  placement, matching the circled framing. It must work with the portals and the journey: the
  wall portal, the dusk return, the night camp and the riverbed portal.
- Show it at the same camera beats as the current canyon: the opening, the camp at dusk and at
  night with the Sasquatches, and the portal approaches. Put it side by side with the current
  canyon.
- **If the POC does not look good or does not work** with the scene and portals, keep the current
  canyon. Then improve the Sasquatch area instead:
  - open the area up a little;
  - fill it with shrubs;
  - add a cave mouth (where the Sasquatches came out of).

  Do the fallback work regardless where it doesn't depend on the canyon choice: shrubs, ground
  detail at character scale, and a cave mouth would suit either canyon.
- The owner decides between the POC and the current canyon at this stop. Do not delete either.

## 2. Sasquatches

- **Three copies of the same Sasquatch look bad.** Find or make two alternative-looking
  Sasquatches, varying build, height, fur colour and length, face, and age. Downloads from the
  approved licence list are fine (log them in assets-pass2.md). If a model needs a login, stop and
  give the coordinator the link; the owner downloads it. A Blender variant of the existing model
  is acceptable only if it reads as clearly a different individual.
- **Fix the idles.** The owner says the upper-body motion looks good. Fix:
  - **Clipping:** the arm going behind the back passes through the body. Limit or re-path that
    gesture so no limb intersects the torso, and check every gesture from the shot cameras.
  - **Sliding:** the characters look as if they slide across the ground (visible in
    `pass2-m3/08-idles/0-24s-sasquatch-idles-dusk.mp4`). Plant the feet: the weight shift must
    come from the hips over fixed feet, not from moving the root.
  - **Poses:** they should not all share the same default pose. Give each a distinct base stance
    (sitting, crouching, standing, leaning, and so on).
- Show a new idle sheet and clip with feet-contact evidence, such as foot positions measured
  across the loop.

## 3. Portals (all of them)

- Add particle effects to the **interior rim** of each portal opening. They make the rim look
  more chaotic as the camera approaches, while the destination stays partly readable through it.
  The particles **clear as the camera passes through**. Drive them by camera distance so they are
  seek-safe, the same as the opening, and keep the centre clear.
- **The water portal into the sea:** the camera must pass through smoothly. Right now it feels
  like someone stepping through it. Remove any stop, step or ease that reads as a footstep; aim
  for one continuous glide.

## 4. The Moon (the owner: "looks great")

- **Domes:** beat-pulse their lighting, and make it look as if a rave is going on inside:
  multi-coloured strobes and laser beams spilling out of the dome openings and windows, and
  through any glass. Use the existing beat clock and response signals; it must still look right
  with audio off. Keep the strobes beat-locked and deliberate. The owner is sensitive to flicker,
  so there should be no aliasing or shimmer between beats. Report the flicker metric.
- **Earth:**
  - larger in the sky and brighter;
  - it must be the first thing you notice when coming out of the portal;
  - a **datamosh effect** on the Earth, with a **vibrant outer glow**, both beat-pulsed.

## 5. Mars

- **The Curiosity rover:** add it, heading toward the pyramids, with tire tracks behind it, as if
  it is on its way to photograph them. Animate it driving, slowly and seek-safe. Make its mast
  camera blink or flash as if taking photos. NASA's 3D Resources model (public domain) is the
  obvious source; log it.
- **The pyramids:**
  - must read as chipped, coursed stone blocks like the Great Pyramid, with stepped, weathered
    and broken edges, not crackle or cobblestone;
  - must sit much more buried in sand.
- **Refine the landscape:** more rocks, more detail, better-looking features.

## 6. The night scene: needs a real art pass

The owner says it feels blocked out:
- the lighting is not good;
- there are no shadows;
- the detail is poor;
- the canyon textures don't hold up at the Sasquatches' scale (tied to section 1).

Fix:
- **Lighting and shadows:**
  - moonlight with real cast shadows from the characters, rocks and craft;
  - the fire as a shadow-casting key on the group.
- **Surface detail at character scale:** ground, rocks and shrubs that hold up next to a 2.5 m
  figure.
- **The Gray:**
  - re-place it so it clearly faces the Sasquatches and reads as engaging with them;
  - keep it fully in frame on its beats.
- **The craft:** illuminate it with its own lighting (rim lights, underside glow, a soft spill on
  the ground beneath it), so it is a light source in the scene, not a dark shape.
- Also fix the earlier coordinator notes:
  - the stair-step terraced banding on the bank;
  - the high angle with tiny figures; use a lower, closer framing;
  - the empty-bank frame.

## 7. The ocean

- The owner **likes the underwater base and its glowing lights**, so keep that look. They are open
  to much more detail. Add more underwater features:
  - rock formations;
  - vegetation (kelp, sea grass, corals or sponges suited to the depth);
  - fish, schools and singles;
  - marine snow.
- **Keep the UFO craft as the submersibles** (the owner likes that). Add a couple more, going in
  and out of the base.

## Working rules for M3b

- Same branch and worktree (`scene/strange-attractor`, `../av-gen-attractor`), on top of e99e6b93.
  The ADR range continues within 1885–1929.
- Media goes in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3b/`, numbered folders per
  section. Give exact file names in the hand-back.
- One stop at the end of M3b, with:
  - the full sequence render with sound (opening to the return to the canyon);
  - stills and short clips per section;
  - the canyon POC side by side with the current canyon;
  - the Moon flicker metric;
  - the opening before/after;
  - both suites at exit 0.
- All the standing rules in `31-art-pass-2-coordinator-notes.md` still apply: no third-party
  assets committed, gpu-lock, no baseline writes, no gore, no visible world edges, offline quality
  first.
