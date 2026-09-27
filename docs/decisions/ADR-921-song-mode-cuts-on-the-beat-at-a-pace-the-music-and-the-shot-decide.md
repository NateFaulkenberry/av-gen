# ADR-921: Song Mode cuts on the beat, at a pace the music and the shot decide

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision audit (reports/director.md §3 finding 1): the engine cutting "Rebuild"
made 34 of 39 shots between 5.90 and 5.92 s, put none of its 38 cuts on a downbeat (the median cut
99 ms off the nearest beat), and buried the drop 0.44 s into a travelling shot. The owner's brief §8:
"Do not enforce one global shot duration. Use shorter shots where the music and visual density
demand them. Allow longer shots when they create contrast or establish scale."
**Follows:** ADR-249 (Song Mode), ADR-896 (one musical time), ADR-897 (level-free density),
ADR-920 (the arc reaches the director)
**Implemented by:** `MusicalGrid`, `musicalGridFrom`, `SectionPace`, `layoutOnGrid`, `layoutFree`,
`snapSections` and the section loop of `directSong`, `ShotTiming`, `SongSectionCut`, `arcCutNote`
(`src/app/song_director.hpp/.cpp`); `FocalTarget::motion` (`src/app/cinematic.hpp`); `SongInputs`,
`songInputsForEngine`, `heroMotion` (`src/app/camera_director.hpp/.cpp`); the Auto-director panel's
Song section and "shortest build" (`src/ui/control_panel.cpp`); the Sequence panel's "cuts:" line
(`src/ui/sequence_panel.cpp`)
**Tests:** `tests/unit/test_song_director.cpp` (`[arc]`, `[grid]`, `[drop]`, `[duration]`,
`[motion]`, `[ui]`), `tests/integration/test_song_rebuild.cpp` (`[rebuild]`, skips without the song)

## Context

Song Mode chose a shot length from one global band: `cutRate` placed a hold linearly between the
Auto-director's longest and shortest shot, the section's energy nudged it, and the section was
divided into equal parts. Nothing snapped to a beat, nothing read how busy the music was (density was
structurally zero until ADR-897), nothing knew what the shot was of, and a Rising treatment cut like a
Steady one because its arc never arrived (ADR-920). Equal division of a section by a band is, by
construction, one duration.

## Decision

**Every cut lands on the beat grid, and every section boundary on its downbeat.** The grid is
ADR-896's one musical time -- the analysed track's tracked beats under `Engine::meter()` -- so "bar
97" is the bar `music.downbeat` fires on and the Director Agent's "bar 97 beat 1" names. Each section
boundary snaps to the nearest bar line within half a bar (else the nearest beat within half a beat,
else it stays); the film's own ends stay where they are (the silence before the first beat belongs to
the first shot). With no analysed track there is no grid and Song Mode cuts in seconds, as before.

**The duration rule.** For a section with the director free to choose (Guided or Expressive):

```
rate(p)  = intentAt(p).cutRate                        the treatment's dial, arc applied (ADR-920)
         + 0.10 * (2 * visualDensity - 1)               a fuller frame asks for shorter shots
         + 0.30 * (2 * density - 1)          [Expressive] the music's density
           clamped to 0..1
density  = 1/2 * composite / piece's largest composite  level-free (ADR-897), 0..1
         + 1/2 * clamp((onsets per second - 1) / 8)     4-on-the-floor with hats ~ 5/s ~ 0.5
           (the section's own energy and density when the plan carries no measurement)
band(r)  = longest * (shortest / longest)^r              geometric: a step in rate is a ratio of lengths

Steady, Falling:  L(p) = band(rate(p))
Rising:           L(p) = L0 * (L1 / L0)^p,   L0 = band(rate(0)),  L1 = max(floor, L0 / 2^(4 * rate(1)))
Burst:            L(p) = L1 + (L0 - L1) * e^(-p / 0.22),  L1 = band(rate(1)),  L0 = max(floor, L1 / 2^(4 * rate(0)))
Suspended:        one shot for the section; split at the grid only past 2 * longest
Locked:           one shot for the section (unchanged)

floor    = shortest for Steady, Falling, Suspended
         = max(min(shortest build, shortest), one beat)    for Rising and Burst -- the only arcs below the floor
per shot (Steady, Falling only):
  x 2^(variation * s_k)          s_k in [-1, 1] from the decision hash: the intent's own "how different
                                 successive shots are", now of length as well as framing
  x 1 + 0.35 * (motion - cast mean)    a subject that moves on its own holds longer
  x (1 + 0.6 * scale) * contrast       the section's opener only: scale = wide and about the world,
                                 contrast = 1 + (previous density - this density) when that exceeds 0.15, at most 1.5
ceiling  = longest; the opener's up to longest * min(1.8, scale x contrast); a hold's 2 * longest
```

