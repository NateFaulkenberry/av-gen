# ADR-1056: The spatial validator learns architecture: openings, sills, trim, connections, containment, text, tiers

- Status: Accepted (2026-10-02), proto/liminal-space (All You Got art pass 4, PART 1)
- Extends: ADR-1051 (the room / spatial validator). Not a parallel validator: the same entities, rules file, report
  schema and CLI (`avgen --validate-space`).
- Code: `src/scene/space_validator.*` (`checkStructure`, `checkConnections`, `checkContainment`, `checkTextGeometry`,
  the lyric edge messages, the INFO tier, `formatSceneValidationMarkdown`, `mergeFilmReport`);
  `src/app/space_validate_cli.cpp` (`--md`, visibility spans).
- Tests: `tests/unit/test_space_validator_pass4.cpp` (`[adr1056]`).

## Context

The owner (pass 4, PART 1): window sills that do not align with windows, wall details through doors, text off its
walls and through geometry, objects through furniture, structure that does not connect, "rather than simply
manually fix these instances ... investigate whether we can improve the underlying validation workflow". He wants
actionable numbers ("exceeds wall bounds by 0.21m on the right edge"), and expected intersections kept apart from
suspicious and invalid ones.

## What the scene already knew (Phase 1)

- **Entities** (ADR-1051): the kit's instrumented calls tag rooms (with interiors and wall thickness), doors,
  windows, stairs, furniture, decor, mannequins and their parts; `liminal_text` tags words (`wallText`,
  `floorText`, `stairText`, `floatingText`) with their `t0`/`t1` spans and room. 230 words and about 140 SDF
  entities in pass 3.
- **Geometry**: every entity's own SDF subtree (exact CPU distances, `spatial::SdfTree::evaluate`), and for a room its
  shell WITH its cuts (the shell is the first child of the difference that cuts its doors and windows).
- **Time**: entity spans, the project's `camera/journey/distance` keys (which chapter is on screen when), and its
  `nodes/<n>/visible` step tracks (now read too).
- **Not known**: trim (the kit's `wall_band`/`skirting` are untagged pieces), sills (part of `window_frame`), roofs,
  railings, landings (no categories), and anything about motion (that is ADR-1057).

## Decision

1. **Openings are measured against the geometry, not the generator's numbers.** For each door and window the
   validator finds its wall, then scans the room's own SDF (cuts included) at mid-wall depth from the opening's
   centre to find the actual opening rectangle.
   - `opening` (critical): the wall is solid behind the window/door. `"blind": true` opts out.
   - `openingAlignment`: the frame's rectangle (a window: its rows of geometry minus the sill; a door: the hole of
     its architrave) against the opening, per edge as seen from inside the room, in metres; + is "past the opening",
     - is "a gap". Warning above 1.5 cm, critical above 5 cm, with a translation that centres it.
   - `sill`: the sill's top (a `sill` part, else the bottom run of rows wider than the frame) against the opening's
     lower edge and the frame's.
   - `openingCrossed`: anything solid in the opening's passage except its own frame, mullions and glass, a leaf in
     its frame, and what may intersect it (curtains). Unannotated geometry is named by its object piece and, when it
     is a long low band, called trim with its height.
2. **Connections.** `stairs`: the lowest tread on a floor, the top within a riser of a floor (a room's floor, a
   `landing`, `floor` or `balcony` entity); `"terminates": true` for a deliberate dead end. `roof`: its underside on
   its building's top (`anchor`, else the building or room under it) and covering 90% of its footprint. `railing`:
   its height above the treads under it varies by under 15 cm, and it stays over them.
3. **Containment.** `containment`: an entity's centre outside its declared room (critical) or its box through a wall
   by more than 5 cm (unless the intersection check already said so). `roomKind`: categories with `rooms` (washer and
   dryer: laundry; treadmill, weightBench, weightRack, exerciseBike: gym; counter, fridge, stove, dishwasher:
   kitchen) are in a room whose id or `kind` matches. `againstWall`: beds, wardrobes, shelves, counters, fridges,
   cabinets have their back within 15 cm of a wall (`"freestanding": true` opts out). `chairSpacing`: chairs anchored
   to one table are at least 0.5 m apart.
4. **Text.** The wall-bounds error says which edge and by how much; a word sunk into its wall is reported; a
   `textIntersection` check samples every word's face against every SDF object shown with it (by chapter list, or by
   the chapters the film is in during the word's span, or only the objects every chapter shares; and only while the
   object is visible); `textOverlap` compares every two words shown at once (oriented boxes); `textSelfOverlap`
   reports tracking below -0.05 em.
5. **Tiers.** Each violation carries `tier`: critical (ERROR), warning (WARNING), info (INFO). Expected overlaps (a
   category's `mayIntersect`, a child within its anchor's allowance) are now reported as INFO
   (`expectedIntersection`) instead of silently dropped, and are never errors.
6. **The report.** `--md <file>` writes the owner's "Scene Validation Report": tier counts, counts by rule and tier,
   every critical and warning with its fix, the first 40 infos, the film pass's statistics, the passes.

## Consequences

- On pass 3 (static) it reports 24 critical and 34 warnings; 23 of the criticals are one kit defect (the skirting
  and dado bands run across every doorway, and the dado across the front window), which is the owner's "wall details
  intersecting doors".
- All of it is data-driven: categories, `rooms`, `againstWall`, `moves`, tolerances under `structure`, overridable with
  `--rules`.
- Gaps: glyph-level self-overlap (the text mesh has no glyph ranges; tracking is the proxy); curved or non-axis
  walls (rooms are axis-aligned boxes in their frame, as in ADR-1051); trim and sills are found geometrically unless
  tagged (`trim` category, `frame`/`sill` parts).
