// Temporary: removed when spatial/*.cpp land (Wave 1 agents). Keeps the app linking.
#include "scene/procedural.hpp"
namespace avgen::scene {
MeshData makePointQuad(float size) {
    MeshData m;
    const float h = size * 0.5f;
    m.vertices = {Vertex{{-h, -h, 0}, {0, 0, 1}, {0, 0}}, Vertex{{h, -h, 0}, {0, 0, 1}, {1, 0}},
                  Vertex{{h, h, 0}, {0, 0, 1}, {1, 1}}, Vertex{{-h, h, 0}, {0, 0, 1}, {0, 1}}};
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}
} // namespace avgen::scene
