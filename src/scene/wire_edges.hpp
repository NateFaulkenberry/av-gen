#pragma once

// ADR-1073: a mesh's edges as a list of screen-space line quads, for the wire vertex entries
// (`vs_proc_wire` in shaders/procedural.wgsl, `vs_entity_wire` in shaders/pbr.wgsl).
//
// Each edge is four vertices and six indices. A vertex carries its own endpoint and the OTHER one, so the
// vertex stage can run the surface's whole transform chain on both and expand the quad across the
// projected segment:
//
//   position / normal         this endpoint, in the source mesh's space (the normal feeds the deformers
//                             that push along it)
//   corner                    x = side of the line (-1 / +1), y = +1 at endpoint A, -1 at endpoint B
//   otherPosition / otherNormal  the edge's other endpoint
//
// Vertices that share a position (a flat-shaded cube's corners, a UV seam) are welded before the
// adjacency is built, so a seam is not mistaken for a boundary and every edge is drawn once.

#include "scene/scene.hpp"

#include <cstdint>
#include <vector>

namespace avgen::scene {

struct WireEdgeVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 corner;
    glm::vec3 otherPosition;
    glm::vec3 otherNormal;
};
static_assert(sizeof(WireEdgeVertex) == 56);

struct WireEdgeMesh {
    std::vector<WireEdgeVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::uint32_t edgeCount = 0;
};

// `mode` 1: boundary edges and edges whose faces meet at more than `creaseDegrees`; 2: every edge.
// Degenerate triangles are skipped. At most `maxEdges` edges are emitted (the rest are dropped, and the
// count says so).
[[nodiscard]] WireEdgeMesh buildWireEdges(const MeshData& mesh, int mode, float creaseDegrees,
                                          std::uint32_t maxEdges = 1u << 20);

} // namespace avgen::scene
