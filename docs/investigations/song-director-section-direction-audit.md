# Song Director / `sectionDirection` Architecture Audit

**Status:** audit only. No production behaviour was changed by this document.
**Date:** 2026-09-16
**Placed here** rather than in `docs/architecture/` because `docs/investigations/` is where this
repository already keeps documents of exactly this shape (`ui-responsiveness.md`), and a second
convention for one file is a convention nobody will find.

---

## Executive Summary

**The audit's central premise is incorrect, and correcting it is the most useful thing this document
does.**

The brief asks why "the current *Generate Initial Director Sequence* workflow requires a
`sectionDirection` table" and treats that requirement as what blocks the beginner path

> Import Song → Analyze Song → Generate Initial Shots → Song Director.

It does not block it. **Song Mode never reads `sectionDirection`.** A repository-wide search for the
identifier finds it in exactly three places — its own definition (`seq/section_actions.hpp`), its
storage (`seq::Sequence`), and the UI checkbox that gates on it. The Song Director, the shot
generator, the camera resolver and the multi-camera system contain no reference to it, directly or
transitively.

The beginner workflow **already works today without any table**:

```
Import audio  →  Analyze song structure  →  Auto-director: Song  →  Play
```

`songPlanForEngine` (`src/app/camera_director.cpp:425`) requires *one of* an authored song plan, a
section timeline, or an analyzed structure — and pressing Analyze produces the second and third
together. Nothing in that path consults a director table.

**What the checkbox actually gates is a different feature that has been mistaken for this one.**
"Generate initial Director sequence" generates **character actions** — `EntityAction` events that ask
the cast to walk, pose or look — and *that* is what needs an authored table. It is about what happens
in the world, not about where the camera is. The two live on the same timeline and answer to the same
section boundaries, which is why they are easy to confuse, but they are separate systems with
separate consumers.

So the real finding is a **naming and presentation defect, not an architectural one**:

* the checkbox is called "Generate initial **Director** sequence" while the Song **Director** works
  without it;
* it sits in the audio-import popup, immediately beside "Analyze song structure", which implies the
  two are steps of one workflow;
* its disabled state therefore reads as *"the beginner path is blocked"* when it means *"nobody has
  said what the characters should do"*.

The architecture the brief describes as intended is, with one documented exception, the architecture
that exists.

---

## Repository Findings

The systems, and where they live:

| system | location | role |
|---|---|---|
| Song analysis | `src/analysis/structure.{hpp,cpp}` | the detector. Produces `analysis::SongStructure` — sections, functions, confidences |
| Section timeline | `src/song/section_timeline.{hpp,cpp}` | **the film.** `song::Section`: type, label, span, optional shot-intent override, per-field provenance |
| Section types | `src/song/section_type.{hpp,cpp}` | 60 built-ins across 5 categories, plus a person's own |
| Shot intents | `src/song/shot_intent.{hpp,cpp}` | the semantic cinematic vocabulary: focus, framing range, movement, energy, variation, cut frequency, camera count, arc |
| Shot language | `src/song/shot_language.{hpp,cpp}` | the vocabulary object: type → default intent, plus custom definitions |
| Cue sheet | `src/song/section_cue.{hpp,cpp}` | `song::SectionCue` — **the only thing the director consumes** |
| Song plan | `src/app/song_plan.{hpp,cpp}` | `app::SongPlan` — the director's own projection of a cue sheet |
| Song Director | `src/app/song_director.{hpp,cpp}` | turns a plan into shots on cameras |
| Multi-camera | `src/scene/camera_rig.{hpp,cpp}` | `CameraRig`, `CameraShot`, `resolveActiveCamera` |
| Auto-director | `src/app/camera_director.{hpp,cpp}` | the three modes and the shot bake |
| **Section direction** | `src/seq/section_direction.hpp`, `src/seq/section_actions.{hpp,cpp}` | **character actions from sections** |
| Reanalysis | `src/song/reanalysis.{hpp,cpp}`, `from_analysis.{hpp,cpp}` | reconciles a fresh detection against an authored timeline |

