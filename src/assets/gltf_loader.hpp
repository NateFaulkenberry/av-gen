#pragma once

// glTF 2.0 / GLB import via fastgltf (ADR-005). Appends meshes, textures, materials, entities,
// lights, cameras and skinned rigs into a Scene. Hierarchies are flattened to world transforms;
// one Entity is created per mesh primitive so each carries its own material.
//
// ADR-086: a skin becomes a scene::SkinnedRig -- the joint hierarchy, the inverse binds, and every
// animation in the file that touches it, as clips with glTF's three interpolation modes intact.
// The rig keeps the whole node chain above the joints, because that is where a Mixamo export puts
// the channel that moves the character. A skinned mesh node's own transform is ignored, as the
// specification requires: the joint matrices already carry the file's scene space, and the entity's
// transform is left for whoever places the character in the world.

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
    std::size_t rigs = 0;        // ADR-086: skins imported as scene::SkinnedRig
    std::size_t joints = 0;      // joint nodes across every rig (the skins' joints and their ancestors)
    std::size_t clips = 0;       // animation clips across every rig
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<std::string> warnings; // unsupported features encountered (skipped, not fatal)
};

// Loads .gltf or .glb (external buffers/images resolved relative to the file). Fails on parse or
// I/O errors; unsupported features produce warnings. The Scene is left untouched on failure.
Result<GltfLoadSummary> loadGltf(const std::filesystem::path& path, scene::Scene& into,
                                 const GltfLoadOptions& options = {});

} // namespace avgen::assets
