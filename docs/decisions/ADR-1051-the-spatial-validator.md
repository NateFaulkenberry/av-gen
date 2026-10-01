# ADR-1051: The room / spatial composition validator

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 3, `03-art-pass-3-addendum.md` §1-15)
- `src/scene/space_validator.{hpp,cpp}` (the checks), `src/app/space_validate_cli.{hpp,cpp}` (the command,
  `avgen --validate-space`), `tools/liminal_space.py` (the Python hook), `tools/liminal_text.py` (lyric annotations).
- Tests: `tests/unit/test_space_validator.cpp` (`[adr1051]`, also `[space]` and `[liminal]`).

## Context

The owner asks for a deterministic check of rooms that answers "is this spatial arrangement actually valid?"
before a render reaches the Creative Critic, which answers "does it look good?". Typical defects are a chair
through a table, a couch through a wall, furniture floating or buried, a lyric over a window, a blocked
doorway, a camera path through a wall, the mannequin missing its head. The check must read the semantic scene
rather than pixels, report machine-readable violations with measurements and suggested corrections, and run
headless without the GPU. It should also be small, extensible and data-driven, not a large taxonomy.

The All You Got worlds are SDF trees: every piece of furniture is a subtree of primitives in an SDF object,
and lyrics are mesh text nodes (ADR-1046). Nothing in the engine knew what any subtree *was*.

## Decision

1. **Semantics are annotations in the scene file.**
   - Any SDF node, or any composition node, may carry
     `"entity": {"id", "category", "room", "anchor", "pose", "interior", "hip", "normal", "t0", "t1", ...}`.
   - A node inside an entity may carry `"part": "head"`.
   - The engine ignores both keys, so an annotated scene renders byte-identically. A save from the app drops
     them; the generator is the source of truth.
   - The entity's frame is the frame its node sits in. Its geometry is the annotated subtree. When the
     subtree is a difference's first child, the geometry is the difference, so a wall takes its cuts.
   - The kit convention gives each prop its front and up: base on y = 0, front facing +Z.
2. **Affordances are rules: data with built-in defaults, deep-merged with a rules file.**
   - Categories carry these fields:
     - `group` (architecture, furniture, decor, character, typography);
     - `rests` (floor or surface), `mounts` (wall or ceiling), `embeds` (wall);
     - `faces` (targets, maxDistance, maxAngle), `mayIntersect`, `keepClear` (a usable volume as fractions of
       the bounds);
     - `requires` (components), `supports`, `seatHeight`/`surfaceHeight`, `opening` (door or window),
       `facesRoom`, `expect` (the expected relationship, in words).
   - Poses map to a support (`floor`, `seat` or `top`), the affordance they need, and optionally `facesAnchor`.
   - Tolerances, lyric margins, door clearance and camera clearance are rules too.
   - `avgen --validate-space --dump-rules` prints the whole set.
3. **Measurements use the engine's CPU SDF evaluator.**
   - Each entity is evaluated with `spatial::SdfTree::evaluate`, the reference the GPU parity tests hold the
     shaders to.
   - **Bounds:** tight bounds come from conservative grid sampling of an analytic box. Anything unbounded, such
     as an infinite repeat, is clipped to the object's march bounds, since nothing outside those is drawn.
   - **Intersections:** sampled over the overlap of the two bounds. The measure is the maximum of
     `min(-dA, -dB)`, so contact within a tolerance is not a collision.
   - **Floor contact, and contact with the ceiling:** sphere tracing along vertical columns.
   - **Text:** measured from its real glyph mesh (`cachedTextMesh`).
4. **What coexists is what the journey shows together.**
   - Each chapter's node list, plus the objects no chapter lists, forms a group. Groups with identical object
     sets are merged.
   - Entities are checked only against others in their group, and each violation lists the groups it was seen
     in.
   - A lyric is checked against the walls of its `room`. Without one, it is checked against the room whose wall
     plane it lies on. When several rooms fit, it is checked against all of them and the best-fitting result
     is reported.
5. **The checks** (rule names as they appear in the report):
   - `integrity`: required parts, and degenerate parts.
   - `clipped`: geometry outside the object's march bounds, which is never drawn. This is checked for
     annotated entities and parts, and also for every top-level piece of every object, annotated or not.
   - `intersection`, with expected attachments allowed.
   - `floor`, `tilt` and `support` for surfaces, walls and ceilings; `wallExtent`.
   - `orientation` and `relationship` (a chair to its table or desk, a chair placed on top of one), `keepClear`.
   - `doorway` (clear width, from columns sampled through the clearance volume) and `window` (blocked);
     `access` (a room with no door).
   - `pose` (hips over the seat at seat height; lying along a top surface; standing on the floor), checked
     against an anchor that must be present.
   - `lyricPlacement` (on the wall plane, facing the room, inside the wall, overlapping windows, doors,
     furniture, mounted things and words shown at the same time), `lyricClearance` (a configurable margin,
     including from the wall's edges), and `lyricOrientation` (mirrored, upside down, tilted).
   - `cameraPath`: each chapter's path at eye height against the chapter's objects.
     - From a project, the eye height is the project's constant `camera/journey/height`.
     - The span checked is the part of the path the film's `camera/journey/distance` keys visit.
     - From a bare scene, the whole authored path is checked.
6. **Output.**
   - JSON: summary counts, violations with `severity`, `rule`, `entities`, `message`, `measured`, `expected`,
     `suggestion`, `fix` and `groups`, plus the entities and the groups.
   - The section 14 text report.
   - `fix` is machine-applicable: a `translate`, a `yaw`, a new `position`/`normal` for a word, or new march
     bounds for an object.
   - `tools/liminal_space.py`:
     - `instrument_kit(kit)` tags every kit prop with its category, and the mannequin with its pose, hip and
       head part.
     - `validate()` returns the report.
     - `apply_fixes()` applies the safe fixes, by default the floor, support, lyric and bounds ones, by
       wrapping the entity's node in one translate.

## Consequences

- **The pass-2 film, generated with the kit instrumented, measures as follows.** The suites do not cover these
  findings; they are listed in PROGRESS-eng.md.
  - The mannequin in the final shot has its head clipped by its object's march bounds (§33's "missing head").
    This is caught with or without annotations.
  - `kit.armchair()` has an infinite row of seat cushions: `couch(seats=1)` gives `repeat` a count of 0, which
    means infinite. The row is clipped only by `livMedia`'s bounds, and it passes through the wall, the cabinet
    and a window.
  - The cabinet and the wardrobe float 5-6 cm (they are built from y = 0.05).
  - About 50 lyrics overlap windows, curtains, paintings or furniture.
  - A door is blocked by a plant and a box.
- **It is a static check.**
  - It reads the scene's authored values. Keyed motion (a word rising into place, a growing room) is not
    evaluated, except the `rise` style's rest offset, which the text helper writes into the annotation.
  - Entities under a repeat, mirror, fold or similar get one INFO line and are not placed.
  - Mesh (glTF) nodes are not entities yet.
- **Unannotated scenes still get the checks that need no semantics:** clipped pieces, the camera path, and the
  orientation of lyrics on walls.
- **Cost:** 2-3 s for the whole film (375 entities, 56 objects) on the CPU.
