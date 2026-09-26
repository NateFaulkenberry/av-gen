# 5. Quality: how it is judged, and what was found

A render that plays is an implementation milestone, not a result ([00-brief.md](00-brief.md) §30).
This is the process that decides whether a shot, a segment or the film is good enough. It is also
the log of everything it found.

## 5.1 The evaluation, in five layers

Cheapest first, so the expensive layers only see what the cheap ones passed.

1. **The cut's structure** (the generator, before any render). It refuses a cut with a hole, an
   overlap or a missing node. It also reports:
   - shot lengths per segment against the directive, e.g. the riser must compress;
   - any shot shorter than a beat;
   - any wide shorter than two bars, because a large space seen briefly reads as small.
2. **The staging against the music** (`avgen_cast_trace`, no GPU). Every beat of the saucer's
   scenario is measured, not assumed:
   - the flyby inside the pull-back;
   - the approach from the suspension;
   - the beam on the riser, within two frames;
   - the dissolve no later than the crash;
   - the exit on bar 99.
3. **Framing from the trace** (no GPU). For every shot of a character or the saucer, the rig and
   the traced positions give where the subject is in frame and how tall it is, sampled across the
   shot. This does the job of the Director's framing critique (ADR-769) for every frame, not one
   still per shot. With the render (`--video`), it also asks every half second whether the subject's
   body is actually visible where it projects, which catches a character behind a plant
   ([07-technical.md](07-technical.md) §7.9).
4. **The image, measured** (`tools/gv3/review.py`). Frames at each shot's start, middle and end,
   with exposure (mean, p99), contrast (RMS), colour (mean saturation) and motion between them.
   Blunt flags:
   - near-black;
   - flat;
   - clipping;
   - a static image.
   These are measurements, never scores: they point the eye at a shot, they do not judge it.
5. **The eye.** Every contact sheet is read against:
   - the rubric of `docs/visual-quality.md` §2: composition, hierarchy, lighting, depth,
     atmosphere, scale, motion, audio relationship, cinematography;
   - the diorama checklist (research) for every wide.

   Each shot gets 1–5 on subject, composition, depth and scale, motion, music and polish. Anything
   at 2 or below is a defect to fix, not a taste. Moving material is judged on the video, not the
   sheet: cuts, pacing, sync.

## 5.2 The diorama checklist

Every wide shot is checked against these, in order (research-findings §"Diorama checklist"):

1. No blur gradient.
2. The camera at eye height or low, never a 30–50° look-down.
3. Haze growing with distance.
4. Slow angular motion.
5. Chroma falling with distance.
6. A known-size object in frame: a horse, a cow, the saucer.
7. Something close to the lens.
8. A horizon or receding ridges.
9. Detail at several scales.
10. Distant light with parallel shadows.
11. Layered occlusion.
12. Glow that spills.
13. Water at the right scale.

## 5.3 Findings

Severity: **A** breaks the film, **B** a visible defect in a shot, **C** polish.

