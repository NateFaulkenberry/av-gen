#pragma once

// glTF 2.0 / GLB import via fastgltf (ADR-005). Appends meshes, textures, materials, entities,
// lights and cameras into a Scene. Hierarchies are flattened to world transforms; one Entity is
// created per mesh primitive so each carries its own material.

#include "core/error.hpp"
#include "scene/scene.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace avgen::assets {

struct GltfLoadOptions {
    bool loadImages = true;        // decode textures (false: materials keep factors only)
    bool generateNormals = true;   // compute smooth normals when a primitive has none
    std::string namePrefix;        // prepended to entity names (e.g. "helmet/")
};

struct GltfLoadSummary {
    std::size_t meshes = 0;      // primitives imported as MeshData
    std::size_t entities = 0;
    std::size_t materials = 0;
    std::size_t textures = 0;    // decoded images
    std::size_t lights = 0;
    std::size_t cameras = 0;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<std::string> warnings; // unsupported features encountered (skipped, not fatal)
};

// Loads .gltf or .glb (external buffers/images resolved relative to the file). Fails on parse or
// I/O errors; unsupported features produce warnings. The Scene is left untouched on failure.
Result<GltfLoadSummary> loadGltf(const std::filesystem::path& path, scene::Scene& into,
                                 const GltfLoadOptions& options = {});

} // namespace avgen::assets
