# ADR-922: A peak goes to its event's subject, and the dead emphasis outputs are cut

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision audit (reports/director.md §2 rows "Hero moments" and "Visual
hierarchy", §3 finding 2): on the multicam film "the drop shot went to spire-cap because it was
spire-cap's turn"; the spotlight emphasis was emitted to `camera/focus/emphasis`, which is never
registered, so every install dropped it, and `ShotSpan::emphasis` was saved and read by nothing.
**Follows:** ADR-062 (the spotlight), ADR-207 (shot spans), ADR-249 (Song Mode's cast rotation),
ADR-442 (no compatibility shims), ADR-767 (a watched play's events), ADR-920 (the push and the events)
**Implemented by:** the peak and event-subject rules in `directSong` (`src/app/song_director.cpp`);
`songEventsForEngine` and `songPlanForEngine` (`src/app/camera_director.cpp`); the bake without the
emphasis track (`Sequence::toTimelineTracks`, `src/app/cinematic.cpp`); `kCameraTargets`
(`src/app/camera_director.cpp`); `world::ShotSpan` and the project's `cameraShotSpans`
(`src/world/effects/effect_timing.hpp`, `src/app/engine.cpp`)
**Tests:** `tests/unit/test_song_director.cpp` -- "A peak section goes to its event's subject, else to
the film's hero", "A watched play's events and the saved plan's reach Song Mode's plan",
"installSequence drops no baked track"; `test_song_rebuild.cpp` -- the drop opens on the film's hero;
`test_cinematic.cpp`, `test_wave_effects.cpp`, `test_directors_cut_parked.cpp` re-baselined

## Context

Song Mode cast every shot from a weighted rotation, leaned towards the film's subject by the intent's
hero emphasis. That is right for most of a film and wrong for its payoff: whoever's turn it was got
the drop. The audit asked that a peak go to "a staging set piece, the most important hero, or the
subject of the section's event".

When a set piece happens is only known by playing the film -- the saucer's beats are sequential and
self-timed, and GV3 measures where they land with `avgen_cast_trace`. The engine already has the
generic record of that: a watched play's observation (ADR-767), `{name, subject, seconds, end}` for
every world event and every event camera's span, stored on the directing plan that asked. The
setpieces stream will publish `setpiece/<id>/beam`, `.../lift` and `.../depart` into exactly that
record. So Song Mode reads events generically -- by name and subject, never by parsing the name --
and works for set pieces the day they merge, without depending on them.

The emphasis half is ADR-442's plain case: an output with no reader.

## Decision

**A section is one of the film's peaks** when its push (the treatment's `energy`, ADR-920) times its
music energy (the section's level-free composite as a share of the piece's largest; its own energy
when unmeasured) is at least 0.85 of the film's largest and at least 0.5. A film in which more than
half the sections qualify has no hierarchy to honour and no peaks.

**A peak opens on the subject of its event.** The event is the first in the plan's `events`, in time
order, that happens from a bar before the section starts to its end, or whose span reaches into it,
and whose subject is a hero. The peak's opening shot and every shot that starts before the later of
the event's end and the section's first phrase go to that subject -- held across the section's
cameras for coverage -- and then the rotation resumes. With no such event, the peak opens on the
film's hero (the most important). An event whose subject is not a hero is said, not dropped: "star it
in the World window for the director to give it the peak". A shot held for a peak never travels to its
subject (a Transition there becomes a Track): the payoff lands on the subject.

**The events** are the plan's own (`"events"` in a song plan -- what a generator writes, or a watch's
observation pasted as it stands) and, for a plan the engine builds, every event on every directing
plan's observation, merged in time order without duplicates.

**Cut, not wired.** `Sequence::toTimelineTracks` no longer bakes `camera/focus/emphasis`, the director
no longer owns it (`kCameraTargets`: six), and `world::ShotSpan` loses `emphasis` -- the project stops
writing it into `cameraShotSpans` and stops reading it. Wiring it would have meant inventing a visual
consumer (exposure, depth of field, rim light) the owner has not asked for, and every film the Auto-
director has cut would then change look; a hierarchy is expressed instead where the brief asked for it,
in who the peaks go to. `Spotlight` stays on the shot: `Sequence::validate`'s coverage check (a spotlit
subject must fill some of the frame) reads it.

### Where an artist finds it

- **Auto-director panel -> the Song section's rows**: a peak's row reads "peak on <subject>".
- **Which sections are peaks** follows the treatment chosen in **Sequence panel -> section -> shot**
  (its push) and the music; **who the fallback is** follows the heroes' importance (**World window ->
  Objects -> a starred object -> importance**); **which events count** are the song plan's and the
  directing plans' watched events. Nothing new is registered as a parameter; the emphasis track never
  was, so no panel offered it.

## Consequences

- **"Rebuild"** with GV3's treatments: the drop (push 1.0 on the second-loudest music, score 0.95) is
  the only peak -- the riser scores 0.65, the arrival 0.63 -- and opens on the film's hero on bar 97's
  downbeat, holding it for eight bars across three cameras. On the GV3 scene with its staged events as
  a watch would report them, the drop opens on `visitor` (its departure at bar 99), with a warning that
  the horse's `abduction/retire` at 177.70 s has a subject that is not a hero.
- **Existing scenes.** A re-directed Song Mode film gives its peaks to event subjects or its hero
  instead of the rotation. `camera/focus/emphasis` was never installed, so no saved camera changes.
  The 14 tracked projects with `cameraShotSpans` carry an `"emphasis"` key on every span (562 spans:
  `glowmere-valley-2`, `-song`, `-multicam`, `effects/ufo-stack`, `_pre-defects` and the nine
  `_diag-water-*` arms): it is no longer read -- it never was, by anything but the loader -- and
  drops on each project's next save. They are left as they are rather than stripped, a deliberate
  departure from ADR-441's "strip it in the same commit": the key is inert data, not a shim nothing
  can remove; `glowmere-valley-2-multicam` is not to be modified (the owner's rule); and the other
  thirteen are shared example files other streams may also be editing, where 562 one-line deletions risk merge
  conflicts for no behaviour. Stripping them later is `del span["emphasis"]` over those files.
- The director's install log no longer says "1 baked track(s) name parameters this build does not
  have and were left out: camera/focus/emphasis" on every direction; `installSequence` keeps the check
  for anything else, and a test holds that every baked track is installed.
- **Not done:** the event's subject must be a hero; a subject the director has no position for (the
  horse, the saucer when unstarred) is refused by name. A peak does not yet choose among several
  events by importance -- the first in time wins.
