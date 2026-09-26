# The revision's iterations

Each iteration names what changed, what the evaluator measured and what was decided. The
evaluator is the Creative Critic. Session `gv3-revision` holds one track per category.

## Iteration 0: the baseline (the first pass's delivered final)
- **Job:** `job_1a0def77d9084ad74` (session `gv3-revision`, track `film`), run 2026-09-26.
- **Input:** `build/gv3/final/glowmere-valley-3-1080p.mov` (1920×1080, 60 fps, 225.5 s, with the song), evaluated in preview mode against the first pass's scene data and intent.
- **Completeness:** complete; every core check ran. Total time 198 s.

**Headline:** 89 issues (0 critical, 9 high, 50 medium, 30 low) over 40 shots. The weakest
dimensions are composition 0.64, motion 0.70, visual hierarchy 0.71, cinematography 0.75 and
technical quality 0.79.

The baseline, by the categories the brief asks to evidence:

| Category | Measured on the first pass |
|---|---|
| **Audio reactivity** | 24 route–entity pairs configured. **Only 1 is observed in the pixels,** the elder's kick heartbeat (+2.0% averaged over the film, z 8.5; +9% in s16 alone). 7 are configured but show no measurable response, 9 have too few clean events to judge, and 3 are behavioural targets. |
| **Camera stability** | 10 "camera shake / wobble", 9 "the camera path itself jitters", 3 "abrupt camera acceleration" and 2 "wobble on a camera move" findings. The shakiest shots are follow shots: s28, s18, s03, s29, s09, s36, s38, s25. |
| **Water and fine detail** | 9 "shimmering / unstable fine detail" findings (s09, s10, s18, s20, s37, s38 …). |
| **Pacing** | 22 s of 225 s (10%) comes after a shot's last new information. s16 and s24 show nothing new after their first frame; s08 holds 2.5 s of its 7.4. There is one "cutting slows where the music gets more energetic" finding, and three pairs of near-identical framings. |
| **Aliens** | Longest standing still: Sage 67.3 s, Rook 26.1 s, Vane 22.3 s, Tide 12.8 s, Ember 9.6 s. Repeated paths: Sage 10, Ember 7. 38% of the film is led by an alien. |
| **Animals** | Stationary 23–44% of the film. Each animal turns in place for 5.8–12.4 s in total. Up to 5 repeated paths each. 7 of 12 are anchored on 17–25° slopes; this is measured separately, because the evaluator had no ground samples, so the next run must supply them with `--ground`. |
| **UFO events** | One "several 'saucer' events are framed alike" finding. |
| **Composition and hierarchy** | 13 composition and 12 visual-hierarchy findings. The worst: the visitor out of frame for 67% of s07; horse-11 out of frame for 50% of s32; a bright element outweighing the subject in s20 and s22. |