| # | Iteration | Where | Finding | Severity | Fix | Validation | State |
|---|---|---|---|---|---|---|---|
| F1 | scout | the valley | Four straight cliffs across the valley, up to 2.35 m, at the medial axes of the corridor's bends | A | Continuous path level (07-technical §7.1) | seam scan 1,455 cells → 0; five tests with controls | fixed |
| F2 | scout | river at z ≈ 82 | A 2.63 m wall of water across the channel where the elder-pool's reach ends | A | Waters blend by weight (§7.2) | seam scan 0; two tests | fixed |
| F3 | scout, v0 | the water | The brightest, most saturated thing in most frames. In v0 it was still a bright cyan sheet under glowing blotches (s02, s10, s18, s19, s21, s26, s33) | B | v0: glow 1.2 → 0.35, ripple scale 0.42 → 0.85, reflection 2.6 → 1.6 (not enough). v1: glow 0.12, glow coverage 0.22 → 0.10, reflection 1.0, fresnel 0.22 → 0.14, roughness 0.17 → 0.30, ripple 0.55, a darker shallow colour; no shot is composed on open water | v1 review | fixed in v1 (dark, quiet water); its ripples were still wrong: F25 |
| F4 | scout | the sky | The camera travel beam: a violet streak across the sky every 9 s, unrelated to anything | B | Removed | v0 review | fixed in data |
| F5 | scout, v0 | wides | Read as a model: a 25° look-down, uniform vegetation, no haze gradient. v0's crane (s12) ended in exactly that view | B | No wides from the walls; long lenses from far and low. v1's crane looks up the valley instead of down onto it | v1 review | partly: the crane now looks up the valley; the river still read as a model railway road (F25) |
| F6 | scout | every shot | Depth of field on every shot, focused at 8 m, blurring distant valley | B | Depth of field off | v0 review | fixed in data |
| F7 | scout | scree, cairn | Stems floating 12.6 cm and 4.1 cm on their downhill side | B | Footprint seating (§7.6) | generator report | fixed |
| F8 | scout | the saucer | Bobbing on every kick and spinning on every beat: a toy, not a craft | B | Reactions removed; slow constant spin | v0 review | fixed in data |
| F9 | trace | the drop | The horse vanished 0.24 s after the crash | B | Lift shortened to 4.91 s | re-trace | fixed |
| F10 | tests | aliens | A walking alien's body dropped 0.31 m in a frame on a sharp turn | B | The bob eases (§7.3) | ADR-830 benchmark passes on every route tried | fixed |
| F11 | v0 | nearly every wide and medium | **The hillsides went pale and flat,** a lilac wall behind every subject, reading as daytime fog. My own base look caused it: the distance haze at 0.16 (6x the source) with the source's pale tint, and contrast lowered to 1.25 | A | Haze 0.035 at 450 m with a dark night tint; contrast 1.4; exposure −1.25 | v1 review | fixed (v1: dark hillsides, no lilac wall) |
| F12 | v0 | first and last frames | The black before the first kick and after the last hit showed the skyline: exposure is not applied to the sky or the bloom | B | The grade's gain keyed to zero outside 0.48–224.788 s | v1 review | fixed (v1: black before the first kick and after the last hit) |
| F13 | v0 | s32 → s33 | A live aim at a body after it is retired swings the camera: at 177.68 s s32's camera looked at the ferns | B | No rig aims at the horse after 177.70 s; s31 and s32 use fixed targets at the saucer | cut sheet | fixed in data |
| F14 | v0 | s05 | The camera was 1.2 m down in the ferns: no horse, no elder, just leaves | B | 3 m up, with the elder behind the horse on one line of sight | v1 review | partly: the horse is seen; the elder's cap was cut by the top of the frame (F32) |
| F15 | v0 | s14 | "The first grand wide" was blocked by trees; the elder was a speck behind a trunk | A | The scout's view from the south over the river, 60 mm, 145 m | v1 review | fixed (v1: s14 reads as a place, the water aside) |
| F16 | v0 | s23, s24, s34 | Near-black frames: a small saucer in an empty sky | B | s23 from north of the elder with its cap in the foreground; s24 from under the cap with the saucer over its rim; s34 from far, the saucer rising over the elder | v1 review | partly: s23 is one of the film's best frames, s24 holds; s34 was still empty sky (F30) |
| F17 | v0 | s25 | The follow camera aimed at the saucer and lost Ember below the frame | B | A fixed eye behind Ember with the saucer's station as the target | v1 review | not fixed: Ember was 35° outside the lens (F29) |
| F18 | v0 | the riser, s27–s32 | Three near-identical saucer-underside shots, and the horse was never seen: a speck in a clipped white column | A | s29 side on at 85 mm (30% of the frame); s30 the elder beside the beam; s31 the horse under the saucer; s32 the horse fading into the saucer | v1 review | partly: s27 and s30 work; the horse was still lost in the beam (F24, F26) and s31 repeated s27 (F31) |
| F19 | v0 | s33, the drop | Did not read as the climax: no brighter or wider than the plateau, and a fern leaf across the frame | A | Arc at the drop to +0.5 EV and light 2.1; the flash to +2 EV and bloom +2.5; a fast lateral move | v1 review | partly: brighter and wider; the flash lasted a second and read as overexposure (F33) |
| F20 | v0 | s39 | The last wide had no subject: a pale hillside over ferns | B | Low, south of the elder, looking north with the aurora behind it | v1 review | partly: a subject and a place, but half the frame was river (F32) |
| F21 | v0 | the cast | Tide and Sage never appeared; five heroes never appeared | C | s10 Tide (an orbit), s18 Sage in the grove | v1 review | fixed (Tide s10, Sage s18); more heroes in v2 (F31) |
| F22 | v0 | s02, s10, s18, s19 | Shots composed on open water | B | Replaced (s02, s10, s18); s19's camera moved to the land side | v1 review | mostly fixed; s08 is still a pool shot, now calm (F25) |
| F23 | v0 | the heartbeat | **The heartbeat did nothing.** The gills' gold measured the same on the kick and between kicks (G 0.691, frame mean 0.166 at every beat phase), and the load reported 0 warnings. Its route drove `nodes/elder-2-gills/emissiveBoost`, which is dead twice over: `Composition` applies a node's `emissiveBoost` to imported meshes only, never to a procedural node; and the gills' emission is written by a material program, which owns it (ADR-179), so no material value reaches it anyway | B | A scored `kick` source: one pulse per kick the track plays (475; none in the pull-back or on a gap beat, half in the muffled break), attack 0, decay 110 ms. First onto a Glow on the gills (an FXL multiplier, which does act after the program): it worked, and moved the gold 1.4%, because the gills are the thin lamellae and the gold that fills the frame is the underside. So it multiplies the warm tissue program's emission instead (×1.6 at the crest), which is the elder's alone: gills and underside. The engine defect is recorded, not fixed (07-technical §7.7) | test render of s16: frame mean +10% on the kick, the pulse lands on the frame of the kick and is gone within the beat | fixed |
| F24 | v1 | the riser | **The horse never glowed.** The source scenario ramps the horse's `emissiveBoost` to 6.5 through the lift, and a boost multiplies a material's emission; the horse's glTF emits nothing (no `emissiveFactor`), so the ramp multiplied zero. The same silent family as F23 | B | The dead cue and its four parameters removed. A Glow on the horse (self-glow 3, rim 5, gold: the valley's light being taken), keyed on the timeline to the measured lift: up over its first 1.3 s, held while the horse dissolves, gone on 177.70 s | stills at 175.4 and 176.95 s: the horse reads cream-gold inside the beam and under the saucer | fixed (v2: the horse rises cream-gold inside the beam, side on in s29 and under the saucer in s31-s32) |
| F25 | v1 | the river, in about a dozen shots | **The river read as a cobbled road** (s12, s14, s21, s26, s33, s37, s39, and more): ripples at 0.55 on 1.2 m cells swung each cell's reflection between the aurora's bright horizon and the black zenith, a model-railway texture on every wide. On calm water, the moon's glint then became one bright patch (s08) | A | Ripple 0.55 → 0.1 and 0.85 → 2.6 cycles/m, so the fine layers fade with distance in the shader and far water is a mirror (for the final, 0.05 at 5.2: F37); roughness 0.3 → 0.2; sparkle and foam 0.06; the bass route 0.18 → 0.03; the moon's glint (specular) 1.2 → 0.25. Chosen from three variants on four stills | v2: the river is a smooth dark ribbon in every wide; shimmer on the locked-off s26 −44% (§5.4) | fixed |
| F26 | v1 | s29, the lift | The beam clipped 22% of the frame and swallowed the horse | B | Beam emission 1.55 → 0.7, spawn rate 6,100 → 4,200 (in the scenario; see F35) | a continuous riser render: clipping 21.9% → 10.5% at 175.4 s, the horse readable | fixed (v2 s29) |
| F27 | v1 | s07, the flyby | The saucer passed over the camera and out of the top of the frame; the pull-back never showed the shape | B | From 112 m north of the elder looking south past it; the path lower and shorter (at least 36 m over the walls), so it crosses the frame left to right above the cap for about 1.4 s. Then Tide, standing 10 m in front of the lens, put its head in the bottom of the frame: the camera moved 12 m west | stills at 27.9 and 28.6 s | fixed (v2: the saucer crosses the top of the frame at 28.2 s, over the dim elder) |
| F28 | v1 | s17, s18, s20 | **Occlusion in moving shots**: a grazing horse's rump filled a quarter of s17; a tree fern hid Sage in s18; s20's camera went through a leaf. Three frames a shot caught one of them by luck | B | s17 orbit 4 → 7.5 m; s20 0.9 → 1.7 m and closer; s18 re-staged (still hidden at 3.2 m). A check that sees it: `framing.py --video` samples every half second and asks whether the subject's body is where it projects (07-technical §7.9) | the check on v1 agrees with the sheets; v2 review | mostly fixed: v2 s17 clear, s20 seen 100% (a leaf wipes past), s18 seen 88% (up from 84%) |
| F29 | v1 | s25 | Ember was 35° outside the lens: the fixed eye was placed on the trace but Ember walks the bank. Then, riding at 2 m, the camera looked up past Ember's body and the head was a grey dome on the bottom edge | B | A follow 7 m behind and 3.6 m up (over the head), looking at the saucer's station | v2 review | fixed (v2: Ember seen in 100% of samples, lower left, the saucer above) |
| F30 | v1 | s34 | The saucer left straight up at 134 m/s, out of the frame in a second; the rest of the shot was empty sky | B | It leaves up and away over the north rim in 8 s; seen from the south bank it lifts beside the elder's cap and shrinks toward the aurora for the whole shot | stills at 182.0, 183.5 and 184.9 s | fixed (v2 s34) |
| F31 | v1 | s15/s35, s27/s31 | Repeats: s35 was s15's bloom from s15's angle; s31 was a third underside of the saucer | C | s35 is the umbra, a hero the film had not shown; s31 is from the south, the glowing horse under the saucer against black sky | still at 176.95 s | fixed (v2) |
| F32 | v1 | s05, s11, s39 | s05 cut the elder's cap at the top; s11 was a portrait on black sky with nothing to watch; the last wide was half river | C | s05 30 mm aimed above the horse; s11 from Vane's north-east with the elder's gold beyond; s39 re-sited (v2 stills) | stills | fixed (v2: s05 the whole cap over the horse; s11 Vane watching the elder's gold; s39 the rhyme with s14) |
| F33 | v1 | the drop | The crash's flash held for a second and read as an overexposed shot, not a flash | C | Held 60 ms, gone in a third of a second; bloom likewise. And one camera shake on the crash: 6 cm, 0.35°, 0.9 s, the film's only shake | v2 review | fixed (v2) |
| F34 | v1 | s25, s31 | A pale flat-shaded body in the eastern sky (about 21° up; it survives both moon controls, so it is in the skybox) reads as debris beside the saucer | C | Not the production's to change: it is the owner's sky. s31 now looks north | stills | noted |
| F35 | tooling | staging | A project parameter does not override a staging scenario's value: two beam variants set through `staging/abduction/*` rendered identically. The scenario reads its own numbers | C | Staging changes are made in `cast.py` and regenerated; the variant tool only varies project parameters | measured | noted |
| F36 | tooling | stills | A render that starts with a seek is not the film's frame: particles start empty (the beam, the spores), and the cast can stand differently (s25 and s28 stills put Ember and Vane elsewhere than the continuous render did; the known seek-is-not-play gap). Stills are trusted only for looks that do not depend on history: the water, a hero, a fixed camera on scenery | C | The variant tool renders a clip when asked (`DURATION`); cast shots are judged on the full render | measured | noted |
| F37 | 1080p check | the river, final render only | **The water fix did not survive the resolution change.** The shader fades each ripple layer when its wavelength falls under about three pixels, and pixels are the internal resolution: the previews render 960×540 at 2× supersampling, the final 1920×1080 at 2×. At the final's resolution the ripples resolved across the whole far river again (s14, s39 stills) | B | Frequency doubled for the final, amplitude halved (0.1 at 2.6 → 0.05 at 5.2 cycles/m; the bass route likewise): the slope is kept and the ripples dissolve into a mirror at the preview's distance | 1080p stills of s12, s14, s39 against the v2 frames | fixed |

