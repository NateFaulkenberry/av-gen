# ADR-755: The Director Plan, its identity, and how its subjects and times are resolved

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-750 (the module), ADR-752 (one undo), ADR-754 (capabilities), ADR-702 (effect
instances; not yet on main), ADR-245 (two shot types), ADR-215/247 (song structure and the section
timeline), ADR-091 (tiers). The owner's rulings of 2026-09-24: plans are saved in the project as
provenance; "the Umbra hero effect" is ADR-702's per-hero Ground Pulse on `umbra-cap`.
**Implemented by:** `src/directing/plan.*`, `issue.*`, `time_ref.*`, `resolver.*`, `text.hpp`;
`src/app/directing_context.hpp`; `Engine::directingPlans` and the project key `directingPlans`;
`ui::PlanListChange` and `app::EditCapture`'s plan capture
**Tests:** `tests/unit/test_directing_plan.cpp`, `tests/unit/test_directing_resolver.cpp`; golden
fixture `tests/data/directing/rook_umbra.plan.json`

## Context

Spec §11–§14 asks for a typed, versioned, engine-level plan between a request and native content,
plus a resolver that turns names and musical times into identities and seconds without the LLM
doing either. The owner ruled that plans are **kept in the project**, as the provenance of the
content they produced, so that a follow-up request can revise the earlier plan.

## Decision

### The document (`directing::Plan`, schema version 1)

`schemaVersion`, `id`, `revision`, `title`, `tier`, `provenance {author, source, request}`,
`subjects`, `shots`, `markers`, `performances`, `cues`, `retimes`, `produced`.

- **Items carry keys**, unique within the plan. Keys match old items to new across revisions,
  attribute diff lines, and let `produced` point back at intent.
- **Subjects are declared once, by alias.** Items name aliases. The resolver fills each alias's
  canonical `{kind, id}`, so ambiguity is reported once, not per item.
