# Art Pass 2, M3b: working plan

Governing notes: `39-pass2-m3b-owner-notes.md`. One stop at the end. Media goes in `pass2-m3b/`, in numbered folders.

## Order

1. **Builders anchored to the terrain.** Each canyon placement becomes a generator-unit anchor times WORLD_SCALE.
   Character-scale offsets stay in real metres around their anchor. The world scale comes from the environment
   (`SA_WORLD_SCALE`), and a POC suffix writes `poc-*` scene files and `strange-attractor-poc.json`. The main canyon
   (0.4) must come out unchanged.
2. **Engine:**
   - `attractorWorld.pads`: the camp's footing eased and smoothed (it removes the terrace steps at the camp and
     opens the bench);
   - portal rim particles: a `portalRim` particle node that follows the live portal (its focus, axis and size), spawns
     with the field and the opening, and clears at the crossing; a ring emitter (the disc's inner radius) keeps the
     centre clear.
3. **Assets (Blender, original, not committed):**
   - Sasquatch individuals: build, height, fur colour and length, an old silverback; base poses (standing, crouching,
     sitting, leaning); IK-planted feet, baked; the scratch re-pathed in front of the body;
   - pyramids as coursed, chipped blocks, buried deep;
   - a desert-scrub shrub kit and a rock kit;
   - a cave mouth;
   - laser fans for the domes;
   - an Earth glow shell;
   - an ocean kelp, sponge and coral kit;
   - the rover from NASA (public domain), its tracks.
4. **Scenes:**
   - the Moon rave and the Earth (bigger, brighter, glow, RGB-split echoes on the beat);
   - Mars (the rover, the tracks, the blinking mast camera, buried block pyramids, more rocks);
   - the night (moon shadows, the fire's shadows, the Gray facing the group, the craft's own lights, low framing,
     scrub, rocks and the cave mouth);
   - the ocean (rocks, kelp, sponges, fish schools, two more subs);
   - the riverbed glide;
   - the bay portal home to the canyon at dawn, ending at 210.5 s.
5. **Renders:** both canyons at the same beats, side by side; the full sequence with sound; the Moon flicker metric;
   the opening before/after; both suites; the commit.
