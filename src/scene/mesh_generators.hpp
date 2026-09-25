#pragma once

#include "scene/scene.hpp"

namespace avgen::scene {

// Unit-ish primitives with outward normals and CCW winding (front face) in a right-handed frame.
MeshData makeIcosphere(float radius, int subdivisions);
MeshData makeCube(float halfExtent);
MeshData makePlane(float halfExtent, int divisions); // XZ plane at y = 0, normal +Y, uv 0..1
// Effect Library Wave 3 (SHELL's canonical meshes, world/effects/shell_frame.hpp):
MeshData makeQuad(float halfExtent);                  // XY plane at z = 0, normal +Z, uv 0..1
MeshData makeDisc(float radius, int segments);        // XZ plane at y = 0, normal +Y, a triangle fan
// An open tube about +Y, y in [-halfHeight, halfHeight], outward normals; `capped` adds both ends.
MeshData makeCylinder(float radius, float halfHeight, int segments, bool capped);
// An open cone about +Y: apex at y = +halfHeight, base of `radius` at y = -halfHeight, outward
// (slanted) normals; `capped` closes the base.
MeshData makeCone(float radius, float halfHeight, int segments, bool capped);

} // namespace avgen::scene
