# Art Pass 2, M3c: the owner's notes on M3b

The owner reviewed M3b (4e675db8) on 2026-11-02. Nothing is merged; build M3c on top of 4e675db8.
The owner did not pick a canyon, so the **current canyon stays the default**. Leave the POC builder
as it is and do no further POC work unless the owner asks.

## 1. Portals: revert to the Gate A look

- **Revert the portal changes made for the owner's M3b note** (the rim particle ring, ADR-1898 as
  used on the portals). The owner says "whatever changed doesn't look good". Remove the rim
  particles from the portals. Remove ADR-1898's engine feature too if nothing else uses it (no
  compat shims, ADR-441); otherwise mark it unused in the ADR.
- **Recreate the opening in
  `~/Desktop/av-gen-review/101-strange-attractor/portals-a/01-gate-a-0-32s-1080p30-with-sound.mp4`.**
  The owner calls this "the best they've looked". It is the Portals Gate A build (commits
  21da024a and 65983cf2; notes in `16-portals-gate-a.md`). In it:
  - dense world-anchored glyphs carpet the rock and the floor;
  - a wire ring gathers on the wall (about 24 s);
  - the wall bursts open into a storm of gold shards and blocks around a dark centre (about
    30 s).

  Diff that build against today's portal code and bring back what made it look like that,
  for every portal. Keep what the owner has asked for since: the destination glimpse through the
  opening, and portals set in real terrain.
- **Fix the regressions:**
  - **Moon exit (the crater-floor portal to the dusk canyon) is broken.** It no longer shows a
    glimpse of the canyon through it, and the camera no longer pops out of a canyon wall. Find
    what changed since M2/M3 (the journey legs, ADR-1899's hand-back guard, the rim particles, the
    exit framing) and restore both behaviours.
  - **The ocean entrance (the river portal into the sea) is broken.** Restore its opening
    animation. Keep the M3b straight glide through it, which was the owner's earlier ask, unless
    it caused the break.
  - Check every portal crossing in the full sequence for the same kind of regression, and show
    each one.
- **The Mars portal (in the canyon wall) must start its animation earlier.** The camera stares
  at a plain canyon wall too long before anything happens. Start the glyphs and the corruption
  sooner on the approach.

## 2. The Moon

- **Remove the laser beams shooting into the sky.** The owner meant something else: light up
  the dome WINDOWS as if a rave is going on inside, with multi-coloured light moving and strobing
  behind the glass, beat-pulsed, maybe a little spill on the ground below. No beams into the sky.
- **Earth's glow:** make it much more stylised and cool. The current soft blue halo is bland.
  Explore and pick a look (for example layered or chromatic rims, a datamoshed or glitching
  halo, scanline or blocky glow, colour-shifted atmosphere bands), keeping the beat pulse and the
  datamosh. Show 2-3 options as stills in the hand-back, with your pick in the sequence.
- Keep the Earth large and the first thing seen. Don't crop it at the top of frame (34 s).

## 3. Mars: another full art pass

- **The rover is the subject of the arrival.** Come out of the portal close to Curiosity and
  approach it as it makes its way toward the pyramids. It should be a significant part of the
  scene, not a few pixels. Keep the tracks and the camera flash.
- **The sand burying the pyramids needs to look realistic:**
  - wind-shaped drifts banked against the faces;
  - sand spilling into the courses;
  - soft ripple texture;
  - no vertical streak banding on the drift face;
  - no hard cliff of sand.
- **The pyramids:** keep the coursed, chipped blocks. Fix the smooth white capstone, and add
  weathering and colour variation so they are less clean.
- **Landscape:** another art pass over the whole Mars scene: terrain, rocks, haze and light.
- **The pyramid portal must start its animation sooner.**

## 4. The ocean

- **UFO subs: much faster.** They should whiz in and out of the base and appear to head up
  toward the ocean surface: fast, purposeful launches and returns, not slow swimming around.
- Fix the entrance portal (see section 1).

## Rules

- **Branch:** same branch and worktree (`scene/strange-attractor`, `../av-gen-attractor`), on
  4e675db8.
- **ADRs:** the next free in 1885-1929 is 1901.
- **Media:** `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3c/`, in numbered folders.
  Give exact file names.
- **One stop at the end of M3c, with:**
  - the full sequence with sound;
  - a portal folder with Gate A side by side with the new opening, plus every crossing;
  - the Moon dome windows and the Earth glow options;
  - Mars;
  - the ocean subs;
  - the opening before/after;
  - the flicker metric for the Moon;
  - both suites at exit 0.
- All standing rules in `31-art-pass-2-coordinator-notes.md` apply.