Relevant ADRs: **206** (structure from repetition), **215** (a decided section outranks a detected
one), **216** (the sequencer owns the song), **245** (a camera is not *the* camera), **247** (the
director is never told what a chorus is), **249** (a section says what it wants, not which camera).

---

## Current `sectionDirection` Implementation

### Data model

```cpp
// src/seq/section_direction.hpp
struct SectionDirection {
    std::string subject;       // which entity
    std::string verb;          // what to ask of it
    std::string argument;      // the verb's object
    int priority = 0;
    double delaySeconds = 0.0;
};

// src/seq/section_actions.hpp
struct SectionDirectionEntry { signals::MusicalSection kind; SectionDirection direction; };
struct SectionDirectionSet   { std::vector<SectionDirectionEntry> entries; ... };
```

**Stored on** `seq::Sequence::sectionDirection` (`src/seq/sequence.hpp:348`) — so it is
**sequence-level**, which is project-level in practice since a project carries one sequence.

**Serialized** in `Sequence::toJson` / `fromJson` under the key `"sectionDirection"`, written **only
when non-empty** so an unauthored project does not grow an empty array on every save.

**Optional.** Absent is the default and the common case. When absent,
`generateDirectorEvents` produces no events and reports one warning per declined section kind.

### Semantics — traced, not assumed

The brief asks whether a row is a camera instruction, a character instruction, a scene instruction, a
shot-generation instruction, or a semantic mapping. Tracing it:

```
SectionDirectionSet
  → seq::tableFrom()                       → SectionDirectionTable
  → seq::generateDirectorEvents()          → SequenceEvent{ what.kind = EntityAction }
  → EventDispatcher                        → Engine::firedEvents()
  → Engine::applySectionActions()          → seq::actionFromEvent()
  → entity::ActionDesc{ Move|Face|Pose|… } → EntityWorld::direct()
```

It is a **character/action instruction**, unambiguously. `seq::actionFromEvent`
(`src/seq/section_actions.cpp`) maps the verb onto `entity::ActionKind` — `Wait`, `Move`, `Face`,
`Pose`, `Interact`, `Equip`, `Unequip`, `Set` — and the terminus is
`EntityWorld::direct(entity, actions, now)`. **No camera type appears anywhere on that path.**

The example in Glowmere's multi-camera project is exactly this:

```json
{ "section": "drop", "subject": "rook", "verb": "pose", "argument": "react" }
```

`rook` is an alien character; `react` is an activity in its `clips` map. The row says *when a drop
lands, that character reacts.* It says nothing about photography.

### Consumers

| file | function | input | what it does | mandatory? |
|---|---|---|---|---|
| `src/seq/section_actions.cpp` | `tableFrom` | the set | wraps it as a lookup function | no |
| `src/seq/section_direction.cpp` | `generateDirectorEvents` | table + `SongStructure` | emits one `SequenceEvent` per distinct section name the table answers for | no — declines produce warnings |
| `src/app/engine.cpp` | `applySectionActions` | this frame's firings | maps and hands to the action system | no — a frame with no firings returns immediately |
| `src/ui/sequence_panel.cpp` | `drawImportPopup` | `entries.size()` | enables/disables the checkbox | — |
| `src/ui/sequence_panel.cpp` | analysis completion | the set | generates events if the box was ticked | no |

**Nothing else.** In particular: not `song_director.cpp`, not `camera_director.cpp`, not
`camera_rig.cpp`, not `song_plan.cpp`.

### Producers

* **Hand-edited JSON** — the only route today. `examples/world/glowmere-valley-2-multicam.json`
  carries a seven-row starter table.
* **Programmatic** — `SectionDirectionSet` is an ordinary struct.
* **Tests** — `tests/unit/test_section_actions.cpp`.
* **No UI producer.** There is no editor for the table. This is a real gap and is listed under
  *Preserve / Adapt / Replace*.

