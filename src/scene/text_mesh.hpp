#pragma once

// Text as geometry (ADR-1046): words that stand in the world, lit, in perspective, behind and in front
// of other things -- the spatial lyric typography of the All You Got art pass 2.
//
// The 2-D text layers (ADR-083) already shape text with the platform's type engine and keep a signed
// distance field per glyph. This turns those same fields into a closed, extruded triangle mesh: the
// glyph's inside (field > 0.5) is traced cell by cell with marching squares, each cell's inside polygon
// becomes the front and back faces, and each contour segment becomes a side wall. Neighbouring cells
// compute a shared edge's crossing from the same two samples in the same order, so the faces and the
// walls meet exactly: the mesh is watertight. Full interior cells are merged into row runs, so a word
// costs a few thousand triangles, not tens of thousands.
//
// Units: one em is `size` metres. The text faces +Z, its back face at z = 0 and its front at
// z = depth * size, so a node placed on a wall with +Z out of the wall puts the word flush on it.
// Horizontally the lines are aligned left/centre/right about x = 0; vertically the block is centred
// (`middle`) or its first baseline is at y = 0 (`baseline`).
//
// Deterministic: the font backend's fields are deterministic (ADR-083) and the trace is integer-ordered.
// A font that is not installed is substituted loudly by the backend, as for the layers.

#include "comp/font.hpp"
#include "core/error.hpp"
#include "scene/scene_types.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace avgen::scene {

enum class TextAlign3d : std::uint8_t { Left, Center, Right };
enum class TextVAlign3d : std::uint8_t { Middle, Baseline };

struct TextMeshSpec {
    std::string text;
    comp::FontDesc font;
    float size = 1.0f;     // metres per em
    float depth = 0.08f;   // extrusion, in em (0 = a flat, two-sided plate)
    float tracking = 0.0f; // em added after every glyph
    TextAlign3d align = TextAlign3d::Center;
    TextVAlign3d valign = TextVAlign3d::Middle;
};

[[nodiscard]] Result<MeshData> makeTextMesh(const TextMeshSpec& spec);
// The same, memoised on everything that decides the geometry (bounds and culling ask every frame).
// Thread-safe. A failure is not cached, so installing a font and reloading recovers.
[[nodiscard]] Result<std::shared_ptr<const MeshData>> cachedTextMesh(const TextMeshSpec& spec);

// The trace on its own, for a test: one field (row 0 at the top, inside > 0.5) as a mesh in texel
// units, x right and y up, with the field's centre texel at (0.5, 0.5).
[[nodiscard]] MeshData traceFieldMesh(const comp::SdfBitmap& field, float depthTexels);

} // namespace avgen::scene
