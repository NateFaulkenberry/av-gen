#pragma once

// The scene data model (ADR-004): value types in scene_types.hpp; this header adds the
// particle, post and procedural components and the Scene container.

#include "scene/particles.hpp"
#include "scene/post_settings.hpp"
#include "scene/procedural.hpp"
#include "spatial/field.hpp"
#include "scene/scene_types.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace avgen::scene {

// ---- scene ---------------------------------------------------------------------------------

struct Scene {
    Camera camera;                      // the active camera
    std::vector<Camera> cameras;        // imported cameras (the first becomes active on import)
    std::vector<PunctualLight> lights;
    Environment environment;
    std::vector<MeshData> meshes;
    std::vector<TextureData> textures;
    std::vector<Entity> entities;
    std::vector<ParticleSystem> particles;
    std::vector<ProceduralGeometry> procedurals; // ADR-023; evaluated by rendering::ProceduralRenderer
    spatial::FieldSet fields;                    // ADR-025; sampled by effectors, deformers, particles, materials
    PostSettings post;                // built-in post-processing (copied in by the Engine)
    std::uint64_t meshVersion = 0;    // incremented when meshes change (renderer re-uploads)
    std::uint64_t textureVersion = 0; // incremented when textures change

    MeshId addMesh(MeshData mesh);
    TextureId addTexture(TextureData texture);
    Entity& addEntity(std::string name, MeshId mesh);
    PunctualLight& addLight(PunctualLight light);
    // World-space bounds over visible lit entities (their mesh bounds transformed by their matrix).
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> bounds() const;
    void clear();
};

} // namespace avgen::scene