### Missing-table behaviour, exactly

The message the brief quotes comes from `src/ui/sequence_panel.cpp`:

```cpp
const std::size_t directorRows = engine.sequence().sectionDirection.entries.size();
ImGui::BeginDisabled(directorRows == 0);
ImGui::Checkbox("Generate initial Director sequence", &generateOnImport_);
```

The condition responsible is **`entries.size() == 0`** and nothing else. It gates one checkbox in the
import popup. It does not gate analysis, section generation, the shot language, the Song Director,
camera selection or playback.

---

## Current User Workflow

Reconstructed from code. **Two independent paths leave the same point**, which is the source of the
confusion:

```
                          IMPORT AUDIO
                               │
                          ANALYZE SONG
                               │
              analysis::SongStructure  (the detector's report)
                               │
                song::timelineFromStructure / applyAnalysis
                               │
              song::SectionTimeline    (the film; types + intents)
                               │
              ┌────────────────┴─────────────────┐
              │                                  │
    PATH A — CAMERAS                    PATH B — CHARACTERS
    (no table needed)                   (needs sectionDirection)
              │                                  │
    song::cueSheet()                   generateDirectorEvents()
              │                                  │
    songPlanFromCues()                 SequenceEvent{EntityAction}
              │                                  │
    app::SongPlan                      EventDispatcher → firedEvents()
              │                                  │
    directSongFromPlan()               applySectionActions()
              │                                  │
    shots on CameraRigs                EntityWorld::direct()
              │                                  │
    resolveActiveCamera()              characters move / pose
              │                                  │
           PLAYBACK  ◄──────────────────────────┘
```

**Path A is the beginner workflow and has no `???` in it.** The brief's diagram expects a missing
prerequisite between section data and the shot sequence; there isn't one.

### Prerequisites, classified

| prerequisite | for | genuinely necessary? |
|---|---|---|
| audio imported | both | yes |
| analysis run (or a timeline authored) | Path A | **yes** — Song Mode fails with an explicit message otherwise |
| section timeline | Path A | no — migrated automatically from the structure on load |
| section types / shot intents | Path A | no — 60 built-ins ship; every section resolves to a treatment with nothing authored |
| cameras defined | Path A | no — `kMainCamera` always exists (ADR-245) |
| **`sectionDirection`** | **Path B only** | **an implementation choice, and correctly so — see the diagnosis** |

---

## Current Architecture Diagram

```
                    CURRENT IMPLEMENTATION  (as built)

  Song ──► Analyzer ──► SongStructure ──► SectionTimeline ──► SectionType
                                                │                  │
                                                │         (ShotLanguage: 60 built-ins)
                                                ▼                  │
                                          SectionCue ◄─────────────┘
                                                │   (resolved ShotIntent, by value)
                                                ▼
                                          SongPlan  (ShotIntentProfile: 5 axes + count)
                                                │
                                                ▼
                                          SongDirector
                                                │
                                          CameraRig / resolveActiveCamera
                                                │
                                                ▼
                                            Playback

  ── separately, and not on that path ──
  SectionDirectionSet ──► generateDirectorEvents ──► EntityAction ──► EntityWorld::direct
```

The two chains share only the section boundaries they are keyed to.

---

## Intended Architecture

The brief's intended model:

```
Song → Song Analyzer → Section Timeline → Section Type → Default Shot Intent
     → Initial Shot Sequence → Song Director → Actual Camera Decisions → Multi-Camera
```

Compare to the left-hand chain above. **They are the same chain.** Every arrow the brief asks for
exists, in the order it asks for, with the separations it asks for:

* section type and shot intent are distinct objects (`SectionType::defaultShotIntent` is an id, not a
  copy);
* shot intent is semantic, not a camera (`ShotIntentProfile::id` is documented as opaque and is never
  compared by the director);
