# 8. Final evaluation

The brief asks for the film to be judged as a viewer would, not as an engineer would (§29).

**How this was judged, and its limit.** I cannot watch video in real time, and I cannot hear it. I
judged the film from:
- dense frame sheets: every shot at its start, middle and end;
- the frames either side of every cut;
- the cast trace against the music grid;
- measurements of exposure, shimmer and whether each subject is actually seen.

Sync was checked by where events land against the grid, not by listening. The owner's eye and ear
are the final judge. What follows is my honest reading of the evidence.

**What was judged:**
- **The final render:** `build/gv3/final/glowmere-valley-3-1080p.mov` (1920×1080, 60 fps, 225.5 s,
  with the song; 0 GPU errors; sequence hash `746d3e45c516c7be`).
- **Its review:** `build/gv3/review-final/`.
- **The v0–v2 previews** before it.

**Reproducible (brief §25).** `python3 tools/make_glowmere_valley_3.py` regenerates the project,
the scene and the shot plan byte for byte identical to the ones the final was rendered from. The
render itself is deterministic, with a sequence hash in its log to compare against.

## The questions

**Music: does it feel synchronized to "Rebuild"?** Yes, by construction and by measurement.
- **Cuts:** every one falls on a bar or a beat of the fitted 130 BPM grid (7 ms against a blind beat
  tracker). They compress with the roll into the drop: two bars, one, half bars, beats.
- **The saucer's story lands on the music:**
  - the flyby in the two-bar pull-back (26.7–29.9 s);
  - over the rim as the shimmer is cut (148.45 s);
  - the beam on the riser, within two frames of bar 93 (24 ms);
  - the horse gone 10 ms before the crash;
  - the exit two bars into the drop.
- **The elder's heartbeat** fires on each of the 475 kicks the track plays, 20 ms ahead of it. It is
  absent in the pull-back and on the four gap beats, and at half strength in the muffled break.

**Energy: does visual energy follow the music?**
- **Yes, in the arc.** The pull-back and the break are the darkest stretches, the arrival steps up,
  and the drop is the brightest, most saturated and widest-coloured (+0.5 EV, the valley's light
  ×2.1, the aurora at 3.2), with the film's one flash and one shake. The tail settles.
- **Yes, in the cutting,** which follows the same curve.

**Pacing: boring, frantic, the right length?**
- **The riser** is fast on purpose: seven shots in 7.4 s, with the roll.
- **The plateau is the slowest stretch:** three 8-bar takes, 44 s. The directive asked for exactly
  that ("hypnotic, variety of angle, not of pace"), and each take moves: an orbit and two follows.
  **It is the film's most likely place to lose a viewer**, and the first thing I would change in
  another pass, to 4-bar takes.
- **Every wide holds at least two bars.**

**Cinematography: does the camera feel intentional?** Yes. Each segment has one camera language,
kept:
- low and slow in the cold open;
- lateral trucks for parallax in the grooves;
- a crane up the valley at the lift;
- locked-off long lenses in the suspension;
- push-ins in the break;
- up the beam in the riser;
- a fast lateral, a crane, and long wides in the drop.

The last wide rhymes with the first grand wide.

**Characters: do the aliens and animals belong to the world?**
- **All five aliens have their own shots,** and the occlusion check sees them in 88–100% of every
  shot they lead.
- **They walk on the ground.** A foot-drop on sharp turns was fixed in the engine (ADR-895).
- **The herd grazes in the wides.** The white horse is a character: set up grazing under the elder
  (s05) and taken in the riser.

**UFO: does the abduction land as a cinematic moment?** In v2, yes. v0 and v1 did not.
1. **Set up:** a shape crosses the sky in the pull-back.
2. **The approach:** heavy and slow through the suspension, the valley's light draining.
3. **The lift:** the horse rises glowing gold inside a beam dim enough to see it through (side on in
   s29, under the saucer in s31), and goes into it on the last beat of the roll.
4. **The drop:** the valley's light comes back.

The gold is the valley's light being taken.

**Mushrooms: are the heroes subjects, not props?**
- **The elder is the film's protagonist:** first image, last image, the heartbeat, the gold.
- **The lantern, the bloom and the umbra** each have a shot, and the rest hold the wides.
- **Five heroes are never more than part of a wide.** That is a real limit of this cut.

**Environment: a place, not a collection of assets?**
- **Yes, in the wides:** the river curving up the valley between its walls to the elder under the
  aurora (s14, s39), with the herd and the fireflies.
- **The geometry defects that broke that are fixed:**
  - four cliffs across the valley;
  - a wall of water;
  - floating stems.

**Immersion: do the wides feel like a valley or a miniature?**
- **A valley, since v2.**
- **The diorama cues are gone:**
  - no depth of field on wides;
  - eye-height or low cameras;
  - haze growing with distance;
  - slow angular motion;
  - known-size animals in frame;
  - foreground plants;
  - receding ridges.
- **The biggest single cue was the water.** A tiled river read as a model railway road; a calm river
  reads as a river.

**Effects: controlled and purposeful?** Yes, and there are few of them. Each has a story reason:
- the aurora follows the arc;
- the beam and the horse's light carry the abduction;
- the spores and fireflies carry the treble;
- one flash and one shake mark the crash.

The source's decorative effects were removed: a travel beam every 9 s, a ground pulse on every hero,
the aurora pulsing on every beat.

**Modulation: does the world respond to the music, or merely pulse?** It responds with a hierarchy.
One element answers each musical layer:
- the elder's gold to the kick;
- the caps to the two-bar bass glide;
- the spores and fireflies to the hats;
- the whole valley's light to the song's structure;
- the exposure to the one crash.

The kick gaps dim the valley for a beat. Everything else is still. Eighteen of the source's forty
routes made every hero and effect pulse on every beat; all of them are gone.

**Cohesion: one work?** Yes: one idea carried from the first frame to the last. The valley's light
is the protagonist: it wakes, is taken, and comes back rebuilt. The same place is seen lit at bar 41
and rebuilt at bar 113.

**Polish: what is still visible.**
- **The moon's reflection** in the pool is the brightest thing in s08 (kept: it is a moon on a pond).
- **A leaf** wipes across s20 for a moment.
- **Sage** is behind the grove's plants for 12% of s18.
- **The elder's gold** is a flat, clipped shape in close-up. This is Glowmere's look, the same in v0.
- **A pale body in the skybox** sits near the saucer in s25.
- **The paused first frame is black,** by design: the film cuts in on the first kick.

## The success criterion

> Glowmere Valley 3 is a cohesive, polished, entertaining animated music video that uses AV Gen's
> current Director, character animation, effects, modulation, cinematography, rendering, and world
> systems as an integrated production toolkit.

On the evidence I have, this is true of the final render. Two qualifications:
- **It has not been watched with the sound on by a person.** That is the review this document asks
  for next.
- **The plateau is the weakest stretch.** If the owner finds it slow, 4-bar takes are the first
  change.

It uses the engine's own systems throughout:
- the Director's camera collection, one locked shot per span;
- the staging system for the saucer's whole story;
- the character simulation, unscripted, cut to where the trace says the cast went;
- effects, modulation routes and sources;
- the world, its water and its heroes.

The generator only wrote data those systems read.
