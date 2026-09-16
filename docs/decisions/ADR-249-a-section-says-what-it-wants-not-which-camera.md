# ADR-249 — A section says what it wants, not which camera

**Status:** Accepted
**Date:** 2026-09-16


> **Terminology note, added 2026-09-16.** What this ADR calls `SectionDirection` /
> `SectionDirectionTable` is now `SectionPerformance` / `SectionPerformanceTable`, and its project
> key is `sectionPerformance`. The rename followed an audit
> (`docs/investigations/song-director-section-direction-audit.md`) which found the old name was the
> whole problem: this system produces **character actions** — move, face, pose — and "direction" was
> read as *camera* direction, so a disabled checkbox about performers looked like a blocked Song
> Director workflow. The decision recorded below is unchanged; only the names are.

## Context

AV Gen can analyze a song into sections (ADR-206), let a person edit those sections and keep their
edits across a re-analysis (ADR-215, ADR-216), and direct a camera from a fold of the audio
(ADR-062, ADR-075). Since ADR-245 it also has more than one camera, and a pure function that says
which of them is live.

What it could not do is join the two ends: a person who had spent ten minutes getting their song's
sections right had no way to say *what each section should look like*, and the Auto-director had no
way to be told. Its two modes — Continuous shot and Edited sequence — both fold the audio themselves
and decide everything from the section's musical kind. They are the camera's interpretation of the
music, not the author's.

The obvious shape for the missing piece is a table: Chorus → camera 3, Verse → camera 1. It is also
the shape that does not survive contact with the product. AV Gen's sections are deliberately
*general audiovisual timeline semantics* rather than musical structures: "Ocean Ambience", "Dream
Sequence" and "Character Reveal" are as legitimate as "Chorus", and a person can invent one at any
time. A table keyed on a musical label answers `Chorus` and has nothing at all to say about
`Ocean Ambience` — which is the case the feature exists for.

So the requirement is not "map sections to cameras". It is:

```
Chorus         -> Dynamic Hero Coverage        -> Auto Director -> available cameras -> decisions
Ocean Ambience -> Slow Environmental Exploration -> Auto Director -> available cameras -> decisions
```

with the director on the right-hand side of that arrow never having heard of either name.

### What the repository already had, and what it did not

* `analysis::SongStructure` / `SongSection` (ADR-206) — boundaries, a function, a confidence, an
  origin, and two measured quantities: `energy` and `density`. Persisted and editable (ADR-215).
* `signals::MusicalSection` — the *director's* vocabulary, where every value has to mean something
  to a shot. Four switch statements read it, and they are exactly the hard-coding this ADR refuses
  to extend: a new section kind falling through one of them produces a silently bland shot.
* `scene::CameraDirection` + `resolveActiveCamera` (ADR-245) — cameras, a shot track, and one pure
  function from (cameras, shots, events, time) to which camera is live.
* `app::Sequence` / `app::Shot` (ADR-062) — fourteen shot kinds, framing in subject radii, a bake to
  timeline keys.
* `seq::SectionDirectionTable` (ADR-216) — a *deliberately empty* seam, whose own header says the
  hole is "the entire remaining job" and that filling it with guesses would produce "a second
  director competing with the real one".

Nothing existed for: what a section wants visually, how much freedom the director has over it, or
how a director chooses between cameras.

## Decision

### 1. The song reaches the director as numbers, and there is one place it can enter

`app::SongPlan` (`src/app/song_plan.hpp`) is the *only* way a song reaches the Auto-director. A plan
is a list of sections; a section is a time span, a display label, six numbers, a camera count, an
autonomy, and an occurrence. There is no `SectionFunction`, no section type, and no musical
vocabulary anywhere in it or downstream of it.

```
struct ShotIntentProfile {
    std::string id;     // opaque: logged, never compared
    float heroEmphasis; // 0 the environment .. 1 the subject
    float distance;     // 0 intimate        .. 1 the widest this world offers
    float movement;     // 0 locked off      .. 1 constantly travelling
    float variation;    // 0 one setup held  .. 1 keep finding new ones
    float cutRate;      // 0 the longest hold.. 1 the shortest
    int   cameras;      // how many viewpoints this section wants used
};
```

