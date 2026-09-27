# ADR-920: The treatment's arc reaches the director

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision audit (reports/director.md §2 row "Build / riser", recommendation 3):
`songPlanFromCues` dropped arc, energy and visualDensity, and `SectionCue::intentAt` had no caller.
**Follows:** ADR-247 (the shot intent and its arc), ADR-249 (Song Mode's `SongPlan`), ADR-899 (the
measured profile in the plan)
**Implemented by:** `ShotIntentProfile::arc` / `energy` / `visualDensity`, `SongPlanSection::intentAt`
and `intentAtProgress`, `SongEvent` and `SongPlan::events` (`src/app/song_plan.hpp/.cpp`);
`songPlanFromCues`, `songPlanFromMeasurements`; the Sequence panel's section inspector, through
`song::SectionCue::intentAt` (`src/ui/sequence_panel.cpp`)
**Tests:** `tests/unit/test_song_director.cpp` -- "The treatment's arc reaches the director through
the song model's own arc", "The new intent dials and the events round-trip, and a bad arc is
refused"; every existing `[song]` case, including the label scramble

## Context

A shot intent (`song::ShotIntent`) has always said how a passage should travel: `arc` is Steady,
Rising, Falling, Suspended or Burst, and `ShotIntent::atProgress` applies it -- a Rising treatment's
movement, energy, variation and cut frequency start at 35% of their stated values and arrive at them
at the section's end; a Burst lands them on the first frame and settles to half; Suspended pins
movement and cutting under 0.1. It is the song model's answer to "a riser accelerates" that never
names a riser.

None of it reached the director. `songPlanFromCues` projected each cue onto five axes and a camera
count and dropped `arc`, `energy` (how hard the treatment pushes) and `visualDensity` (how much
should be in frame). `SectionCue::intentAt`, the one function that applies an arc at a second, had
no caller. So "Building Tension", "Increasing Movement" and "Rising / Reveal" -- three of the four
treatments Glowmere Valley 3 put on its lift, arrival, lead-forward and riser -- cut exactly like a
Steady treatment with the same cut rate, and nothing could tell the drop's "Large-Scale Dynamic
Coverage" (push 1.0) from the plateau's exploration (0.5).

## Decision

**The profile carries the three.** `ShotIntentProfile` gains `arc` (a `song::Arc`), `energy` and
`visualDensity`, each 0..1 and refused out of range like the other axes. They are shapes and
magnitudes, not labels: the director never learns that risers exist, it learns that this section's
cutting accelerates, and the label-scramble test still holds. In a song plan's JSON they are the
intent's `"arc"` (a name; an unknown one is refused rather than read as Steady), `"energy"` and
`"visualDensity"`; a plan written before them reads as Steady, 0.5 and 0.5.

**`songPlanFromCues` projects them as the song model states them.** `songPlanFromMeasurements` --
the fallback for a detection with no film -- gives Steady, its measured energy as the push and its
density as the frame.

**One implementation of an arc.** `SongPlanSection::intentAt(seconds)` returns the profile with its
arc applied, by handing the four dials an arc moves to `song::ShotIntent::atProgress` and reading
them back. It is `SectionCue::intentAt`'s twin -- the director is handed a plan, not cues (the seam
ADR-249 draws), so it cannot call the cue's method, and a second copy of the arc arithmetic would
come to disagree with the first. A test holds the two equal at five points of a section for every
arc. `SectionCue::intentAt` itself gets its caller where a person chooses the treatment: the
Sequence panel's section inspector shows the cut rate the arc gives at the section's start and end.

**Events.** `SongPlan::events` -- `{name, subject, seconds, end}`, the shape of a watched play's
observation (ADR-767) -- is what ADR-922's peaks read. Written only when present, so a plan with
none is byte-identical to one written before.

## Consequences

- The director reads all three: the arc shapes a section's pace (ADR-921), the push finds the film's
  peaks (ADR-922), the visual density moves shot length (ADR-921).
- **Every film directed from a section timeline changes** wherever a section's treatment has a
  non-Steady arc: 10 of the 31 built-in treatments rise (5), fall (1), burst (3) or hold (1). Song Mode is baked, so
  a saved project's camera does not move until it is re-directed; `glowmere-valley-2-multicam` is
  Locked, where an arc changes nothing (a Locked section is one shot).
- The Auto-director panel's section tooltip shows the arc, the push and the frame; the Sequence
  panel's section inspector says what the chosen treatment's arc does to the cutting (ADR-921).
- `song::SectionCue::intentAt` has a production caller: the Sequence panel's section inspector reads
  the cut rate at a section's start and end through it ("cut rate 19% at its start, 55% at its end")
  for any treatment that is not Steady -- the same arithmetic the director applies, where the
  treatment is chosen.