* the director consumes intent and never a musical label — enforced by
  `tests/unit/test_section_cue_isolation.cpp`, which **fails the build** if `section_type.hpp` becomes
  reachable from the director's headers.

---

## Architecture Gap Analysis

The brief's §6 asks whether the implementation resembles
`Section → sectionDirection → Director Rule → Camera`. **It does not.** There is no path from
`sectionDirection` to a camera.

Nor is `sectionDirection` conflating musical semantics with cinematic intent: it is keyed by
`signals::MusicalSection` (musical) and produces `entity::ActionDesc` (character). It never touches
framing, distance, cut timing or camera identity.

**The one real gap** is presentation, and it is worth stating precisely because it is what generated
this audit:

| what the UI says | what is true |
|---|---|
| "Generate initial **Director** sequence" | generates **character actions**, not director shots |
| sits in the import popup beside "Analyze song structure" | is not a step in the analyze→direct path |
| disabled, therefore the workflow looks blocked | the workflow it appears to block does not use it |

A second, smaller gap: **there is no UI to author the table**, so the only way to enable that checkbox
is to hand-edit JSON.

---

## Shot Generation Analysis

The brief's §9 asks whether shot generation is `Section → Shot` unnecessarily gated, or
`Section → sectionDirection → Shot` by genuine dependency. **Neither: it is `Section → Shot`, and it
is not gated at all.**

`app::directSongFromPlan` (`src/app/camera_director.cpp`) already:

* knows section boundaries — they are `SongPlanSection::startSeconds`/`endSeconds`;
* knows section treatment — `ShotIntentProfile`, five axes and a camera count;
* creates shots and decides durations — from `cutRate` against the director's shot-timing settings;
* uses multiple cameras — `cameras` is a request the resolver satisfies against what is eligible;
* makes autonomous decisions — `occurrence` differs between repeats of identical intents, which is
  what makes Verse 1 and Verse 2 come out differently;
* has defaults — 60 section types each naming a default intent.

It **does not** reference `sectionDirection`, and it can generate a complete baseline sequence with
nothing authored.

---

## Song Director Analysis

`songPlanForEngine` resolves a plan in three tiers, most-authored first:

1. an explicitly authored `songPlan` (`--song-plan` or the project's block);
2. the **section timeline** through `song::cueSheet` → `songPlanFromCues`;
3. the analyzed structure read for *measurements only* — `energy`, `density`, `repetitionGroup` —
   never for labels.

Tier 3's existence is what makes the beginner path work the instant analysis finishes, including on a
project that has never had a timeline.

---

## Multi-Camera Analysis

Song Mode consumes the ADR-245 system rather than duplicating it. Camera discovery, eligibility,
selection, hero tracking and switching are all `camera_rig.cpp`'s; the plan contributes a *request*
(`ShotIntentProfile::cameras`) which the resolver satisfies against what the scene actually has.

Nothing is missing here for the intended workflow. A world with one camera gets `kMainCamera`, which
always exists.

---

## Character / Scene Direction Analysis

The brief's §8 asks whether music→character and music→camera have been coupled. **They have not**, and
the separation is deliberate and enforced by file boundaries: `src/song/` contains no entity type and
`src/seq/section_actions.hpp` contains no camera type.

The header states the rule directly: `section_direction.hpp` says it "must not grow an opinion about
the vocabulary of behaviours, or it becomes the thing it is a seam for" — which is why the table is
authored rather than built in. A shipped default saying *a Drop means the visitor hovers* would put
one scene's cast into a generic seam.

**This is the design the brief asks for**: the camera director photographs what is happening in the
world without deciding what every character does.

---

## UI Analysis

| moment | what the user sees |
|---|---|
| after import | the Sequence panel, an audio lane, and (if ticked) analysis running as a job |
| after Analyze | the **Sections lane** populated from the film timeline; blocks coloured by category |
| selecting a section | an inspector: name, **type** picker (the person's vocabulary), **shot** picker (`default` clears the override), times, per-field provenance, `+ New type…` |
| where `sectionDirection` appears | **only** as the disabled checkbox in the import popup |
| Song Mode | Auto-director panel, third radio beside Continuous shot and Edited sequence |

**Is there an obvious beginner path?** Functionally yes; *legibly*, no. The path is Import → Analyze →
Auto-director: Song → Play, but the Analyze step sits beside a disabled control whose name contains
"Director", which is the single most misleading thing in this workflow.

---

## Data Ownership

| Concept | Current Owner | Intended Owner | Notes |
|---|---|---|---|
| Song | `audio::AudioFile` + `analysis::AnalysisTrack` | same | — |
| Song Analysis | `analysis::SongStructure` | same | evidence, not decision (ADR-215) |
| Section | `song::Section` in `SectionTimeline` | same | the film |
| Section Type | `song::SectionType` in `ShotLanguage` | same | 60 built-in, plus custom |
| Shot Intent | `song::ShotIntent` in `ShotLanguage` | same | semantic, opaque id downstream |
| Shot | `app::SongDirection` shots on `CameraShot` | same | — |
| **Section Direction** | `seq::SectionDirectionSet` on `seq::Sequence` | **same, renamed** | correct owner, misleading name |
| Character Direction | `entity::EntityWorld` + action system | same | — |
| Camera | `scene::CameraRig` | same | — |
| Director | `app::camera_director` + `song_director` | same | — |
| Multi-Camera | `scene::camera_rig` | same | — |

**Every row's current owner is already the intended owner.** The only change indicated is a name.

---

## Existing Defaults

* **60 section types** in `src/song/section_type.cpp`, across Structural / Energy / Texture /
  Cinematic / Custom, each naming a `defaultShotIntent`.
* **Shot intents** in `src/song/shot_intent.cpp`, each carrying focus, framing range, movement,
  energy, variation, cut frequency, visual density, camera count and arc.
* **Auto-director settings** — shot timing, pace caps, dwell, seed (`AutoDirectorSettings`).
* **`ShotIntent::atProgress`** applies the arc, so Build rises and Pause suspends **generically**,
  without musical rules in the director.

**No duplication found.** Built-ins are code and are never serialized, so a project cannot resurrect a
definition the engine has since improved.

**Conclusion for §12: AV Gen already contains enough default information for a useful first-pass Song
Director experience with nothing authored.**

---

## Preserve / Adapt / Replace

### Preserve
* The whole `src/song/` model — types, intents, language, cue sheet, reanalysis.
* `songPlanForEngine`'s three-tier resolution.
* `SongDirector` and its multi-camera consumption.
* `section_actions.cpp`'s verb mapping and `applySectionActions`.
* The cue-isolation build guard.

### Adapt
* **The checkbox's name and placement.** "Generate initial Director sequence" should say it is about
  characters, and probably does not belong in the audio-import popup.
* **`sectionDirection` as an identifier** — "direction" reads as camera direction. Something like
  `sectionPerformance` or `sectionStaging` would not.
* **The table needs an editor.** Hand-editing JSON is the only producer.

### Replace
* Nothing. No system's architecture conflicts with the intended model.

---

## Migration Risks

Low, because no architectural change is indicated. If the *naming* changes are taken:

* **Serialization** — the JSON key `"sectionDirection"` is in `glowmere-valley-2-multicam.json`.
  Renaming needs a read-both/write-new migration or the table silently vanishes (an unknown key is
  ignored by design — which is itself the risk: it fails **silently**).
* **Tests** — `test_section_actions.cpp` names the type and the key.
* **Docs/ADRs** — ADR-216 and `docs/project-format.md` name it.
* **User projects** — anyone who hand-authored a table.
* **No risk** to multi-camera, the sequencer, cached analysis, reproducibility or existing renders:
  none of them reference it.

---

## Test Coverage

| area | file | covers |
|---|---|---|
| section timeline | `test_section_timeline.cpp` | edits, provenance, boundaries |
| reanalysis | `test_song_reanalysis.cpp` | override preservation |
| shot language | `test_shot_language.cpp` | types, intents, custom definitions |
| cue isolation | `test_section_cue_isolation.cpp` | **build fails** if the director can see section types |
| song director | `test_song_director.cpp` | plan → shots, multi-camera, variation |
| plan adapter | `test_song_plan_adapter.cpp` | axis ordering; the film reaching the director |
| section actions | `test_section_actions.cpp` | verb mapping, round-trip, the wire to the action system |
| camera rig | `test_camera_rig.cpp`, `test_camera_multicam.cpp` | resolution, shots, events |

**Untested, and the notable gap:** no test drives *import → analyze → Song mode → play* end to end.
Each link is tested; the chain is not. That is exactly the shape of gap that let
`songPlanFromCues` sit unwired while every test passed — see the diagnosis.

---

## Final Diagnosis

**1. Why does the implementation require a table before generating an initial director sequence?**
It does not, for *camera* direction. It requires one for the **character-action** generator, because
that generator has nothing to say without one — there is no defensible built-in answer to "what
should the cast do on a Drop" that is not a claim about one specific scene's cast.

**2. Necessary, or an implementation choice?**
A deliberate choice, and the right one for what that feature does. `section_direction.hpp` refuses to
hold a vocabulary of behaviours on purpose. The *presentation* of that choice is the defect.

**3. What is `sectionDirection` responsible for?**
Turning section boundaries into character actions. Nothing else.

**4. Does that responsibility belong in the Song Director architecture?**
No — and it is not in it. It is a sibling that shares the section timeline. Keeping them apart is what
lets the camera director photograph the world without staging it.

**5. Is there enough infrastructure for Import → Analyze → Initial Shots → Song Director without a
user table?**
**Yes, and it is already wired.** 60 types, their default intents, the cue sheet, the adapter, the
three-tier plan resolution and the multi-camera system are all present and connected.

**6. Smallest change that makes the workflow possible?**
None is needed to make it *possible*. To make it **legible**, the smallest change is to the checkbox:
rename it to say it is about performers, move it out of the import popup, and have its disabled
tooltip point at the Song Mode path rather than implying it is a prerequisite.

**7. What should be preserved?**
Everything listed under Preserve — which is nearly all of it.

**8. What should become optional/advanced rather than mandatory?**
Nothing is currently mandatory that should not be. The table is already optional; it only *looks*
mandatory.

**9. What should the future architecture look like?**
The one that exists, with clearer naming around the character-direction sibling.

**10. Concrete phases for a future agent?**
1. Rename the concept and migrate the JSON key with a read-both shim, then retire the old spelling.
2. Move and rename the checkbox; rewrite its tooltip to point at Song Mode.
3. Build a small editor for the table (the only genuinely missing capability).
4. **Add the end-to-end test** — import, analyze, Song mode, assert shots on more than one camera.
   This is the highest-value item on the list and the only one that closes a real hole.

---

## Proposed Future Architecture

Unchanged from the current one, plus:

```
  seq::SectionPerformanceSet   (was: SectionDirectionSet)
        │   authored, optional, with an editor
        ▼
  generatePerformanceEvents ──► EntityAction ──► EntityWorld::direct
```

running beside — never through — the camera chain.

---

## Recommended Implementation Phases

See Final Diagnosis §10. **Phase 4 should arguably be first:** it is the only item that would have
caught a real defect, and this session already found one of exactly that shape — `songPlanFromCues`
was written, correct, tested, and **not called by anything** for its entire life, because every test
covered a link and none covered the chain.

---

## What this audit changed

Nothing in production. The audit was read-only, as specified.

Two things were committed *before* it began and are noted here so a reader is not confused by the
timeline: `songPlanFromCues` was wired into `songPlanForEngine` (commit `84ab8b5`), and the starter
table was added to the multi-camera demo (`c2f6105`). Both predate this document; neither was made in
response to it.