**`id` is inert by construction.** It reaches log lines and panel rows and is compared against
nothing. That is not a convention anybody has to remember: `tests/unit/test_song_director.cpp`
scrambles every label and every intent id in a plan and requires the film to be byte-identical, and
the control arm below shows what that probe reports when somebody adds the `if` it is guarding
against.

**`ShotIntentProfile` is not a subset of a shot intent, it is a projection of one** onto the axes a
camera can act on. A shot intent in the song data model may carry a description, a category, an icon
and a colour; a camera cannot do anything with any of them.

**The seam is two functions at the bottom of one header.** Today a plan comes from JSON (the
project's `songPlan` block, or `--song-plan`), or from an analyzed structure read for its
*measurements only* — `songPlanFromMeasurements`, which reads `startSeconds`, `endSeconds`,
`energy`, `density` and `repetitionGroup`, and reads the display name only to fill in a display
name. When the song data model publishes `Section` / `SectionType` / `ShotIntent`, a third overload
joins them there and the measurement-derived one becomes a fallback. Nothing downstream moves.

That fallback is not only a placeholder. It is the demonstration that this engine can direct a piece
whose sections nobody has named — an ambient track the detector honestly answered `Other` for, which
ADR-206 says is the correct answer and not a degenerate one.

### 2. Song Mode authors camera shots; it does not select cameras

`directSong` emits two things: a framing bake for the Auto-director's own camera, and a list of
`scene::CameraShot`s. The shots go onto the composition's existing shot track, and
`resolveActiveCamera` remains the only thing in the engine that resolves them.

This is the difference between an *editor* and a *second selection system*. Song Mode does what a
person does in the Cameras panel, faster and from an intent. It invents no precedence rule, no
second live-camera concept, and no runtime state. An event camera still outranks a directed shot
unless that shot's section was Locked, so Glowmere's abduction still takes the frame from the middle
of a chorus — which is correct, and which fell out of using the existing system rather than being
designed.

A camera's suitability for an intent is scored on two axes it already carries: how wide it is (its
`focalLength`, or its `fovDegrees` when it states no lens) and how much it is *about a subject* (a
`followNode` or `aimNode` means it is watching something; the main camera frames whatever the shot
is of; a placed viewpoint is a shot of a world). No new camera property was added for this.

### 3. A directed camera shot says who wrote it

`CameraShot::Origin` — `Authored` or `Directed`. The moment two authors write to one list, the list
has to say which is which, or re-directing either erases a person's shots or piles a second cut on
top of the first.

This is the rule `installSequence` has always followed on the timeline, applied to the shot track:
**everything the director owns goes; everything else stays.** `Authored` is the default and is not
serialised, so every project written before this ADR reads back byte-identical.

### 4. Autonomy is a ceiling

Three levels — `Locked`, `Guided`, `Expressive` — and the film-wide control is the *lesser* of it
and the section's own, never the greater.

A global control that could raise a section's freedom would be a control that appears to do nothing
when lowered and everything when raised, and an author who sets a section to Locked has stated
something the film-wide default must not overrule. A control that can always be trusted to reduce is
worth more than a control that is sometimes an override.

### 5. Determinism: varied at bake, pure at playback

This is the decision the brief's section 21 asks for, and it has two halves that look contradictory
until they are separated.

**Song Mode must not be deterministic shot playback.** Two sections carrying a byte-identical intent
have to come out as different films, or the mode is a worse spelling of the shot track.

**ADR-091 must hold.** A seeked second must be a function of the second, not of how the playhead got
there.

Both, by making every decision a pure function of the *section's coordinates* rather than of a
stream or a clock:

```
h(seed, intent shape, occurrence, shot index)
```

* **`occurrence`** — which time round this material is — is what makes two passes differ. Verse 1
  and Verse 2 carry the same intent and differ only here.
* **`intent shape`** — a hash of the six axes and the camera count, *not* of the name — is what
  makes two sections carrying the same intent score the cameras identically.
* Nothing is drawn from a PRNG stream, for the reason ADR-202 already records: a stream makes every
  later choice depend on how many earlier ones were made, so adding one section would re-cast the
  whole rest of the film.

The **opening camera** is a guarantee rather than a tendency: the seed picks the phase of the camera
rotation and the occurrence advances it, so a second pass over the same intent never opens on the
same camera when the section has more than one to open on. Left to a hash it would hold about half
the time, and the property it protects is the one the whole mode is judged on.