A shot starting at `t` aims at the length whose pace integral (`∫ dt / L`) from `t` is one shot. The
cuts are then chosen by a dynamic programme over the grid's positions inside the section, minimising
the time-weighted mean over shots of `|ln(length / aim)| + landing`, where landing is where the shot's
end falls -- a phrase line 0, half a phrase 0.1, a bar 0.3, half a bar 0.6, any other beat 0.9 (scaled
down to a quarter for aims under a bar, where a beat is the natural unit) -- with length inside
`[floor, ceiling]` (half a beat of grace for the tracked grid's jitter), Rising lengths never longer
than the one before and Burst never shorter. Time-weighted, because summed per shot every extra cut
paid a landing cost and one forty-second shot came out cheaper than twenty-four aimed ones -- which is
what the first version of this did to the drop. Without a grid the pace integral gives the count and
the shots share the section in proportion to the pace and the per-shot factors. All of it is a function
of the plan, the heroes, the cameras and the grid: the same inputs give the same film, and a seek is
unaffected (the cut is a bake).

**A cut the viewer can see.** The framing bake re-frames only the director's own camera, so two shots
in a row on one *placed* camera are one picture. A section that wants one camera and cuts more than
once is shot on the director's camera when it is eligible, and otherwise held as one shot with a
warning; a section that would open on the placed camera the last one closed on rotates to its next
camera. The report marks any cut that is still not visible.

**Short shots do not travel.** Under 1.5 s a Transition becomes a Track (no whip from one subject to
another in half a second), and every shot's travel and sweep scale with its length up to 2.5 s.

**Transitions.** A section opener is a hard cut when the section is a peak (ADR-922), bursts, or its
energy or measured density steps by more than 0.18; otherwise the existing 0.8 s blend. Inside an
accelerating section every cut is hard (a blend would smear the acceleration); elsewhere the existing
0.35 s coverage blend, never longer than 30% of the shot.

**Subject motion** (`heroMotion`): an entity a scenario flies, or one whose behaviours travel
(`wander`, `explore`, `orbit`, `decide`, `motionScript`) or whose actions or schedule `move`, is 1; one
that moves in place (`hover`, `drift`, `bank`, `spin`, `liveliness`, `lookAt`, `interest`) 0.5; a plain
node whose hero reactions hover, rotate or pulse its scale 0.3; anything else 0.

### Where an artist finds it

- **Auto-director panel -> Shot mode: Song -> Shot timing**: "shortest shot", "longest shot" (the
  band) and **"shortest build"**, live in Song Mode again (it had been greyed out with "Song Mode has
  no builds"): the floor a rising or bursting section cuts down to, reaching 0.25 s so a one-beat cut
  is reachable at any tempo.
- **Auto-director panel -> the Song section's rows**: each section shows what the last cut made of it
  ("6 shots, 1.8-3.7 s, rising", "1 shot, 14.8 s, suspended", "peak on elder"); the tooltip adds the
  arc, the push and the visual density.
- **Sequence panel -> a section -> "shot"**: under the treatment, "cuts: rising -- shots shorten
  toward the section's end, below 'shortest shot' if they must" (and the other four), and for an arc
  that travels, "cut rate 19% at its start, 55% at its end" -- so an accelerating riser or a held
  suspension traces back to the control that made it.

## Consequences

- **"Rebuild"**, the 13 production segments with Glowmere Valley 3's treatments, band 1.8-7.5 s,
  shortest build 0.45 s, Expressive, three test cameras (`test_song_rebuild.cpp`, re-measured
  2026-09-26): **72 shots; all 71 cuts on a tracked beat, 65 on a bar line; every section boundary on
  its downbeat**, 7.3-18.9 ms after the true bar (mean 10.9) -- the grid's own error (ADR-896) -- so
  each shows on the true downbeat's frame or the next at 60 fps. Every cut is within 18.9 ms of a
  true beat (mean 8.9). Shot length CV 0.71 (the audit's cut: 34 of 39 within 20 ms of 5.91 s); the
  most common length (two bars) is 0.36 of the shots. **The riser (bars 93-96) cuts 8, 4, 2, 2
  beats**, after the break's single 16-beat hold, the lift 8-4-4-4-4-2-2-2-2, the suspension one
  14.8 s hold. The control arm -- the same song with one Steady treatment, no variation, Guided --
  cuts 61 shots with CV 0.019 and 0.97 of them one length: the measure tells the two apart.
- **Existing scenes.** Song Mode is a bake, so no saved project's camera moves until it is
  re-directed. Re-directing a Song Mode film (`glowmere-valley-2-song`, `-multicam`, `ufo-stack` and
  the `_diag-water-*` arms) now cuts on the beat with the rule above: different cut points, counts,
  subjects and framing. `glowmere-valley-2-multicam` is Locked, so it remains one shot per section,
  but its section boundaries now land on downbeats, and two adjacent Locked sections on one placed
  camera now switch the second to the director's camera. Continuous and Edited are untouched.
- `shortest build` is read by Song Mode: projects that set it (GV3: 2.0 s) now floor their risers
  there. At 130 BPM a 2.0 s floor admits no one-bar cut, so GV3's riser cuts 8-8 beats until it is
  lowered.
- A test re-baselined with evidence: "A section's treatment reaches the director and changes the
  bake" still passes unchanged -- its 3.69 s Build sections at a 2.0 s build floor ask for 1.1 shots,
  so they stay one shot at Guided; the roomy arm still separates the two treatments.
- The layout is O(n^2 x shots) over a section's beats for a Steady section and O(n^3) for an
  accelerating one; a section with more than 200 beats lays out on half-bars, more than 480 on bars.
  "Rebuild" directs in 8 ms (the project load and audio analysis take 4 s).
- **Not done:** local density inside a section (a fill, a kick gap) does not move a cut; the grid's
  one-frame lateness against the true beats (it sits 9 ms late on average) is not compensated -- a
  generator that leads its cuts by a frame, as GV3's does, applies that itself.
