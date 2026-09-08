#pragma once

#include "scene/scene.hpp"

namespace avgen::scene {

// Unit-ish primitives with outward normals and CCW winding (front face) in a right-handed frame.
MeshData makeIcosphere(float radius, int subdivisions);
MeshData makeCube(float halfExtent);
MeshData makePlane(float halfExtent, int divisions); // XZ plane at y = 0, normal +Y, uv 0..1

} // namespace avgen::scene
