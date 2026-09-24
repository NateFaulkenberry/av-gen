# ADR-750: The Director's engine module is `src/directing/`, and it depends on no AI code

**Status:** Accepted
**Date:** 2026-09-24
**Related:** ADR-094 (the AI control plane), ADR-091 (two-tier determinism), ADR-098 (cinematic
events), ADR-245 (cameras), ADR-210 (stage director); `docs/design/director-agent-feasibility-report.md`;
the Director program (`docs/development/director-system-progress.md`, spec §1.2, §43, §44)

## Context

The Director program adds an engine-level layer: a typed, versioned **Director Plan**, a subject and
time resolver, a capability registry, a semantic validator, and a deterministic compiler to native
content. The spec requires this layer to live outside `src/ai/`, to be usable with no LLM at all, and
to have a name that cannot be confused with the codebase's existing directors.

The codebase already has at least nine "director" concepts:

| Name | What it is |
|---|---|
| `WorldDirector` (`src/app/world_director.*`) | world macro knobs |
| camera auto-director (`src/app/camera_director.*`, `AutoDirectorSettings`, `DirectorMode`, `DirectorKnob`) | runtime camera cutting |
| `SongDirector` (`src/app/song_director.*`) | Song Mode |
| `seq::Director` (`src/seq/director.*`) | installs a `Sequence` into the timeline |
| `entity::DirectorMotion`, `Authority::Director` | the entity authority tier |
| `stage::Staging`'s director (ADR-210) | the behaviour decision layer |
| `DirectorState`, `DirectorCheckpoint`, `DirectorMapping`, `ParkedDirectorsCut` | state for the above |

Collision counts across `src/` (identifier prefixes):

| Candidate | Hits | Collides with |
|---|---:|---|
| `director` | 635 | everything above |
| `direction` | 841 | `scene::CameraDirection`, `Application::cameraDirection_` |
| `direct` | 85 | the `--direct` CLI flag, `directEngine`, `EntityWorld::direct()` |
| `cinematic` | 56 | `src/app/cinematic.*` (`app::Shot`, `ShotKind`); a third meaning for "shot" |
| `authoring` | 72 | the world editor (`docs/world-authoring-spec.md`, `docs/authoring.md`) |
| `directing` | 22 | prose only, plus the member function `CameraDirection::directing()` |

## Decision

- **Module:** `src/directing/`, namespace `avgen::directing`. It is added to the core library glob in
  `src/CMakeLists.txt`.
- **Names inside it are short, because the namespace carries the meaning:** `directing::Plan`,
  `directing::Issue`, `directing::Resolver`, `directing::CapabilityRegistry`, `directing::Compiler`.
  No type in it is called `Director`. "Director" stays the product name the user sees (the Director
  panel, "Director Plan" in docs and the progress artifact).
- **Why `directing`:** it names the activity (directing performances, cameras and cues), not an
  agent. It is the only candidate with no type, namespace or file collision. The one member function
  it shares a word with, `CameraDirection::directing()`, lives in a different namespace and cannot
  be confused at a call site.
- **Dependency boundary (spec §44):**
  - `src/directing/` may depend on `seq/`, `scene/`, `entity/`, `stage/`, `world/`, `song/`,
    `analysis/`, `params/` and `core/`.
  - `src/directing/` must not include anything from `src/ai/`, and must name no AI vendor.
  - `src/ai/` may depend on `src/directing/`: the semantic tools are thin wrappers over it.
  - `src/app/` hosts the editor seams: undo, the preview session and the panel.
  - A test (`[directing][boundary]`, added with the first source file) scans `src/directing/` for
    `#include "ai/` and for vendor names, so the rule cannot rot silently.

## Consequences

- A future MCP server or command-line tool can drive `directing::` with no provider linked.
- Grep for `directing::` finds every call into the Director layer, and finds nothing else.
- Two "shot" types remain (`seq::Shot`, `scene::CameraShot`), plus `app::Shot`. The plan schema
  (ADR on the Plan, to follow) must name which one each plan field compiles to.
