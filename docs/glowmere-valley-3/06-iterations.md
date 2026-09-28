# 6. Iteration history

Each iteration is a full render of the film at 960×540 and 60 fps, reviewed shot by shot
([05-quality.md](05-quality.md) §5.1). An iteration exists because something needed changing, not
because the brief's example list had a slot for it. The renders are in `build/gv3/vN.mov`; their
review sheets and reports are in `build/gv3/review-vN/`.

## Scout (before iteration 0)

A 26-viewpoint location scout (`python3 tools/make_glowmere_valley_3.py --scout`) rendered in one pass,
with the source's look unchanged, to learn what the valley looks like before writing a shot for it.

It found the four cliffs and the water wall (F1, F2), the bathtub water (F3), the travel-beam streaks
(F4), the model-railway wides from the valley walls (F5), and the floating stems (F7).

It also found what already works:

- the hero mushrooms, especially the elder's gold gills from below;
- ferns as foreground;
- the aliens' pale bodies against the dark;
- the view up the valley toward the aurora.

## Iteration 0: the first cut

Built from the directives, the scout and the first cast trace.

- **The cut:** 40 shots.
- **Staging:** the saucer's whole story is staged against the music.
- **Look:** the base look (depth of field off, pulled-back lens effects, tamed water, distance haze)
  plus the section arc and the motif routes.
- **Checks before rendering:**
  - the staging: flyby 26.70–29.90 s, beam 170.35 s, dissolve fixed to land on the crash;
  - the framing: s20 had its subject out of frame for the whole shot.

**Rendered** (`build/gv3/v0.mov`): 13,530 frames at 11.8 fps, 0 GPU errors.

**What worked:**
- The staging landed on the music: the flyby in the pull-back, the approach from the suspension,
  the beam on the riser, the horse gone on the crash, the exit at bar 99.
- s16, the elder's gold gills from below with falling spores, is the film's best image.
- s22, the saucer appearing high over the elder's gills against the aurora, is the suspension's
  storytelling frame.
- s09 (Ember walking with the elder behind) and s15 (the bloom's spores against the sky) read cleanly.

**What failed** is F11–F22 in [05-quality.md](05-quality.md). The largest was my own base look:
- The distance haze was six times the source's, with a pale tint, so every hillside became a lilac
  wall and every frame went flat.
- Behind that came the water, still bathtub-bright.
- Then the set-piece: the riser never showed the horse being taken, and the drop did not read as the
  climax.
- Four shots failed outright: a buried camera, a blocked wide, and two near-black frames.

## Iteration 1: composition, the set-piece, and the look corrected

- **The look:**
  - the haze back to the source's strength with a night tint;
  - contrast restored;
  - dark water;
  - true black at the start and end;
  - a brighter drop with a stronger flash.
- **The shots:** 22 of 40 reworked, each recorded in `shots.py`:
  - the riser rebuilt so every shot is a different view of the one event, and the horse is seen;
  - nothing aims at a retired body;
  - Tide and Sage enter the film;
  - no shot is composed on open water.
- **The flyby** is lower and passes over the elder.

**Rendered** (`build/gv3/v1.mov`): 13,530 frames in 1,166 s (11.6 fps), 0 GPU errors. The cast trace
put every tracked subject in frame for its whole shot, including s20, which v0 had lost.

**What worked:**
- **The look.** The hillsides are dark night country, not a lilac wall (F11), and the film opens and
  closes on true black (F12).
- **The set-piece has a shape.** s23 (the saucer coming on beyond the elder's cap), s27 (straight up
  the beam) and s30 (the elder beside the beam) are three of the film's strongest frames.
- **The cast.** Ember, Tide, Sage, Rook and Vane all read, lit, against the valley.

**What failed** is F23–F36 in [05-quality.md](05-quality.md):
- **The river read as a cobbled road in about a dozen shots (F25).** It was the largest defect left,
  and it was the ripples, not the glow iteration 1 had tamed.
- **The riser's story still did not reach the screen.** The horse's glow had never existed (F24), and
  the beam clipped a fifth of the frame around it (F26).
- **Two dead motifs.** The heartbeat was measured on the v0 render and did nothing (F23); the horse's
  glow did nothing either. Both are the same silent failure: a multiplier on an emission that is zero,
  or owned by something else, bound without a warning.
- **Subjects lost.** The flyby missed the frame (F27), Ember was outside s25 (F29), the saucer left
  s34 in a second (F30), and three moving shots were blocked by the valley's own plants (F28).
- **Repeats and small misses (F31–F33).**

## Iteration 2: the water, the riser, and every lost subject

Each change was checked on stills or short clips before the full render: three water variants on
four stills, two beam densities on a continuous riser clip, and stills of every re-sited shot.

- **The water (F25):** faint, fine ripples that the shader fades with distance, so the river carries
  the sky like a mirror; the moon's glint on it tamed.
- **The riser (F24, F26):** the horse glows gold as it goes up, the valley's light being taken, and the
  beam is dim enough to see it through.
- **The heartbeat (F23):** a scored kick, one pulse per kick the track plays, multiplying the elder's
  warm tissue. It lands on the frame of each kick and is gone within the beat.
- **The saucer's story (F27, F30):** the flyby crosses the frame over the elder; the exit climbs away
  over the north rim instead of vanishing upward.
- **The shots (F28, F29, F31, F32):** eleven re-sited or re-staged, each recorded in `shots.py`.
- **The drop (F33):** a short flash and the film's one camera shake.
- **A new check:** `framing.py --video` asks, every half second, whether each subject's body is where
  it projects. On v1 it agrees with the sheets (s18: Sage seen in 84% of samples).


**Rendered** (`build/gv3/v2.mov`): 13,530 frames in 1,383 s (9.8 fps; it shared the machine with a
trace and the CPU suite), 0 GPU errors. The trace confirmed the staging:
- the flyby is visible from 26.7 s on the new line;
- the lift runs 172.9–177.70 s, unchanged;
- the saucer leaves the station at 181.4 s and is gone at 189.45 s.

**What worked** (F24–F33 in [05-quality.md](05-quality.md), nearly all fixed):
- **The river is water.** In s12, s14 and s39 it is a smooth dark ribbon winding up the valley to the
  elder, under the aurora. The wides read as a place where v1's read as a model railway. Measured on
  the one locked-off water shot, its shimmer fell by 44%.
- **The lift lands.** The horse rises cream-gold inside a beam dim enough to see it through (s29), is
  seen under the saucer from the south (s31), and goes into it on the last beat of the roll (s32).
- **Every subject is where the shot says.** The occlusion check sees Ember in s25 (0% in v1, 100%
  now), the horse, Tide, Rook and Vane throughout; Sage 88%.
- **The saucer's story reads end to end.** It crosses the sky over the elder in the pull-back, comes
  over the rim, takes the horse, and climbs away over the north rim until it is a star.
- **New frames among the film's best:** s11 (Vane watching the elder's gold across the valley) and
  s39 (the rhyme with s14).

