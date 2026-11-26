#pragma once

// A surface's material at one point, assembled in the order the raster's lit shader assembles it (ADR-2246).
//
// shaders/pbr_shade.wgsl `shadeSurface` (entities, and procedurals through procedural.wgsl `procShade`) does, per
// fragment:
//
//   1. the material's FACTORS (base colour, opacity, roughness, metallic, emissive colour x intensity) are the base;
//   2. the material program, when the object has one and its gate admits this instance at this camera distance, runs
//      over that base and replaces what it writes (`evaluateMaterialProgramFast`);
//   3. the base colour is multiplied by the instance's colour lane, then by the base-colour texture; roughness and
//      metallic by the metallic-roughness texture;
//   4. the emission is the (program's or factor's) emissive x the instance's emissive lane (dropped when the program
//      writes emission itself) x the emissive texture x intensity, then the instance's variation as a hue rotation
//      and gain when the program left it to the engine, and the object's lane (ADR-903/905) last.
//
// This file is that order on the CPU, through the CPU twins the engine already has and tests against the WGSL:
// `scene::evaluateMaterialProgram` (tests/rendering/test_material_gpu.cpp), `color::emissionVariationOf`,
// `color::livingChromaTurns`, `color::hueShift`. It is not a second material system: nothing here decides what a
// material means, it only calls the functions that already do, in the shader's order.
//
// Inputs a ray tracer has no value for -- screen-space derivatives (curvature, cavity, normal variance, footprint),
// the GTAO occlusion buffer, audio and beat, the tile and landing glows -- are given the neutral value and the
// programs that read them are reported (snapshot.cpp, "material program input").

#include "pathtrace/bsdf.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace avgen::pathtrace {

struct Snapshot;

// Where on which surface: the same identity `SurfaceHit` carries, plus the world-space point and shading normal.
struct ShadingPoint {
    bool instanced = false;
    std::uint32_t object = 0;    // index into Snapshot::meshes, or Snapshot::instanced when `instanced`
    std::uint32_t instance = 0;  // which copy, when `instanced`
    std::uint32_t prim = 0;
    float b0 = 1.0f;             // barycentric weight of the triangle's first vertex
    float b1 = 0.0f;             // ... second
    float b2 = 0.0f;             // ... third
    glm::vec3 position{0.0f};    // world space
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // world-space shading normal, facing the viewer (the raster's front-facing n)
};

// The material at `p`, with textures sampled, the program run and the emission lanes applied.
[[nodiscard]] SurfaceMaterial shadeSurfaceMaterial(const Snapshot& snap, const ShadingPoint& p);

// The interpolated texture coordinate at `p` (zero when the mesh has none).
[[nodiscard]] glm::vec2 surfaceUv(const Snapshot& snap, const ShadingPoint& p);

} // namespace avgen::pathtrace