## 5.4 The water evaluation (brief §13)

The water was judged from 13 shots that hold it:
- **near:** the foreground of s21 and s28;
- **middle distance:** s02, s08, s25 and s26;
- **far:** s06, s12, s14, s33, s37 and s39;
- **height:** from a metre above the surface to 30 m up the s37 crane;
- **direction:** both toward the moon and away from it.

| The brief asks about | What was found | State |
|---|---|---|
| strange reflections | v1: every ripple cell reflected either the aurora's bright horizon or the black zenith, so the river was tiled (F25). v2: a calm surface carrying the sky. The moon's own reflection in the pool is the brightest thing in s08, and is kept: it is what a pond under a moon does | fixed; one kept by choice |
| transparency, incorrect depth | Nothing seen: the banks fade over the authored depth and the shallows show the bed without a hard edge | none found |
| visual discontinuities | A 2.63 m wall of water across the river where the pool's reach ended (F2) | fixed in the engine (ADR-894); seam scan 0 |
| shimmering, aliasing | Measured on the locked-off s26 (`tools/gv3/shimmer.py`): the water's change per frame 0.0052 → 0.0029, p99 0.045 → 0.027. Finer ripples fade with distance in the shader, so the far water holds still; near the lens the ripples resolve and read as water. That fade is in pixels, so the final needed its own ripple scale (F37) | fixed |
| lighting inconsistencies | v0: the water was the brightest, most saturated thing in most frames, a bathtub under glowing patches (F3). The moon's glint on calm water floodlit the foreground of every shot facing the moon | fixed (glow 1.2 → 0.12, coverage 0.22 → 0.10, glint 1.2 → 0.25) |
| unnatural movement | The river pumped on the bass (a route from the source) | the route cut to a trace (0.18 → 0.03) |
| bad interaction with terrain | The river corridor's cliffs at the bends (F1), and the pool's wall (F2); foam lines on the banks | fixed in the engine (ADR-893, 894); foam 0.25 → 0.06 |
| distracting artifacts, views where it looks fake | Every v1 wide: a model railway's road winding up the valley (F25) | fixed: in v2 the river is a mirror in s12, s14 and s39 |

The brief's instruction was to leave the water alone if it looked good, and it did not. Every
correction except the two engine fixes uses the water's existing settings. Those two are small
reusable improvements to how the world is built (ADR-893 and ADR-894), not to the water shader.