Everything is decided once, at install, and baked. Nothing runs per frame, nothing runs on the audio
thread, and no song analysis happens during playback — Song Mode does not fold audio at all, which
makes it the *cheapest* of the three modes to re-cut.

### 6. Five shot kinds are out of reach, and stay out of reach

`Entry`, `Passage`, `Descent`, `Ascent` and `Flyby` are moves about a direction in the world — into,
through, down, up, past — and none of the six intent axes expresses a direction. They remain
available to an authored shot and to the other two director modes.

Stated rather than fixed. Adding a seventh axis so that a table comes out complete is adding a knob
for the wrong reason, and the knob would be one an author has no vocabulary for.

## What was measured

**Regression.** Unit **1,935 cases / 1,935,959 assertions** (3 skipped), all passing — against a
baseline of 1,908 / 1,935,021 (3 skipped). The 27 new cases and 938 new assertions are this
change's. Render **301 cases / 410,167 assertions** (1 skipped) — *exactly* the baseline, every pixel test unchanged — run through `tools/gpu-lock.sh` (ADR-170).

(Two `[city]` cases failed in this worktree before a line of this change was written: `assets/kenney/city`
is an untracked asset directory and had not been linked in. Symlinked, and they pass. Recorded because
a regression number taken against a missing asset is not a regression number.)

**Control arms (ADR-182).** Every probe was shown capable of failing. Each row is the whole `[song]`
suite built against a deliberately wrong implementation. The arms were run against the suite
as it stood at 26 cases and 871 assertions; the twenty-seventh case (the mode-aware validation
noted below) landed after them and is not in these counts:

| the implementation was made to... | result |
| --------------------------------- | ------ |
| the director branches on the intent's NAME | 2 case(s), 7 assertion(s) FAILED |
| occurrence is dropped from the decision hash | 1 case(s), 2 assertion(s) FAILED |
| occurrence no longer advances the opening camera | 1 case(s), 12 assertion(s) FAILED |
| every shot takes the best-matching camera, so `cameras` does nothing | 4 case(s), 17 assertion(s) FAILED |
| an intent asking for more cameras than the scene has is silently granted | 1 case(s), 1 assertion(s) FAILED |
| the film-wide autonomy is a setting rather than a ceiling | 1 case(s), 29 assertion(s) FAILED |
| a shot may run past its section's end | 16 case(s), 55 assertion(s) FAILED |
| the measured plan reads the section's label after all | 1 case(s), 1 assertion(s) FAILED |
| `autonomy` is dropped from the Auto-director's project block | 1 case(s), 1 assertion(s) FAILED |
| a directed camera shot is appended rather than replacing the last direction's | 1 case(s), 1 assertion(s) FAILED |
| `origin` is not written into the scene document | 1 case(s), 1 assertion(s) FAILED |
| the old planner answers Song Mode as an edited sequence | 1 case(s), 1 assertion(s) FAILED |
| the song plan is not written into the project | 1 case(s), 1 assertion(s) FAILED |

Restored, all 26 cases and 871 assertions passed; the suite now stands at 27 and 875. **Had the change been wrong**, these are the numbers
the suite would have reported instead of a clean run — which is the point of recording them.

**The demonstration.** `examples/world/glowmere-valley-2-song.json`: the three-camera Glowmere demo
with its authored shot track removed, every camera made available to the director, and an eight-
section song plan added. The director's own log is the acceptance test.

