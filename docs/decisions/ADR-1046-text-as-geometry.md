# ADR-1046: Text as geometry: extruded glyph meshes for spatial lyric typography

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 2)
- `src/scene/text_mesh.*` (the trace and the cache); `PrimitiveKind::Text` and its `SourceSpec` fields in
  `src/scene/procedural.*`; the data path in `tools/liminal_text.py`.
- Tests: `tests/unit/test_text_mesh.cpp` (`[text][adr1046]`).

## Context

Art pass 2 section 8 asks for lyrics *inside* the world: on walls, around corners, on floors, in perspective,
lit, at exact eighth-note times, each word addressable so it can pop, rise, flicker or slide. The engine had
only screen-space text layers (ADR-083), which are compositing overlays: no depth, no lighting, no fog.

Options considered:
- SDF glyphs inside the ray-marched world (capsule strokes of a Hershey-style font): lit and fogged like the
  walls, but every word adds nodes to an object already near the 96-node limit, and the march cost grows
  with the number of words on screen.
- A textured quad sampling the glyph atlas: cheap, but alpha-tested, unlit edges and a new material path.
- **Meshes from the glyph distance fields the layers already build.** Lit, fogged, shadowed and
  depth-composed with the SDF by the existing procedural mesh path; one draw per word.

## Decision

A procedural source kind `text` (`"source": {"kind": "text", "text", "font", "textSize", "textDepth",
"textTracking", "textAlign", "textVAlign"}`). `makeTextMesh` shapes the text with the platform type engine,
takes each glyph's signed distance field (64 texels per em), and traces it with marching squares: each cell's
inside polygon (a Sutherland-Hodgman clip; a saddle is one hexagon) becomes the front and back faces, and
each contour segment becomes a side wall. A shared cell edge's crossing is computed from its lower-index
corner in both cells, so faces and walls meet exactly. Full cells are merged into row runs. The mesh faces
+Z with its back at z = 0, so a node on a wall with +Z out of the wall puts the word flush; it is centred
by default. Meshes are memoised on everything that decides them.

A text source with no `distribution` is a single instance (every other primitive defaults to a ring of 32).
Text is never an LOD impostor. `procedural/<n>/source/kind` now runs to 9.

The word-level data path is `tools/liminal_text.py`: `place_words(entries)` turns lyric entries (text, times,
position, wall normal and in-plane tilt, cap height, colour or palette role, style) into scene nodes, one
beat-grid envelope channel per word (ADR-1045), the routes for its arrival style (pop, rise, flash, flicker,
cut), a `visible` route, and palette bindings.

## Consequences

- Measured: 303 words in one scene, 788 routes, render at 113 fps at 960x540 (M2 Max); a 3-letter word is a
  few thousand triangles.
- **At most 256 procedural objects draw at once** (the renderer's uniform slots). A zero scale still counts,
  so `place_words` multiplies `nodes/<n>/visible` by the envelope: hidden words cost nothing.
- Each word registers about 86 parameters (the procedural set), so 300 words are about 26,000 parameters.
  Loading is still well under a second.
- Fonts come from the machine (ADR-083); a missing family is substituted loudly. Tests skip without a type
  engine.
- Mesh text does not get the SDF edge light (ADR-1047); it reads through its emission and bloom.