**What is left, and why it stays:**
- **The moon's reflection in the pool** is the brightest thing in s08. It is a moon on a pond, and
  was kept.
- **A leaf wipes across s20 for a moment.** It is parallax in a moving follow, and Vane stays in view.
- **Sage is behind the grove's plants for 12% of s18.** The grove is dense, and the shot is about
  walking through it.
- **The elder's gold is a flat, clipped shape in close-up.** It was the same in v0, and is the
  Glowmere look, not a defect of this production.

**Iteration 3** was not needed as a full pass. v2 called for one change: the moon's glint on calm
water in the four shots that face it. It was a single setting (specular 0.55 → 0.25), checked on
stills.

## The final render

Before committing an hour to it, the river was checked on stills at the final resolution. The water
fix did not survive the change: the ripples fade by their size in pixels, the final has twice the
previews' internal resolution, and the texture came back across the far river (F37).

The ripple frequency was doubled and the amplitude halved, and 1080p stills of s12, s14 and s39
matched the v2 preview. Then the final was rendered at 1920×1080, 60 fps, 2× supersampling, with the
song: `build/gv3/final/glowmere-valley-3-1080p.mov`, with the project, scene and shot plan that made
it beside it.

**Rendered:**
- 13,530 frames in 2,061 s (6.6 fps), 0 GPU errors;
- H.264 at 1920×1080 and 60 fps, with the song as AAC, 225.5 s long, 920 MB;
- sequence hash `746d3e45c516c7be`.

**Reviewed** like every iteration, and it matches v2:
- **The sheets and cut pairs:** the same film, with a calmer river.
- **The occlusion check:** identical to v2.
- **Shimmer on s26:** 0.0028 per frame.
- **The only flags:**
  - the pull-back is dark, as directed;
  - the elder's gold clips 6.8% in its close-up;
  - the beam shots clip 3–10%, against 19% in v1.

## Verification

The engine changes (ADR-893–895) were run against both suites in the worktree's release build.

| Run | Result |
|---|---|
| GPU suite (`avgen_render_tests`) | 509 cases: 508 passed, 1 skipped. Main's figures at the pause |
| Full CPU suite (`avgen_tests`) | 3,518 cases: 3,496 passed, 19 skipped, 1 failed as expected (the slopes `shouldfail`), **2 failed** |
| The two failures | Both were existing tests whose world moved (07-technical §7.2, §7.3). The directed-orders test's control arm now faces Sage, proven by a throwaway build with the old water rule; ADR-623's pinned trace was re-pinned, as ADR-830 did. |
| After those two edits | `[directing]` 90 cases passed (2,697 assertions); `[defaultoff]` 2 cases passed (29 assertions) |

The full CPU run took 2 h 6 min, single-threaded, sharing the machine with renders. It was not
repeated after the two test edits: only those two test files changed, and both groups were re-run.