```
 0.00-  4.00  Intro           [Atmospheric Establishing]      x1  Valley Wide      establish   13.9->13.9r  lantern-cap
 4.00-  7.50  Verse           [Hero Performance]              x1  Hero Free Roam   track        6.4-> 6.4r  visitor
 7.50- 11.00  Verse           [Hero Performance]              x1  UFO Watch        track        6.5-> 6.5r  visitor
11.00- 14.00  Pre-Chorus      [Building Tension]              x1  Hero Free Roam   discovery   10.6-> 6.7r  ridge-cap
14.00- 15.83  Chorus          [Dynamic Hero Coverage]         x1  Valley Wide      heroReveal   6.3->10.4r  visitor
15.83- 17.67  Chorus          [Dynamic Hero Coverage]         x1  UFO Watch        heroReveal   8.1->13.3r  umbra-cap
17.67- 19.50  Chorus          [Dynamic Hero Coverage]         x1  Hero Free Roam   heroReveal   6.2->10.2r  visitor
19.50- 23.00  Ocean Ambience  [Slow Environmental Exploration] x1 Valley Wide      drift       12.9->12.9r  bloom-cap
23.00- 26.50  Verse           [Hero Performance]              x2  UFO Watch        track        6.3-> 6.3r  spire-cap
26.50- 30.00  Verse           [Hero Performance]              x2  Hero Free Roam   track        6.6-> 6.6r  spire-cap
30.00- 32.00  Final Chorus    [Peak Multi-Camera Hero]        x2  UFO Watch        heroReveal   7.6->12.8r  visitor
32.00- 34.00  Final Chorus    [Peak Multi-Camera Hero]        x2  Hero Free Roam   heroReveal   8.2->13.9r  veil-cap
34.00- 36.00  Outro           [Slow Pullback]                 x1  Valley Wide      establish   13.3->13.3r  scree-cap
```

2160 frames at 1920x1080, supersample 2, **0 GPU errors**, sequence hash `2bd42fdc9bfc54e2`, written to
`examples/world/renders/glowmere-valley-2-song.mov` through `tools/gpu-lock.sh`.

Three things to read out of it. Every camera is used, and nobody said which. `Ocean Ambience` — a
section type no analyzer produces and nothing in `src/` has heard of — is directed by the same code
as `Chorus`, and differs from it only in its six numbers. And the two `Verse` sections carry a
byte-identical intent and come out as different films: different cameras, in a different order, on a
different subject, at different distances.

## Consequences

* The Auto-director now has a mode that does not need audio. A project carrying a song plan directs
  and renders from the project alone, which is what makes an offline render of a Song Mode cut
  reproducible.
* `docs/auto-director.md` was reconciled rather than appended to. Two obsolete claims went with it:
  "One camera, no cameras", false since ADR-245, and the ADR-217 aim-hold paragraph, whose code no
  longer exists. ADR-245's own migration appendix is stale in the same direction and is left as the
  historical record it is — `camera_director.hpp` already carries the corrections.
* A project gains an optional `songPlan` key and an `autoDirector.autonomy` key; a scene gains an
  optional `origin` on a camera shot. All three are written only when non-default, following the
  convention `autoDirector`, `timeline` and `cameraDirection` already set.
* `--save-scene` exists, the symmetry `--save-project` had always lacked. Song Mode needed it because
  its camera track lives in the *scene* document (ADR-245) and so cannot be persisted by saving the
  project.
* **A defect was found and fixed on the way.** `--direct` took the Auto-director's *defaults* rather
  than the settings the loaded project had saved, because `syncDirectorSettings()` runs below the
  `--direct` call in `Application::init`. So `--project p.json --direct` on a project that had chosen
  Edited sequence silently produced a continuous take. The same shape of bug ADR-225 records, in the
  other direction.
* `seq::SectionDirectionTable` (ADR-216) is **still empty, and should stay that way.** That seam
  turns sections into scheduled `EntityAction` events for the Director *behaviour* layer, which is a
  different job from pointing a camera. Song Mode is not its implementation, and filling it with
  camera decisions would build exactly the second director its own header refuses.
* `AutoDirectorSettings::validate()` became mode-aware in one place: the rule that a build's
  minimum may not exceed the ordinary minimum is not checked in Song Mode, which has no builds.
  Found by using `--song-plan`, not by reading: the panel *disables* `shortest build` in Song Mode,
  so lowering `shortest shot` past it produced a refusal whose only cure was a slider the mode had
  greyed out. The pair is checked again the moment a mode that reads it is chosen.
* **The panel was verified by looking at it** (`--capture-ui`), and the first capture found two
  defects a model cannot see from source: the per-section autonomy combo was drawn *over* the intent
  name for any name longer than about twenty characters, and two sliders Song Mode never reads --
  `shortest build` and `hold subject` -- were live in it. The combo moved to the left of the row and
  the two sliders are disabled with a tooltip saying why.
* What Song Mode does *not* own: the section type library, the analyzer's assignment of types, custom
  section types, the default intent for each type, and the re-analysis policy. Those belong to the
  song data model, and the seam above is the whole of the coupling between them.
