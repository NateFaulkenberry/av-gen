# Talking Heads: the owner's notes on stop 1 (verbatim, 2026-10-25)

Relayed by the coordinator. The owner's words are quoted exactly; the coordinator's descriptions of the screenshots are
marked as such.

## 1. The neck

> "let's fix this wringle in the neck of the bust and shorten the neck some"

Screenshot (as the coordinator described it): the bust in profile with the head tilted back. A box marks the joint,
where the collar ring reads as a visible ridge or ring around the neck.

## 2. The beams

> "the beams of light from eyes / is misaligned - they look like they come from below the eyes and mouth"

## 3. Direction: a mechanical, gated head

> "the animations I'm looking for are a little more mechanical, and should feel a bit more like a noise gate for this
> scene. For example: feline footwear using stems as an example - let's say a talking head is on the bass channel (or
> stem for sake of development) - I would want the neck to snap back and mouth to open and beams of light to shoot out
> when there's a note, neck snaps back and mouth closes when there's no note being playing - it should feel more like a
> noise gate with distinct on/off. let's cut the eye beams and just keep it to beams of light from the mouth. also it's
> okay for the busts to rotate on the x-axis in the space, so it appears they are scanning the room - can we try for the
> next pass a bust with eyes the dart about?"

## How the coordinator and I read it (for stop 2)

- One assigned source per head (a stem key or a role; Feline's Bass stem during development, else the mix's bass)
  drives a **gate** with distinct ON and OFF and hysteresis:
  - ON: the neck snaps back, a hinged jaw opens, and the mouth beams shoot out;
  - OFF: the neck snaps forward, the jaw closes, and the beams cut;
  - transitions are mechanical, 30-80 ms with a slight settle.
- The eye beams are cut.
- "Scanning the room": a mechanical side-to-side yaw sweep (pans, pauses, snaps) within about ±35°. A pitch-sweep
  alternative was planned, then dropped (section 4).
- The smooth "procedural life" (float, noise wobble) goes. Wash, ghosts and tearing key off the gate.
- Next pass, after the gate pass is approved: eyeballs in the recesses that dart in saccades. The head is structured
  for it now.
- Also in this pass: no ridge at the neck joint, a 25-35% shorter neck, and the beam origins inside the mouth.

## 4. Follow-up (2026-10-25)

The owner confirms the scan is a side-to-side yaw about the vertical axis, so there is no pitch-sweep alternative.
For the darting-eyes pass:

> "eyes can look down though"

The eyes may glance down as well as sideways. The head itself does not pitch to scan; the neck tilt stays with the
gate.