- **Shots** compile to *both* shot types (ADR-245): the `seq::Shot` (the framing) and, when a camera
  is named or implied, the `scene::CameraShot` (which camera is live). They are written together so
  they cannot disagree. `camera` is an ordered list of semantic moves (spec §15's thirteen), so
  "chase → rise_over → pass" stays intent after compilation.
- **Performances** carry the request's beats verbatim, including actions no character can do
  (`backflip`). Refusing them is the validator's job (1.3), and the plan must still say what was
  asked. Plan events (`emits: "rook.backflip_peak"`) are named here; their *times* are computed by
  the compiler (spec §1.4), never written by the model.
- **Cues** target exactly one of a parameter path (the escape hatch) or an **`EffectRef`**
  `{id?, owner, type}`. This is ADR-702's addressing: an instance by scene-unique id when known,
  otherwise by owner and type. "The Umbra hero effect" is `{owner: umbra, type: groundPulse}`. A cue
  starts `at` a time or `on` a plan event, one or the other.
- **Retimes** are performance-local (spec §33). No scene clock is modelled.
- **Not in v1:** a free-form constraint list (every v1 constraint is a typed field), anything
  vendor-specific (`provenance.source` is free text nothing reads), and global time warp.
- **Canonical JSON.** The same plan always serialises to the same bytes, so plans diff and
  fingerprint cleanly.
- **Versioning.** The version is read first and alone. A newer document is refused whole with
  `SCHEMA_VERSION_UNSUPPORTED` (not recoverable). An unknown field in a readable version is a
  `SCHEMA_UNKNOWN_FIELD` *warning* with the nearest real field as a suggestion. That is how a
  model's typo is found, instead of the field silently doing nothing.

### Identity and the link to content

- `id` is stable and readable (`mintPlanId`: "rook-umbra", then "rook-umbra-2"). It never contains
  '/'. A follow-up request names it.
- `revision` counts accepted revisions from 1. A revision **replaces** the plan in the project. The
  project keeps only the current revision; the edit history keeps the earlier ones, and undo
  returns them.
- `produced: [{item, domain, id, fingerprint}]` records every piece of native content the current
  revision created: its domain (`sequence.shot`, `camera.shot`, `effect.instance`, …), its native
  id, and a fingerprint of the content as compiled. A revision diffs against it. **A fingerprint
  that no longer matches means the person edited that content by hand, and a revision must show
  that, never silently overwrite it** (to be enforced by the compiler, 1.4).

### Persistence and undo

- Project key `directingPlans`, written only when there is a plan, so a project that never used
  the Director is unchanged. Cleared on load when absent, and on New.
- **A plan this build cannot read is kept verbatim and written back.** It produces a project
  warning, and is never dropped by a save, which is the defect family ADR-751 was the sixth member
  of.
- `ui::PlanListChange` in `EditCommand`, captured by `app::EditCapture`. Applying a plan writes the
  plan and its content in one command, and undo takes back both. Otherwise the project would hold
  the provenance of content that no longer exists.

### Time (`TimeRef`, `parseTime`, `resolveTime`)

- A `TimeRef` is the request: seconds, bar+beat (1-based), or section+occurrence+anchor, plus an
  offset. It keeps the text as written. `parseTime` accepts clock, seconds, bar, ordinal and
  numbered sections, "start/end of", and "+ 1.5s". Anything else is `MALFORMED_TIME`, never a guess.
- `resolveTime` reads a `MusicalContext`, a plain value the host builds
  (`app::musicalContextFor`). The module never reads the engine.
- **Sections count as runs.** Consecutive timeline entries of one type are one passage. Glowmere's
  first chorus is three entries (89.1–111.3 s), and "the second chorus" is the one that comes back,
  at 118.6 s, not the second entry at 96.5 s.
- **Sources.** The person's section timeline is used first; the analyser's structure only when there
  is none. The source is named in every explanation.
- **Bars.** The analysed beat grid is used first. A constant-tempo fallback is a *warning*. With
  neither, the bar is unresolvable.
- **Refusals.** "The chorus" in a song with several is `AMBIGUOUS_TIME`, with the candidates. An
  unknown type is `UNRESOLVABLE_TIME`, with the available types and the nearest spelling. A time
  outside the piece is `TIME_OUT_OF_RANGE`.

### Subjects (`SubjectIndex`, `resolveSubject`)

1. **One identity per thing.** An entity carries its node. A hero on an entity's node marks that
   entity as a hero (`hero = true`) rather than listing it again. A hero on any other node is a Hero
   identity that carries the node. Cameras are identified by slug and answer to their display name.
   Parameters match by exact path only.
2. **Kind words are hints.** A trailing one ("the Hero Free Roam camera") leaves the name whole.
   Otherwise they are lifted out ("the Umbra hero mushroom").
3. **Exact before partial**, where partial means the candidate's name contains the first
   significant word.
4. **Never choose silently.** More than one candidate is `AMBIGUOUS_REFERENCE`, listing them all.
   None is `UNKNOWN_SUBJECT`, with the nearest names, or with the same name under another kind. A
   resolution the model or a person already chose is *checked*, never trusted.
5. **Effects** are `UNSUPPORTED` until ADR-702's instance list is on main.

## Consequences

Measured on the benchmark:
- "Rook" is entity `rook`.
- "Rookk" is unknown, and suggests `rook`.
- "Umbra" is ambiguous: hero `umbra-cap` plus four `umbra-*` nodes. It matches spec §14's example
  as the scene actually is.
- "the Umbra hero mushroom" is hero `umbra-cap`.
- "1:30" is 90 s, and "the second chorus" is 118.6 s.
- "the bridge" is unresolvable, because this film calls it `middle_eight`, and the resolver says so.

The golden benchmark plan (`tests/data/directing/rook_umbra.plan.json`) parses with no issues,
round-trips to an equal plan and identical bytes, and its subjects and times resolve with no
issues. Proven red: removing run merging, the hero/node collapse, keeping unreadable plans, and
the plan capture each fail their tests.

**Open:** the compiler's use of `produced` fingerprints, and revision semantics end to end, are
Slice 1.4 and 1.5. They are designed here and not yet implemented.
